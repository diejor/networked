#include "support/fault_log.h"
#include "support/mesh_stand.h"
#include "support/netw_test.h"

#include "netw/session/frames.hpp"

namespace TestNetwMeshStand {

#if defined(NETW_TIER_HOSTED)

using namespace godot;
using netw::LocalMultiplayerPeer;
using netw::MultiplayerPeerBase;
using netw::NetwPlayer;
using netw::NetwPromise;
using netw::NetwSceneCore;
using netw::session::AcceptFrame;
using netw_test::CapturedPacket;
using netw_test::MeshStand;

PackedByteArray marked(int p_value) {
    PackedByteArray bytes;
    bytes.push_back(uint8_t(p_value));
    bytes.push_back(uint8_t(p_value + 1));
    return bytes;
}

TEST_CASE(
    "[Networked][Session] L1 the server seats as the authority while every "
    "member is an ordinary peer, so the stand declares the graph instead of "
    "inheriting a star"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(7);
    mesh.seat_member(9);
    mesh.wire(1, 7);
    mesh.wire(1, 9);
    mesh.pump(2);

    NETW_CHECK_EQ(mesh.peer_of(1)->NETW_PEER_VIRTUAL(get_unique_id)(), 1);
    NETW_CHECK_EQ(mesh.peer_of(7)->NETW_PEER_VIRTUAL(get_unique_id)(), 7);

    CHECK(mesh.session_of(1)->is_host());
    CHECK_FALSE(mesh.session_of(7)->is_host());
    CHECK_FALSE(mesh.session_of(9)->is_host());
}

TEST_CASE(
    "[Networked][Session] L2 a direct edge carries a packet from 7 to 9 with "
    "the sender, channel and transfer mode the sender actually chose, and no "
    "peer 1 appears anywhere in the record"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(7);
    mesh.seat_member(9);
    mesh.wire(1, 7);
    mesh.wire(1, 9);
    mesh.wire(7, 9);
    mesh.pump(1);
    mesh.drain_all();

    mesh.send_on(
        7,
        9,
        3,
        MultiplayerPeerBase::TRANSFER_MODE_UNRELIABLE,
        marked(40)
    );

    const Vector<CapturedPacket> taken = mesh.capture_at(9);
    REQUIRE(taken.size() == 1);
    NETW_CHECK_EQ(taken[0].sender, 7);
    NETW_CHECK_EQ(taken[0].destination, 9);
    NETW_CHECK_EQ(taken[0].channel, 3);
    NETW_CHECK_EQ(
        taken[0].mode,
        int(MultiplayerPeerBase::TRANSFER_MODE_UNRELIABLE)
    );
    REQUIRE(taken[0].bytes.size() == 2);
    NETW_CHECK_EQ(int(taken[0].bytes[0]), 40);
}

TEST_CASE(
    "[Networked][Session] L3 the 1 to 9 edge still carries after peer 7 is "
    "retired, because nothing was routing through it"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(7);
    mesh.seat_member(9);
    mesh.wire(1, 7);
    mesh.wire(1, 9);
    mesh.wire(7, 9);
    mesh.pump(1);

    mesh.retire(7);
    mesh.pump(1);

    CHECK_FALSE(mesh.peer_of(1)->is_linked_to(7));
    CHECK(mesh.peer_of(1)->is_linked_to(9));

    mesh.drain_all();
    mesh.send(1, 9, marked(70));

    const Vector<CapturedPacket> taken = mesh.capture_at(9);
    REQUIRE(taken.size() == 1);
    NETW_CHECK_EQ(taken[0].sender, 1);
    NETW_CHECK_EQ(int(taken[0].bytes[0]), 70);
}

TEST_CASE(
    "[Networked][Session] L4 a captured record survives the disposal of the "
    "peer that sent it, because it is plain data rather than a reach back "
    "into a live object"
) {
    Vector<CapturedPacket> taken;
    {
        MeshStand mesh;
        mesh.seat_server();
        mesh.seat_member(9);
        mesh.wire(1, 9);
        mesh.pump(1);
        mesh.drain_all();

        mesh.send_on(
            1,
            9,
            2,
            MultiplayerPeerBase::TRANSFER_MODE_RELIABLE,
            marked(11)
        );
        taken = mesh.capture_at(9);
        REQUIRE(taken.size() == 1);
    }

    NETW_CHECK_EQ(taken[0].sender, 1);
    NETW_CHECK_EQ(taken[0].destination, 9);
    NETW_CHECK_EQ(taken[0].channel, 2);
    REQUIRE(taken[0].bytes.size() == 2);
    NETW_CHECK_EQ(int(taken[0].bytes[0]), 11);
    NETW_CHECK_EQ(int(taken[0].bytes[1]), 12);
}

TEST_CASE(
    "[Networked][Session] L5 a broadcast from the server reaches every peer "
    "it is linked to and reaches it exactly once, so a mesh edge is not a "
    "second delivery of the same packet"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(7);
    mesh.seat_member(9);
    mesh.wire(1, 7);
    mesh.wire(1, 9);
    mesh.wire(7, 9);
    mesh.pump(1);
    mesh.drain_all();

    mesh.send(1, 0, marked(5));

    NETW_CHECK_EQ(mesh.capture_at(7).size(), 1);
    NETW_CHECK_EQ(mesh.capture_at(9).size(), 1);
}

TEST_CASE(
    "[Networked][Session] L6 a seat is an admitted peer once the graph has "
    "been pumped, so each session reaches the neighbours it is wired to and "
    "holds the role its seat declared rather than an empty roster and no role"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(9);
    mesh.wire(1, 9);
    mesh.pump(4);

    const PackedInt32Array at_server = mesh.session_of(1)->reachable_peer_ids();
    const PackedInt32Array at_member = mesh.session_of(9)->reachable_peer_ids();

    NETW_CHECK_EQ(int(at_server.size()), 1);
    CHECK(at_server.has(9));
    NETW_CHECK_EQ(int(at_member.size()), 1);
    CHECK(at_member.has(1));

    NETW_CHECK_EQ(
        int(mesh.session_of(1)->session_get_role()),
        int(netw::NetwMultiplayer::ROLE_LISTEN_SERVER)
    );
    NETW_CHECK_EQ(
        int(mesh.session_of(9)->session_get_role()),
        int(netw::NetwMultiplayer::ROLE_CLIENT)
    );
}

TEST_CASE(
    "[Networked][Scene] L7 a member's scene request travels to the server "
    "over the transport and the server's answer travels back and settles the "
    "promise, so the verb is proved where it is delivered rather than where "
    "it is recorded"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][scene] Peer * asked for a scene change before it "
        "joined, so it was refused.",
        1
    );

    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(9);
    mesh.wire(1, 9);
    mesh.pump(4);

    const Ref<NetwPromise> asked = mesh.session_of(9)->scene_request_send(
        String("res://arena.tscn"),
        NetwSceneCore::SCOPE_SESSION,
        RID()
    );
    REQUIRE(asked.is_valid());
    NETW_CHECK_EQ(
        mesh.session_of(9)->get_scene_core()->get_pending_request_destination(),
        int64_t(1)
    );
    CHECK_FALSE(asked->get_is_settled());

    mesh.pump(4);

    CHECK(asked->get_is_settled());
    NETW_CHECK_EQ(asked->get_code(), int64_t(ERR_UNAUTHORIZED));
}

TEST_CASE(
    "[Networked][Session] L8 a member's join crosses the transport to the "
    "server, seats a player there, and the accept and roster the server "
    "sends back seat the same membership at the member"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(9);
    mesh.wire(1, 9);
    mesh.pump(4);

    const Ref<NetwPlayer> host_seat = mesh.join(1, StringName("one"));
    const Ref<NetwPlayer> member_seat = mesh.join(9, StringName("nine"));

    REQUIRE(host_seat.is_valid());
    REQUIRE(member_seat.is_valid());
    NETW_CHECK_EQ(int(host_seat->get_peer_id()), 1);
    NETW_CHECK_EQ(int(member_seat->get_peer_id()), 9);

    NETW_CHECK_EQ(int(mesh.session_of(1)->get_connected_players().size()), 2);
    NETW_CHECK_EQ(int(mesh.session_of(9)->get_connected_players().size()), 2);

    const Ref<NetwPlayer> local_at_member = mesh.session_of(9)->player_local();
    REQUIRE(local_at_member.is_valid());
    NETW_CHECK_EQ(int(local_at_member->get_peer_id()), 9);
    CHECK(mesh.session_of(9)->player_of(1).is_valid());
}

TEST_CASE(
    "[Networked][Session] L9 an accept frame arriving from a peer that is not "
    "the server seats nobody, so member 7 cannot write the roster of the "
    "session"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(7);
    mesh.seat_member(9);
    mesh.wire(1, 7);
    mesh.wire(1, 9);
    mesh.wire(7, 9);
    mesh.pump(4);

    AcceptFrame forged;
    forged.peer_id = 11;
    forged.username = StringName("eleven");
    forged.player_id = 11;
    const PackedByteArray frame = netw::session::frame_write(forged);

    mesh.session_of(9)->session_receive_accept(frame, 7);
    NETW_CHECK_EQ(int(mesh.session_of(9)->player_of(11).is_valid()), 0);

    mesh.session_of(9)->session_receive_accept(frame, 1);
    NETW_CHECK_EQ(int(mesh.session_of(9)->player_of(11).is_valid()), 1);
}

TEST_CASE(
    "[Networked][Session] L11 a peer seated after the others have joined "
    "learns them from the roster the server sends it, so the seats it missed "
    "are the ones it reads"
) {
    MeshStand mesh;
    mesh.seat_server();
    mesh.seat_member(9);
    mesh.wire(1, 9);
    mesh.pump(4);

    REQUIRE(mesh.join(1, StringName("one")).is_valid());
    REQUIRE(mesh.join(9, StringName("nine")).is_valid());

    mesh.seat_member(11);
    mesh.wire(1, 11);
    mesh.pump(4);

    REQUIRE(mesh.join(11, StringName("eleven")).is_valid());

    NETW_CHECK_EQ(int(mesh.session_of(11)->get_connected_players().size()), 3);
    CHECK(mesh.session_of(11)->player_of(1).is_valid());
    CHECK(mesh.session_of(11)->player_of(9).is_valid());
}

#endif

} // namespace TestNetwMeshStand

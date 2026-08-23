#pragma once

#include "support/netw_test.h"

#include "core/object/callable_mp.h"
#include "godot/packed_scene.hpp"
#include "godot/utility.hpp"
#include "modules/multiplayer/scene_multiplayer.h"
#include "netw/carrier_frame.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/scene_core.hpp"
#include "netw/api/loopback.hpp"
#include "netw/wire/frame.hpp"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

using namespace godot;

namespace TestNetworkedSession {

using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;
using netw::NetwCarrierFrame;
using netw::NetwEntity;
using netw::NetwMultiplayerCore;
using netw::NetwPromise;
using netw::NetwSceneCore;
using netw::wire::FrameWalk;

struct SessionSide {
    Ref<LocalMultiplayerPeer> peer;
    Ref<SceneMultiplayer> inner;
    Ref<NetwMultiplayerCore> core;
};

struct NativeSessionPair {
    Ref<LocalLoopbackSession> session;
    SessionSide server;
    SessionSide client;
    int client_id = 0;
};

SessionSide make_side(const Ref<LocalMultiplayerPeer> &p_peer) {
    SessionSide side;
    side.peer = p_peer;
    side.inner.instantiate();
    side.inner->set_root_path(NodePath("/"));
    side.inner->set_multiplayer_peer(p_peer);
    side.core.instantiate();
    side.core->set_inner(side.inner);
    side.core->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
    return side;
}

NativeSessionPair make_native_pair() {
    NativeSessionPair pair;
    pair.session.instantiate();
    pair.server = make_side(pair.session->get_server_peer());
    const Ref<LocalMultiplayerPeer> client_peer
        = pair.session->create_client_peer();
    pair.client_id = client_peer->get_unique_id();
    pair.client = make_side(client_peer);
    return pair;
}

void adopt_transport_roster(SessionSide &p_side) {
    p_side.core->set_peer_ids(netw::gd::api_peer_ids(p_side.inner));
}

void settle_pair(NativeSessionPair &p_pair) {
    p_pair.server.inner->poll();
    p_pair.client.inner->poll();
    p_pair.server.core->NETW_API_VIRTUAL(poll)();
    p_pair.client.core->NETW_API_VIRTUAL(poll)();
    adopt_transport_roster(p_pair.server);
    adopt_transport_roster(p_pair.client);
}

PackedByteArray two_bytes(uint8_t p_first, uint8_t p_second) {
    PackedByteArray bytes;
    bytes.push_back(p_first);
    bytes.push_back(p_second);
    return bytes;
}

struct InboundSink {
    int64_t sender = 0;
    int64_t count = 0;
    PackedByteArray packet;
};

InboundSink inbound_sink;

void note_inbound_packet(int p_id, const PackedByteArray &p_packet) {
    inbound_sink.sender = p_id;
    inbound_sink.count += 1;
    inbound_sink.packet = p_packet;
}

void arm_inbound_sink(SessionSide &p_side) {
    inbound_sink = InboundSink();
    p_side.inner->connect(
        "peer_packet",
        callable_mp_static(&note_inbound_packet)
    );
}

void disarm_inbound_sink(SessionSide &p_side) {
    p_side.inner->disconnect(
        "peer_packet",
        callable_mp_static(&note_inbound_packet)
    );
}

TEST_CASE(
    "[Networked][Session] Two NetwMultiplayerCores admit each other over the "
    "loopback pair with no GDScript in the picture"
) {
    NativeSessionPair pair = make_native_pair();

    settle_pair(pair);

    NETW_CHECK_EQ(pair.server.core->NETW_API_VIRTUAL(get_unique_id)(), 1);
    NETW_CHECK_EQ(
        pair.client.core->NETW_API_VIRTUAL(get_unique_id)(),
        pair.client_id
    );
    NETW_CHECK_EQ(pair.server.core->is_server(), true);
    NETW_CHECK_EQ(pair.client.core->is_server(), false);

    const PackedInt32Array server_view
        = pair.server.core->NETW_API_VIRTUAL(get_peer_ids)();
    const PackedInt32Array client_view
        = pair.client.core->NETW_API_VIRTUAL(get_peer_ids)();

    NETW_CHECK_EQ(server_view.size(), 1);
    NETW_CHECK_GE(server_view.find(pair.client_id), 0);
    NETW_CHECK_EQ(client_view.size(), 1);
    NETW_CHECK_GE(client_view.find(1), 0);
}

TEST_CASE(
    "[Networked][Session] A core reads its roster from the transport it was "
    "handed rather than from the peer it holds, so an unadopted roster is "
    "empty while the transport already knows both sides"
) {
    NativeSessionPair pair = make_native_pair();

    pair.server.inner->poll();
    pair.client.inner->poll();
    pair.server.core->NETW_API_VIRTUAL(poll)();
    pair.client.core->NETW_API_VIRTUAL(poll)();

    NETW_CHECK_EQ(
        pair.server.core->NETW_API_VIRTUAL(get_peer_ids)().size(),
        0
    );
    NETW_CHECK_EQ(netw::gd::api_peer_ids(pair.server.inner).size(), 1);

    adopt_transport_roster(pair.server);

    NETW_CHECK_EQ(
        pair.server.core->NETW_API_VIRTUAL(get_peer_ids)().size(),
        1
    );
}

TEST_CASE(
    "[Networked][Session] A datagram framed by one core is read back as a "
    "carrier header by the other core"
) {
    NativeSessionPair pair = make_native_pair();
    settle_pair(pair);
    arm_inbound_sink(pair.client);

    const PackedByteArray payload = two_bytes(0xAB, 0xCD);
    pair.server.core->send_datagram(pair.client_id, payload, true);

    pair.client.inner->poll();

    NETW_CHECK_EQ(inbound_sink.count, 1);
    NETW_CHECK_EQ(inbound_sink.sender, 1);

    const Ref<NetwCarrierFrame> header
        = pair.client.core->receive_header(
            inbound_sink.sender,
            inbound_sink.packet
        );
    REQUIRE(header.is_valid());
    NETW_CHECK_EQ(header->kind, int64_t(NetwCarrierFrame::RELIABLE));
    NETW_CHECK_EQ(
        inbound_sink.packet.size() - header->payload_offset,
        payload.size()
    );
    NETW_CHECK_EQ(
        inbound_sink.packet[header->payload_offset],
        payload[0]
    );
    NETW_CHECK_EQ(
        inbound_sink.packet[header->payload_offset + 1],
        payload[1]
    );
    NETW_CHECK_EQ(pair.client.core->get_received_packets(), 1);
    NETW_CHECK_EQ(pair.client.core->get_received_bytes(), payload.size());

    disarm_inbound_sink(pair.client);
}

TEST_CASE(
    "[Networked][Session] Only an unreliable datagram spends a sequence, and "
    "one addressed to a peer the sender's transport does not report is "
    "refused before it spends one"
) {
    NativeSessionPair pair = make_native_pair();
    settle_pair(pair);

    NETW_CHECK_EQ(
        pair.server.core->send_datagram(
            pair.client_id,
            two_bytes(0x01, 0x02),
            true
        ),
        -1
    );
    const int64_t spent = pair.server.core->send_datagram(
        pair.client_id,
        two_bytes(0x03, 0x04),
        false
    );
    NETW_CHECK_GE(spent, 0);
    NETW_CHECK_EQ(
        pair.server.core->send_datagram(
            pair.client_id + 41,
            two_bytes(0x05, 0x06),
            false
        ),
        -1
    );
    NETW_CHECK_EQ(
        pair.server.core->send_datagram(
            pair.client_id,
            two_bytes(0x07, 0x08),
            false
        ),
        spent + 1
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] A GDScript-free session pair installs as "
    "two scoped multiplayer APIs under the fixture tree"
) {
    SceneTree *tree = SceneTree::get_singleton();
    REQUIRE(tree != nullptr);

    Node *server_anchor = memnew(Node);
    server_anchor->set_name("NetwSessionServer");
    Node *client_anchor = memnew(Node);
    client_anchor->set_name("NetwSessionClient");
    tree->get_root()->add_child(server_anchor);
    tree->get_root()->add_child(client_anchor);

    NativeSessionPair pair = make_native_pair();
    tree->set_multiplayer(pair.server.core, server_anchor->get_path());
    tree->set_multiplayer(pair.client.core, client_anchor->get_path());

    NETW_CHECK_EQ(
        tree->get_multiplayer(server_anchor->get_path()).ptr()
            == pair.server.core.ptr(),
        true
    );
    NETW_CHECK_EQ(
        tree->get_multiplayer(client_anchor->get_path()).ptr()
            == pair.client.core.ptr(),
        true
    );

    settle_pair(pair);
    arm_inbound_sink(pair.client);

    NETW_CHECK_EQ(pair.server.core->NETW_API_VIRTUAL(get_unique_id)(), 1);
    NETW_CHECK_EQ(
        pair.client.core->NETW_API_VIRTUAL(get_unique_id)(),
        pair.client_id
    );
    NETW_CHECK_GE(
        pair.server.core->NETW_API_VIRTUAL(get_peer_ids)()
            .find(pair.client_id),
        0
    );

    const PackedByteArray payload = two_bytes(0x10, 0x20);
    pair.server.core->send_datagram(pair.client_id, payload, true);
    pair.client.inner->poll();
    NETW_CHECK_EQ(inbound_sink.count, 1);
    disarm_inbound_sink(pair.client);

    tree->set_multiplayer(Ref<MultiplayerAPI>(), server_anchor->get_path());
    tree->set_multiplayer(Ref<MultiplayerAPI>(), client_anchor->get_path());
    tree->get_root()->remove_child(server_anchor);
    tree->get_root()->remove_child(client_anchor);
    memdelete(server_anchor);
    memdelete(client_anchor);
}

Variant admit_scene_request(
    const Variant &p_participant,
    const Variant &p_destination,
    const Array &p_args
) {
    return int64_t(OK);
}

FrameWalk walk_inbound(SessionSide &p_side) {
    const Ref<NetwCarrierFrame> header
        = p_side.core->receive_header(
            inbound_sink.sender,
            inbound_sink.packet
        );
    if (header.is_null()) {
        return FrameWalk();
    }
    return netw::wire::frame_unpack_all(
        inbound_sink.packet,
        header->payload_offset
    );
}

TEST_CASE(
    "[Networked][Session] A scene request crosses the pair as a framed "
    "datagram and settles the asking side's pending request, with no "
    "GDScript on either end"
) {
    NativeSessionPair pair = make_native_pair();
    settle_pair(pair);
    arm_inbound_sink(pair.server);

    const Ref<NetwSceneCore> client_scene
        = pair.client.core->get_scene_core();
    const Ref<NetwSceneCore> server_scene
        = pair.server.core->get_scene_core();
    REQUIRE(client_scene.is_valid());
    REQUIRE(server_scene.is_valid());

    const String destination = String("res://moved.tscn");
    const Ref<NetwPromise> asked
        = pair.client.core->scene_request(true, destination, Array(), false);
    REQUIRE(asked.is_valid());

    const int request_id = client_scene->get_pending_request_id();
    NETW_CHECK_GE(request_id, 1);
    NETW_CHECK_EQ(client_scene->is_current(request_id), true);
    NETW_CHECK_EQ(asked->get_is_settled(), false);

    pair.server.inner->poll();

    NETW_CHECK_EQ(inbound_sink.count, 1);
    NETW_CHECK_EQ(inbound_sink.sender, pair.client_id);

    const FrameWalk asked_walk = walk_inbound(pair.server);
    NETW_CHECK_EQ(asked_walk.whole, true);
    REQUIRE(asked_walk.frames.size() == 1);
    NETW_CHECK_EQ(asked_walk.frames[0].route, 0);
    NETW_CHECK_EQ(asked_walk.frames[0].comp, 0);
    NETW_CHECK_EQ(asked_walk.frames[0].path.is_empty(), true);

    const Variant decoded
        = netw::gd::bytes_to_var(asked_walk.frames[0].payload);
    REQUIRE(decoded.get_type() == Variant::ARRAY);
    const Array row = decoded;
    REQUIRE(row.size() == 4);
    NETW_CHECK_EQ(int(row[0]), request_id);
    NETW_CHECK_EQ(bool(row[1]), true);
    NETW_CHECK_EQ(String(row[2]) == destination, true);

    server_scene->set_request_handler(
        callable_mp_static(&admit_scene_request)
    );
    const String verified
        = NetwSceneCore::verify_requested_path(String(row[2]));
    NETW_CHECK_EQ(verified == destination, true);
    NETW_CHECK_EQ(
        server_scene->decide_request(
            inbound_sink.sender,
            row[2],
            row[3],
            false,
            false,
            false,
            verified
        ),
        true
    );

    disarm_inbound_sink(pair.server);
    arm_inbound_sink(pair.client);

    Array result;
    result.push_back(int(row[0]));
    result.push_back(int(OK));
    pair.server.core->send_to(
        pair.client_id,
        0,
        0,
        netw::gd::var_to_bytes(result),
        true,
        0,
        String(),
        false
    );

    pair.client.inner->poll();

    NETW_CHECK_EQ(inbound_sink.count, 1);
    NETW_CHECK_EQ(inbound_sink.sender, 1);

    const FrameWalk result_walk = walk_inbound(pair.client);
    NETW_CHECK_EQ(result_walk.whole, true);
    REQUIRE(result_walk.frames.size() == 1);
    NETW_CHECK_EQ(
        client_scene->receive_result_frame(
            result_walk.frames[0].payload,
            int(inbound_sink.sender)
        ),
        true
    );

    NETW_CHECK_EQ(client_scene->get_pending_request_id(), 0);
    NETW_CHECK_EQ(client_scene->is_current(request_id), false);
    NETW_CHECK_EQ(asked->get_is_settled(), true);

    disarm_inbound_sink(pair.client);
}

TEST_CASE(
    "[Networked][Session][SceneTree] A packed in-memory template seats a "
    "native entity with no GDScript and no res:// scene file"
) {
    SceneTree *tree = SceneTree::get_singleton();
    REQUIRE(tree != nullptr);

    Node *fixture_parent = memnew(Node);
    fixture_parent->set_name("NetwSpawnFixture");
    tree->get_root()->add_child(fixture_parent);

    Node *root_template = memnew(Node);
    root_template->set_name("Npc");
    root_template->set("process_priority", 7);
    Node *child_template = memnew(Node);
    child_template->set_name("Marker");
    root_template->add_child(child_template);
    child_template->set_owner(root_template);

    Ref<PackedScene> packed;
    packed.instantiate();
    NETW_CHECK_EQ(packed->pack(root_template), OK);

    {
        Node *copy = packed->instantiate();
        REQUIRE(copy != nullptr);
        NETW_CHECK_EQ(copy->get_name() == StringName("Npc"), true);
        NETW_CHECK_EQ(copy->get_child_count(), 1);
        NETW_CHECK_EQ(int64_t(copy->get("process_priority")), 7);

        NetwMultiplayerCore::wrapper_ensure(copy);
        fixture_parent->add_child(copy);

        const Ref<NetwEntity> entity = NetwEntity::of(copy);
        REQUIRE(entity.is_valid());
        NETW_CHECK_EQ(entity->get_owner(), copy);
        NETW_CHECK_EQ(entity->get_peer_id(), 0);
        NETW_CHECK_EQ(entity->get_entity_id() == StringName(), true);
        NETW_CHECK_EQ(entity->get_is_player(), false);
        NETW_CHECK_EQ(entity->get_is_authority(), true);
        NETW_CHECK_EQ(copy->get_parent(), fixture_parent);

        fixture_parent->remove_child(copy);
        memdelete(copy);
    }

    {
        Node *copy = packed->instantiate();
        REQUIRE(copy != nullptr);

        NetwMultiplayerCore::wrapper_bind(copy, StringName("courier"), 7);
        fixture_parent->add_child(copy);

        NETW_CHECK_EQ(copy->get_name() == StringName("courier|7"), true);

        const Ref<NetwEntity> entity = NetwEntity::of(copy);
        REQUIRE(entity.is_valid());
        NETW_CHECK_EQ(entity->get_entity_id() == StringName("courier"), true);
        NETW_CHECK_EQ(entity->get_peer_id(), 7);
        NETW_CHECK_EQ(entity->get_is_player(), true);
        NETW_CHECK_EQ(
            entity->get_ownership(),
            int64_t(NetwEntity::OWNERSHIP_PEER)
        );
        NETW_CHECK_EQ(copy->get_parent(), fixture_parent);

        fixture_parent->remove_child(copy);
        memdelete(copy);
    }

    tree->get_root()->remove_child(fixture_parent);
    memdelete(fixture_parent);
    memdelete(root_template);
}

} // namespace TestNetworkedSession

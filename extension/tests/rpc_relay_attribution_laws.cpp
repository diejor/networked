#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/script.hpp>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/call_args.hpp"
#include "netw/script/model.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/stream.hpp"
#include "support/declared_nodes.h"
#include "support/mesh_stand.h"
#include "support/minted_script.h"
#include "support/netw_call_log.h"

namespace TestRpcRelayAttributionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
namespace script_model = netw::script::model;

constexpr int MESH_SERVER = 1;
constexpr int MESH_OTHER = 7;
constexpr int MESH_MEMBER = 9;

Node *a_sink() {
    const Ref<Script> script = script_from(gdsrc::CALLER_RPC_SINK);
    REQUIRE(script.is_valid());
    Node *made = Object::cast_to<Node>(script->call("new"));
    REQUIRE(made != nullptr);
    made->set_name("Sink");
    return made;
}

struct Seated {
    Node *hub = nullptr;
    Node *first = nullptr;
    Node *second = nullptr;
    int64_t route = 0;
};

Seated seat_star(LoopbackRig &p_rig) {
    Seated made;
    made.hub = a_sink();
    p_rig.branch(-1)->add_child(made.hub);
    const RID at_hub = p_rig.server()->entity_create();
    made.route = p_rig.server()->entity_admit(at_hub);
    REQUIRE(made.route > 0);
    NETW_CHECK_EQ(
        int(p_rig.server()->entity_bind_node(at_hub, made.hub)),
        int(OK)
    );

    Node *seats[2] = {nullptr, nullptr};
    for (int index = 0; index < 2; ++index) {
        seats[index] = a_sink();
        p_rig.branch(index)->add_child(seats[index]);
        const RID here = p_rig.client(index)->entity_create();
        NETW_CHECK_EQ(
            int(p_rig.client(index)->entity_bind_route(here, made.route)),
            int(OK)
        );
        NETW_CHECK_EQ(
            int(p_rig.client(index)->entity_bind_node(here, seats[index])),
            int(OK)
        );
    }
    made.first = seats[0];
    made.second = seats[1];
    p_rig.pump(4);
    return made;
}

void retire_star(const Seated &p_seated) {
    for (Node *node : {p_seated.hub, p_seated.first, p_seated.second}) {
        if (node != nullptr && node->get_parent() != nullptr) {
            node->get_parent()->remove_child(node);
        }
        memdelete(node);
    }
}

TEST_CASE(
    "[Networked][Rpc] RA1 a call one client addresses to another runs once at "
    "that client under the caller's own id, and never at the transport hub "
    "the packet crossed on its way"
) {
    LoopbackRig rig(2);
    rig.mount();
    const Seated seated = seat_star(rig);
    const int caller = rig.peer_id(0);

    Array args;
    args.push_back(11);
    rig.client(0)->rpc_call(
        Callable(seated.first, StringName("note_call")),
        args,
        rig.peer_id(1)
    );
    rig.pump(6);

    NETW_CHECK_EQ(int(seated.second->get(StringName("calls"))), 1);
    NETW_CHECK_EQ(int(seated.second->get(StringName("last_tag"))), 11);
    NETW_CHECK_EQ(int(seated.second->get(StringName("last_sender"))), caller);
    NETW_CHECK_EQ(int(seated.hub->get(StringName("calls"))), 0);
    NETW_CHECK_EQ(int(seated.first->get(StringName("calls"))), 0);

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Rpc] RA2 a client's broadcast runs exactly once at every "
    "other peer, because the caller addresses each recipient itself and the "
    "hub that carried the packet does not send it on again"
) {
    LoopbackRig rig(2);
    rig.mount();
    const Seated seated = seat_star(rig);
    const int caller = rig.peer_id(0);

    Array args;
    args.push_back(22);
    rig.client(0)
        ->rpc_call(Callable(seated.first, StringName("note_call")), args, 0);
    rig.pump(6);

    NETW_CHECK_EQ(int(seated.second->get(StringName("calls"))), 1);
    NETW_CHECK_EQ(int(seated.second->get(StringName("last_sender"))), caller);
    NETW_CHECK_EQ(int(seated.hub->get(StringName("calls"))), 1);
    NETW_CHECK_EQ(int(seated.hub->get(StringName("last_sender"))), caller);

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Rpc] RA3 a request one client addresses to another settles "
    "with that client's own answer, and the answering peer reads the caller "
    "rather than the hub between them"
) {
    LoopbackRig rig(2);
    rig.mount();
    const Seated seated = seat_star(rig);
    const int caller = rig.peer_id(0);

    Array args;
    args.push_back(21);
    const Ref<NetwPromise> asked = rig.client(0)->rpc_request_call(
        rig.peer_id(1),
        Callable(seated.first, StringName("double_tag")),
        args,
        NetwMultiplayer::rpc_request_timeout_default()
    );
    REQUIRE(asked.is_valid());
    rig.pump(8);

    CHECK(asked->get_is_completed());
    NETW_CHECK_EQ(int(asked->get_result()), 42);
    NETW_CHECK_EQ(int(seated.second->get(StringName("last_sender"))), caller);
    NETW_CHECK_EQ(int(seated.hub->get(StringName("calls"))), 0);

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Rpc] RA4 a call frame naming one peer runs nowhere else, so "
    "a packet that reaches the wrong session leaves no effect behind"
) {
    LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);

    Node *sink = a_sink();
    rig.branch(-1)->add_child(sink);
    const RID seat = core->entity_create();
    const int64_t route = core->entity_admit(seat);
    NETW_CHECK_GT(route, 0);
    NETW_CHECK_EQ(int(core->entity_bind_node(seat, sink)), int(OK));
    const Ref<NetwEntity> wrapper = core->entity_get_view(seat);

    LocalVector<netw::call_args::Slot> encoded;
    encoded.push_back(netw::call_args::of_value(5));
    netw::wire::WriteStream writer;
    uint64_t addresses_a_peer = 2;
    uint64_t elsewhere = 9;
    REQUIRE(writer.bits(addresses_a_peer, 8));
    REQUIRE(writer.bits(elsewhere, 32));
    REQUIRE(
        script_model::write_call_body(
            writer,
            core->rpc_method_token(wrapper, sink, StringName("note_call")),
            encoded,
            Array(),
            Array()
        )
    );
    REQUIRE(writer.align_verify());

    core->receive_carrier(
        NetwMultiplayer::frame_pack(
            route,
            0,
            netw::wire::builtin_channel("CALL"),
            writer.to_bytes(),
            String()
        ),
        2,
        true,
        -1,
        -1
    );

    NETW_CHECK_EQ(int(sink->get(StringName("calls"))), 0);

    sink->get_parent()->remove_child(sink);
    memdelete(sink);
}

Node *a_branch(const char *p_name) {
    Node *branch = memnew(Node);
    branch->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(branch);
    return branch;
}

void drop_branch(Node *p_branch) {
    netw::gd::scene_root()->remove_child(p_branch);
    memdelete(p_branch);
}

TEST_CASE(
    "[Networked][Rpc][SceneTree] RA5 a call over a directly wired mesh runs "
    "at the server under the member's own id, because no hub stands "
    "between them to be mistaken for the caller"
) {
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MESH_MEMBER);
    stand.wire(MESH_SERVER, MESH_MEMBER);
    stand.pump(4);

    NetwMultiplayer *at_server = stand.session_of(MESH_SERVER);
    NetwMultiplayer *at_member = stand.session_of(MESH_MEMBER);
    REQUIRE(at_server != nullptr);
    REQUIRE(at_member != nullptr);

    Node *server_branch = a_branch("RA5Server");
    Node *member_branch = a_branch("RA5Member");
    stand.mount(MESH_SERVER, server_branch);
    stand.mount(MESH_MEMBER, member_branch);

    Node *server_sink = a_sink();
    Node *member_sink = a_sink();
    server_branch->add_child(server_sink);
    member_branch->add_child(member_sink);

    const RID seat = at_server->entity_create();
    const int64_t route = at_server->entity_admit(seat);
    NETW_CHECK_GT(route, 0);
    NETW_CHECK_EQ(
        int(at_server->entity_bind_node(seat, server_sink)),
        int(OK)
    );
    const RID mirror = at_member->entity_create();
    NETW_CHECK_EQ(int(at_member->entity_bind_route(mirror, route)), int(OK));
    NETW_CHECK_EQ(
        int(at_member->entity_bind_node(mirror, member_sink)),
        int(OK)
    );
    stand.pump(4);

    Array args;
    args.push_back(33);
    at_member->rpc_call(
        Callable(member_sink, StringName("note_call")),
        args,
        MESH_SERVER
    );
    stand.pump(6);

    NETW_CHECK_EQ(int(server_sink->get(StringName("calls"))), 1);
    NETW_CHECK_EQ(int(server_sink->get(StringName("last_tag"))), 33);
    NETW_CHECK_EQ(
        int(server_sink->get(StringName("last_sender"))),
        MESH_MEMBER
    );

    drop_branch(member_branch);
    drop_branch(server_branch);
}

TEST_CASE(
    "[Networked][Rpc] RA6 a session shows a filtered entity to the server "
    "alone, so no member buys a view of what the session hides"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    Node *passenger = memnew(Node);
    passenger->set_name("Passenger");
    const RID carried = core->entity_create();
    NETW_CHECK_GT(int(core->entity_admit(carried)), 0);
    NETW_CHECK_EQ(int(core->entity_bind_node(carried, passenger)), int(OK));

    const Ref<NetwEntity> hidden = core->entity_get_view(carried);
    const Ref<netw::NetwInterestLayer> curtain
        = core->interest_layer(StringName("hidden"));
    REQUIRE(curtain.is_valid());
    REQUIRE(hidden.is_valid());
    curtain->add_entity(hidden);

    CHECK(core->interest_wire_admits(MESH_SERVER, hidden));
    CHECK_FALSE(core->interest_wire_admits(MESH_OTHER, hidden));
    CHECK_FALSE(core->interest_wire_admits(MESH_MEMBER, hidden));

    CHECK(core->send_admits(MESH_SERVER, hidden));
    CHECK_FALSE(core->send_admits(MESH_OTHER, hidden));
    CHECK_FALSE(core->send_admits(MESH_MEMBER, hidden));

    memdelete(passenger);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] RA8 the server may emit an unregistered "
    "signal on a node another peer holds, and a peer holding neither may "
    "not, because the permission is the session's own"
) {
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MESH_MEMBER);
    stand.wire(MESH_SERVER, MESH_MEMBER);
    stand.pump(4);

    NetwMultiplayer *at_server = stand.session_of(MESH_SERVER);
    NetwMultiplayer *at_member = stand.session_of(MESH_MEMBER);
    REQUIRE(at_server != nullptr);
    REQUIRE(at_member != nullptr);

    Node *server_branch = a_branch("RA8Server");
    Node *member_branch = a_branch("RA8Member");
    stand.mount(MESH_SERVER, server_branch);
    stand.mount(MESH_MEMBER, member_branch);

    const Ref<Script> plain = script_from(gdsrc::A_PLAIN_SCRIPT);
    REQUIRE(plain.is_valid());
    Node *held = Object::cast_to<Node>(plain->call("new"));
    Node *elsewhere = Object::cast_to<Node>(plain->call("new"));
    REQUIRE(held != nullptr);
    REQUIRE(elsewhere != nullptr);
    held->set_name("Held");
    elsewhere->set_name("Held");
    server_branch->add_child(held);
    member_branch->add_child(elsewhere);
    held->set_multiplayer_authority(11, false);
    elsewhere->set_multiplayer_authority(11, false);

    const RID seat = at_server->entity_create();
    const int64_t route = at_server->entity_admit(seat);
    NETW_CHECK_GT(route, 0);
    NETW_CHECK_EQ(int(at_server->entity_bind_node(seat, held)), int(OK));
    const RID mirror = at_member->entity_create();
    NETW_CHECK_EQ(int(at_member->entity_bind_route(mirror, route)), int(OK));
    NETW_CHECK_EQ(int(at_member->entity_bind_node(mirror, elsewhere)), int(OK));
    stand.pump(4);

    const CallLog seen;
    held->connect(StringName("changed"), seen.callable(StringName("host")));
    elsewhere->connect(
        StringName("changed"),
        seen.callable(StringName("guest"))
    );

    Array args;
    args.push_back(5);
    at_server->sync_send_signal(seat, 0, StringName("changed"), args);
    at_member->sync_send_signal(mirror, 0, StringName("changed"), args);

    NETW_CHECK_EQ(seen.count(StringName("host")), 1);
    NETW_CHECK_EQ(seen.count(StringName("guest")), 0);

    held->set(StringName("value"), 5);
    at_server->sync_send_property(seat, 0, StringName("value"));
    stand.pump(6);
    NETW_CHECK_EQ(int(elsewhere->get(StringName("value"))), 5);

    drop_branch(member_branch);
    drop_branch(server_branch);
}

TEST_CASE(
    "[Networked][Session] RA7 an administrative control frame is honoured "
    "from the server and from nobody else, so a member cannot unpause a "
    "session it does not run"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog seen;
    core->observe(
        NetwMultiplayer::SESSION_TREE_UNPAUSED,
        seen.callable(StringName("unpaused"))
    );

    const int64_t channel = netw::wire::builtin_channel("SESSION_UNPAUSE");
    core->session_publish_control(channel, MESH_OTHER, PackedByteArray());
    NETW_CHECK_EQ(seen.count(StringName("unpaused")), 0);

    core->session_publish_control(channel, MESH_SERVER, PackedByteArray());
    NETW_CHECK_EQ(seen.count(StringName("unpaused")), 1);
}

} // namespace TestRpcRelayAttributionLaws

#endif

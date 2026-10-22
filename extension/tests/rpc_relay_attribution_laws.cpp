#include "support/fault_log.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/script.hpp>

#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/call_args.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][transport] A call for '*' was meant for peer * and "
        "reached peer *, so it was dropped.",
        1
    );

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
    NETW_CHECK_EQ(int(at_server->entity_bind_node(seat, server_sink)), int(OK));
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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][wire] Only the authority can emit '*', because it "
        "is not declared. Declare it with Netw.configure_signal.",
        1
    );

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

    core->session_publish_control(
        netw::wire::builtin_channel("SESSION_PAUSE"),
        MESH_SERVER,
        netw::session::frame_write(netw::session::SessionReason{String()})
    );
    REQUIRE(core->session_is_paused());

    const int64_t channel = netw::wire::builtin_channel("SESSION_UNPAUSE");
    core->session_publish_control(channel, MESH_OTHER, PackedByteArray());
    NETW_CHECK_EQ(seen.count(StringName("unpaused")), 0);

    core->session_publish_control(channel, MESH_SERVER, PackedByteArray());
    NETW_CHECK_EQ(seen.count(StringName("unpaused")), 1);
}

constexpr const char *ANSWERING_SINK = R"(extends Node


@rpc("authority", "call_remote", "reliable")
func guarded(tag: int) -> int:
	return tag


@rpc("any_peer", "call_remote", "reliable")
func echo(tag: int) -> int:
	return tag * 2


@rpc("any_peer", "call_remote", "reliable")
func closed(_tag: int) -> NetwPromise:
	return NetwPromise.rejected(ERR_BUSY, "the shop is closed")
)";

constexpr const char *UNDECLARED_SINK = R"(extends Node


func guarded(tag: int) -> int:
	return tag


func echo(tag: int) -> int:
	return tag * 2


func closed(_tag: int) -> NetwPromise:
	return NetwPromise.rejected(ERR_BUSY, "the shop is closed")
)";

struct Refusal {
    const char *label;
    const char *hub;
    const char *second;
    int caller;
    const char *method;
    Error code;
    const char *detail;
    const char *receiver_warning;
};

const Refusal REFUSALS[] = {
    {"RR1 a sender the method refuses",
     ANSWERING_SINK,
     ANSWERING_SINK,
     0,
     "guarded",
     ERR_UNAUTHORIZED,
     nullptr,
     "[warn][transport] Peer * may not call 'guarded' on 'Sink', so the call "
     "was dropped."},
    {"RR2 a method that is not an RPC on the receiver",
     UNDECLARED_SINK,
     ANSWERING_SINK,
     0,
     "echo",
     ERR_METHOD_NOT_FOUND,
     nullptr,
     "[warn][transport] A call on 'Sink' was dropped, because it names no RPC "
     "on this peer. Declare it the same way on every peer."},
    {"RR3 a method whose promise rejects",
     ANSWERING_SINK,
     ANSWERING_SINK,
     0,
     "closed",
     ERR_BUSY,
     "the shop is closed",
     nullptr},
    {"RR4 a group with one peer that refuses",
     ANSWERING_SINK,
     UNDECLARED_SINK,
     -1,
     "echo",
     ERR_METHOD_NOT_FOUND,
     nullptr,
     "[warn][transport] A call on 'Sink' was dropped, because it names no RPC "
     "on this peer. Declare it the same way on every peer."},
};

Node *sink_of(const char *p_source) {
    const Ref<Script> script = script_from(p_source);
    REQUIRE(script.is_valid());
    Node *made = Object::cast_to<Node>(script->call("new"));
    REQUIRE(made != nullptr);
    made->set_name("Sink");
    return made;
}

Seated seat_sources(
    LoopbackRig &p_rig,
    const char *p_hub,
    const char *p_first,
    const char *p_second
) {
    Seated made;
    made.hub = sink_of(p_hub);
    p_rig.branch(-1)->add_child(made.hub);
    NetwMultiplayer *server = p_rig.server();
    const RID at_hub = server->entity_create();
    made.route = server->entity_admit(at_hub);
    REQUIRE(made.route > 0);
    REQUIRE(server->entity_bind_node(at_hub, made.hub) == OK);
    Node *seats[2] = {sink_of(p_first), sink_of(p_second)};
    for (int index = 0; index < 2; ++index) {
        p_rig.branch(index)->add_child(seats[index]);
        NetwMultiplayer *client = p_rig.client(index);
        const RID here = client->entity_create();
        REQUIRE(client->entity_bind_route(here, made.route) == OK);
        REQUIRE(client->entity_bind_node(here, seats[index]) == OK);
    }
    made.first = seats[0];
    made.second = seats[1];
    p_rig.pump(4);
    return made;
}

TEST_CASE(
    "[Networked][Rpc] RR1 to RR4 a request the receiver does not run is "
    "answered with the reason, never left to time out"
) {
    for (const Refusal &row : REFUSALS) {
        NETW_FORMAT_TEXT(netw_cell_text, row.label);
        CAPTURE(netw_cell_text);
        netw_test::FaultWatch faults;
        if (row.receiver_warning != nullptr) {
            faults.expect_warning(row.receiver_warning, 1);
        }
        LoopbackRig rig(2);
        rig.mount();
        const Seated seated
            = seat_sources(rig, row.hub, ANSWERING_SINK, row.second);
        Array args;
        args.push_back(3);
        Node *caller = row.caller < 0 ? seated.hub : seated.first;
        const Callable method(caller, StringName(row.method));
        const Ref<NetwPromise> single = row.caller < 0
            ? Ref<NetwPromise>()
            : netw::Netw::request(method, args);
        const Ref<netw::NetwGroupPromise> group = row.caller < 0
            ? netw::Netw::request_all(method, args)
            : Ref<netw::NetwGroupPromise>();
        const CallLog caught;
        if (group.is_valid()) {
            group->catch_error(caught.callable(StringName("caught")));
        }
        rig.pump(6);

        if (single.is_valid()) {
            CHECK(single->get_is_failed());
            NETW_CHECK_EQ(int(single->get_code()), int(row.code));
            if (row.detail != nullptr) {
                CHECK(single->get_detail() == String(row.detail));
            }
        } else {
            REQUIRE(group.is_valid());
            CHECK(group->get_is_failed());
            NETW_CHECK_EQ(caught.count(StringName("caught")), 1);
            NETW_CHECK_EQ(int(group->get_code()), int(row.code));
            const Dictionary results = group->get_results();
            NETW_CHECK_EQ(results.size(), 1);
            NETW_CHECK_EQ(int(results.get(rig.peer_id(0), 0)), 6);
        }
        retire_star(seated);
    }
}

constexpr const char *DEADLINE_SINK = R"(extends Node

var held: NetwPromise
var hp := 0


func _init() -> void:
	Netw.configure_rpc(patient).timeout(60.0)


@rpc("any_peer", "call_remote", "reliable")
func patient(_tag: int) -> NetwPromise:
	held = NetwPromise.new()
	return held


@rpc("any_peer", "call_remote", "reliable")
func hasty(_tag: int) -> NetwPromise:
	held = NetwPromise.new()
	return held


@rpc("authority", "call_local", "reliable")
func everyone(tag: int) -> int:
	return tag * 2


@rpc("authority", "call_remote", "reliable")
func others(tag: int) -> int:
	return tag * 2
)";

struct Deadline {
    const char *label;
    const char *method;
    int pumps;
    bool times_out;
};

const Deadline DEADLINES[] = {
    {"RT1 timeout(60) on the method, a reply at 10 seconds",
     "patient",
     300,
     false},
    {"RT2 no timeout declared, still waiting at 4.5 seconds",
     "hasty",
     135,
     false},
    {"RT2 no timeout declared, ERR_TIMEOUT at 5.5 seconds",
     "hasty",
     165,
     true},
};

TEST_CASE(
    "[Networked][Rpc] RT1 and RT2 a request waits as long as its method "
    "declares with NetwMemberConfig.timeout, and 5 seconds otherwise"
) {
    for (const Deadline &row : DEADLINES) {
        NETW_FORMAT_TEXT(netw_cell_text, row.label);
        CAPTURE(netw_cell_text);
        LoopbackRig rig(2);
        rig.mount();
        const Seated seated
            = seat_sources(rig, DEADLINE_SINK, DEADLINE_SINK, DEADLINE_SINK);
        Array args;
        args.push_back(3);
        const Ref<NetwPromise> asked = netw::Netw::request(
            Callable(seated.first, StringName(row.method)),
            args
        );
        rig.pump(row.pumps);
        if (row.times_out) {
            CHECK(asked->get_is_failed());
            NETW_CHECK_EQ(int(asked->get_code()), int(ERR_TIMEOUT));
        } else {
            CHECK_FALSE(asked->get_is_settled());
            const Ref<NetwPromise> held = seated.hub->get("held");
            REQUIRE(held.is_valid());
            held->resolve(9);
            rig.pump(4);
            CHECK(asked->get_is_completed());
            NETW_CHECK_EQ(int(asked->get_result()), 9);
        }
        retire_star(seated);
    }
}

TEST_CASE(
    "[Networked][Rpc] RT3 NetwMemberConfig.timeout on a property warns that "
    "it does nothing, and a timeout of zero or less is refused"
) {
    netw_test::FaultWatch faults;
    faults.expect_warning(
        "[warn][session] NetwMemberConfig.timeout does nothing on 'hp', "
        "because only a requested method waits for a reply. Remove the call.",
        1
    );
    faults.expect_error(
        "[error][session] NetwMemberConfig.timeout got * seconds for 'hasty'. "
        "Pass a number of seconds above zero.",
        1
    );
    Node *sink = sink_of(DEADLINE_SINK);
    netw::Netw::configure_property(sink, StringName("hp"))->timeout(2.0);
    netw::Netw::configure_rpc(Callable(sink, StringName("hasty")))
        ->timeout(-1.0);
    memdelete(sink);
}

struct Asked {
    const char *label;
    const char *method;
    bool asks_caller;
};

const Asked ASKED[] = {
    {"RT4 a call_local method", "everyone", true},
    {"RT5 a method without call_local", "others", false},
};

TEST_CASE(
    "[Networked][Rpc] RT4 and RT5 request_all asks the caller too only when "
    "the method is call_local, and its value joins under its own peer id"
) {
    for (const Asked &row : ASKED) {
        NETW_FORMAT_TEXT(netw_cell_text, row.label);
        CAPTURE(netw_cell_text);
        LoopbackRig rig(2);
        rig.mount();
        const Seated seated
            = seat_sources(rig, DEADLINE_SINK, DEADLINE_SINK, DEADLINE_SINK);
        Array args;
        args.push_back(3);
        const Ref<netw::NetwGroupPromise> group = netw::Netw::request_all(
            Callable(seated.hub, StringName(row.method)),
            args
        );
        rig.pump(6);
        REQUIRE(group->get_is_completed());
        const Dictionary results = group->get_results();
        NETW_CHECK_EQ(results.size(), row.asks_caller ? 3 : 2);
        NETW_CHECK_EQ(int(results.has(1)), int(row.asks_caller));
        for (int at = 0; at < 2; ++at) {
            NETW_CHECK_EQ(int(results.get(rig.peer_id(at), 0)), 6);
        }
        if (row.asks_caller) {
            NETW_CHECK_EQ(int(results.get(1, 0)), 6);
        }
        retire_star(seated);
    }
}

} // namespace TestRpcRelayAttributionLaws

#endif

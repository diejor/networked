#include "support/netw_test.h"

#include "support/fault_log.h"
#include "support/loopback_rig.h"
#include "support/netw_call_log.h"
#include "support/stock_stand.h"

#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"

namespace TestNetwRpcCallLaws {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] RC1 a call aimed at a node the session "
    "holds no entity for is charged to the unroutable drop alone, so a "
    "missing entity never reads as a missing route"
) {
    Ref<NetwMultiplayer> session = hosting();
    NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(0));
    NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));

    Node *stray = memnew(Node);
    session->rpc_call(Callable(stray, StringName("get_name")), Array(), 0);

    NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(1));
    NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));

    SUBCASE("and a call with no object at all is the same drop") {
        session->rpc_call(Callable(), Array(), 0);
        NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(2));
        NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));
    }

    SUBCASE("and a reported route drop is charged to the other counter") {
        session->rpc_note_dropped_not_live();
        NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(1));
        NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(1));
    }

    memdelete(stray);
}

TEST_CASE(
    "[Networked][Session][Hosted] RC2 the sender of the frame under dispatch "
    "is the session's own fact, so a handler reached through the carrier "
    "reads it without the session owning a script to hold it"
) {
    Ref<NetwMultiplayer> session = hosting();
    NETW_CHECK_EQ(session->rpc_get_relay_sender(), int64_t(0));

    session->set_relay_sender(7);
    NETW_CHECK_EQ(session->rpc_get_relay_sender(), int64_t(7));

    SUBCASE("and it restores to nothing when the dispatch unwinds") {
        session->set_relay_sender(0);
        NETW_CHECK_EQ(session->rpc_get_relay_sender(), int64_t(0));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] RC3 a component call refuses an entity "
    "this session holds no component node for, and charges no drop for it, "
    "because a call that was never addressed was never sent"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();

    NETW_CHECK_EQ(
        int(session->entity_call(RID(), 0, StringName("go"), Array(), 0)),
        int(ERR_DOES_NOT_EXIST)
    );

    SUBCASE("an entity the record plane knows but nothing owns is the same") {
        const RID handle = session->get_liveness_core()->entity_create();
        NETW_CHECK_EQ(
            int(session->entity_call(handle, 0, StringName("go"), Array(), 0)),
            int(ERR_DOES_NOT_EXIST)
        );
    }

    NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(0));
    NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));
}

#if defined(NETW_TIER_HOSTED)

using netw::Netw;
using netw::NetwEntity;
using netw::NetwGroupPromise;
using netw::NetwPromise;
using netw_test::LoopbackRig;

constexpr const char *CALL_SINK = R"(extends Node

signal rang(tag: int)

var calls := 0
var last_sender := 0
var level := 0


func _init() -> void:
	Netw.configure_signal(rang)


@rpc("any_peer", "call_remote", "reliable")
func note_call(tag: int) -> void:
	calls += 1
	last_sender = multiplayer.get_remote_sender_id()


@rpc("any_peer", "call_remote", "reliable")
func double_tag(tag: int) -> int:
	calls += 1
	last_sender = multiplayer.get_remote_sender_id()
	return tag * 2
)";

Array one_tag() {
    Array args;
    args.push_back(3);
    return args;
}

Callable noted(Node *p_node) {
    return Callable(p_node, StringName("note_call"));
}

Callable doubled(Node *p_node) {
    return Callable(p_node, StringName("double_tag"));
}

NetwMultiplayer *home(Node *p_node) {
    return NetwMultiplayer::of(p_node);
}

Variant via_rpc(Node *p_node) {
    Netw::rpc(noted(p_node), one_tag());
    return Variant();
}

Variant via_rpc_id(Node *p_node) {
    Netw::rpc_id(1, noted(p_node), one_tag());
    return Variant();
}

Variant via_rpc_controller(Node *p_node) {
    Netw::rpc_controller(noted(p_node), one_tag());
    return Variant();
}

Variant via_request(Node *p_node) {
    return Netw::request(doubled(p_node), one_tag());
}

Variant via_request_id(Node *p_node) {
    return Netw::request_id(1, doubled(p_node), one_tag());
}

Variant via_request_all(Node *p_node) {
    return Netw::request_all(doubled(p_node), one_tag());
}

Variant via_request_controller(Node *p_node) {
    return Netw::request_controller(doubled(p_node), one_tag());
}

Variant via_rpc_call(Node *p_node) {
    home(p_node)->rpc_call(noted(p_node), one_tag(), 0);
    return Variant();
}

Variant via_rpc_request_call(Node *p_node) {
    return home(p_node)->rpc_request_call(1, doubled(p_node), one_tag(), 5.0);
}

Variant via_rpc_request_call_group(Node *p_node) {
    return home(p_node)
        ->rpc_request_call_group(doubled(p_node), one_tag(), 5.0);
}

Variant via_entity_call(Node *p_node) {
    NetwMultiplayer *session = home(p_node);
    return int64_t(session->entity_call(
        session->entity_of(p_node),
        0,
        StringName("note_call"),
        one_tag(),
        0
    ));
}

Variant via_sync_property(Node *p_node) {
    Netw::sync_property(p_node, StringName("level"));
    return Variant();
}

Variant via_emit_entity_signal(Node *p_node) {
    Netw::emit_entity_signal(Signal(p_node, StringName("rang")), one_tag());
    return Variant();
}

struct Door {
    const char *label;
    const char *member;
    bool names_an_entity;
    Variant (*call)(Node *p_node);
};

const Door DOORS[] = {
    {"Netw.rpc", "note_call", false, &via_rpc},
    {"Netw.rpc_id", "note_call", false, &via_rpc_id},
    {"Netw.rpc_controller", "note_call", false, &via_rpc_controller},
    {"Netw.request", "double_tag", false, &via_request},
    {"Netw.request_id", "double_tag", false, &via_request_id},
    {"Netw.request_all", "double_tag", false, &via_request_all},
    {"Netw.request_controller", "double_tag", false, &via_request_controller},
    {"rpc_call", "note_call", false, &via_rpc_call},
    {"rpc_request_call", "double_tag", false, &via_rpc_request_call},
    {"rpc_request_call_group",
     "double_tag",
     false,
     &via_rpc_request_call_group},
    {"entity_call", "note_call", true, &via_entity_call},
    {"Netw.sync_property", "level", false, &via_sync_property},
    {"Netw.emit_entity_signal", "rang", false, &via_emit_entity_signal},
};

struct Fault {
    const char *label;
    int client;
    bool configured;
    Error code;
    bool error;
    const char *text;
};

const Fault FAULTS[] = {
    {"no-entity",
     -1,
     false,
     ERR_UNCONFIGURED,
     true,
     "[error][session] '%s' is not part of a NetwEntity, so the call to '%s' "
     "was dropped. Call Netw.configure_entity on its root."},
    {"not-live",
     0,
     true,
     ERR_UNAVAILABLE,
     false,
     "[warn][transport] '%s' is not spawned on the network yet, so the call "
     "to '%s' was dropped. Call it from _ready or later."},
};

struct Answer {
    bool answered = false;
    bool failed = false;
    int64_t code = OK;
};

Answer answer_of(const Variant &p_answer) {
    Answer out;
    if (p_answer.get_type() == Variant::INT) {
        out.answered = true;
        out.code = int64_t(p_answer);
        out.failed = out.code != OK;
        return out;
    }
    Object *held = p_answer;
    NetwPromise *single = Object::cast_to<NetwPromise>(held);
    NetwGroupPromise *group = Object::cast_to<NetwGroupPromise>(held);
    if (single != nullptr) {
        out.answered = true;
        out.failed = single->get_is_failed();
        out.code = single->get_code();
    } else if (group != nullptr) {
        out.answered = true;
        out.failed = group->get_is_failed();
        out.code = group->get_code();
    }
    return out;
}

struct Cell {
    int64_t reported = 0;
    Answer first;
    Answer second;
    int64_t heard = 0;
};

Cell knock(LoopbackRig &p_rig, const Fault &p_fault, const Door &p_door) {
    Cell cell;
    Node *node = netw_test::scripted_root(
        CALL_SINK,
        String(p_door.label).replace(".", "_")
    );
    if (p_fault.configured) {
        NetwEntity::ensure(node);
    }
    p_rig.branch(p_fault.client)->add_child(node);
    netw_test::CallLog heard;
    node->connect(StringName("rang"), heard.callable("rang"));
    const String expected
        = vformat(p_fault.text, node->get_name(), p_door.member);
    {
        netw_test::FaultWatch faults;
        if (p_fault.error) {
            faults.expect_error(expected, 1);
        } else {
            faults.expect_warning(expected, 1);
        }
        cell.first = answer_of(p_door.call(node));
        cell.second = answer_of(p_door.call(node));
        cell.reported = p_fault.error ? faults.errors_containing(expected)
                                      : faults.warnings_containing(expected);
    }
    cell.heard = heard.count("rang");
    node->get_parent()->remove_child(node);
    memdelete(node);
    return cell;
}

bool answers(const Door &p_door) {
    return String(p_door.label).contains("request")
        || String(p_door.label) == "entity_call";
}

TEST_CASE(
    "[Networked][Rpc][SceneTree] RC4 RC5 RC7 a call no peer can route fails "
    "at the call through every door, reports once per node, and a request "
    "answers a failed promise"
) {
    for (const Fault &fault : FAULTS) {
        LoopbackRig rig(1);
        rig.mount();
        for (const Door &door : DOORS) {
            if (door.names_an_entity && !fault.configured) {
                continue;
            }
            const String label = vformat("%s x %s", fault.label, door.label);
            NETW_FORMAT_TEXT(netw_cell_text, label.utf8().get_data());
            CAPTURE(netw_cell_text);
            const Cell cell = knock(rig, fault, door);
            NETW_CHECK_EQ(cell.reported, int64_t(1));
            if (answers(door)) {
                CHECK(cell.first.answered);
                CHECK(cell.second.answered);
                CHECK(cell.second.failed);
                NETW_CHECK_EQ(cell.first.code, int64_t(fault.code));
            }
            if (String(door.member) == "rang") {
                NETW_CHECK_EQ(cell.heard, int64_t(2));
            }
        }
    }
}

struct Steered {
    Node *hub = nullptr;
    Node *seat = nullptr;
};

Steered seat_pair(LoopbackRig &p_rig) {
    Steered made;
    made.hub = netw_test::scripted_root(CALL_SINK, "Unsteered");
    p_rig.branch(-1)->add_child(made.hub);
    const RID at_hub = p_rig.server()->entity_create();
    const int64_t route = p_rig.server()->entity_admit(at_hub);
    REQUIRE(route > 0);
    REQUIRE(p_rig.server()->entity_bind_node(at_hub, made.hub) == OK);
    made.seat = netw_test::scripted_root(CALL_SINK, "Unsteered");
    p_rig.branch(0)->add_child(made.seat);
    const RID here = p_rig.client(0)->entity_create();
    REQUIRE(p_rig.client(0)->entity_bind_route(here, route) == OK);
    REQUIRE(p_rig.client(0)->entity_bind_node(here, made.seat) == OK);
    p_rig.pump(4);
    return made;
}

TEST_CASE(
    "[Networked][Rpc][SceneTree] RC6 a call to the controller of an entity "
    "no peer steers reaches the server, from a client and on the server "
    "itself"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Steered pair = seat_pair(rig);
    NETW_CHECK_EQ(NetwEntity::of(pair.seat)->get_controller(), int64_t(1));

    Netw::rpc_controller(noted(pair.seat), one_tag());
    const Ref<NetwPromise> asked
        = Netw::request_controller(doubled(pair.seat), one_tag());
    rig.pump(6);
    NETW_CHECK_EQ(int(pair.hub->get(StringName("calls"))), 2);
    NETW_CHECK_EQ(
        int(pair.hub->get(StringName("last_sender"))),
        rig.peer_id(0)
    );
    REQUIRE(asked.is_valid());
    NETW_CHECK_EQ(int(asked->get_result()), 6);

    Netw::rpc_controller(noted(pair.hub), one_tag());
    const Ref<NetwPromise> own
        = Netw::request_controller(doubled(pair.hub), one_tag());
    NETW_CHECK_EQ(int(pair.hub->get(StringName("calls"))), 4);
    REQUIRE(own.is_valid());
    CHECK(own->get_is_completed());
    NETW_CHECK_EQ(int(own->get_result()), 6);
    NETW_CHECK_EQ(int(pair.seat->get(StringName("calls"))), 0);
}

#endif

} // namespace TestNetwRpcCallLaws

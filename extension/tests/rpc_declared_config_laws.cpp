#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/script.hpp>

#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/script/model.hpp"
#include "support/fault_log.h"
#include "support/minted_script.h"

namespace TestRpcDeclaredConfigLaws {

using namespace godot;
using namespace netw_test;

constexpr const char *LOCAL_DECLARED_SINK = R"(extends Node

var calls: int = 0
var last_tag: int = 0

func _init() -> void:
	Netw.configure_rpc(note_call).call_local()

func note_call(tag: int) -> void:
	calls += 1
	last_tag = tag
)";

constexpr const char *CONTROLLER_DECLARED_SINK = R"(extends Node

var calls: int = 0
var last_tag: int = 0

func _init() -> void:
	Netw.configure_rpc(note_call).controller()

func note_call(tag: int) -> void:
	calls += 1
	last_tag = tag
)";

constexpr const char *SERVER_DECLARED_SINK = R"(extends Node

var calls: int = 0
var last_tag: int = 0

func _init() -> void:
	Netw.configure_rpc(note_call).server()

func note_call(tag: int) -> void:
	calls += 1
	last_tag = tag
)";

constexpr const char *REMOTE_SIGNAL_SINK = R"(extends Node

signal changed(next: int)

func _init() -> void:
	Netw.configure_signal(changed).call_remote()
)";

constexpr const char *TRANSFER_DECLARED_SINK = R"(extends Node

var lossy_calls: int = 0
var steady_calls: int = 0

func _init() -> void:
	Netw.configure_rpc(lossy).unreliable()
	Netw.configure_rpc(steady).reliable()

func lossy(tag: int) -> void:
	lossy_calls += tag

@rpc("authority", "call_remote", "unreliable")
func steady(tag: int) -> void:
	steady_calls += tag
)";

struct Seated {
    Node *hub = nullptr;
    Node *first = nullptr;
    Node *second = nullptr;
    RID at_hub;
};

Node *a_sink(const Ref<Script> &p_script) {
    Node *made = Object::cast_to<Node>(p_script->call("new"));
    REQUIRE(made != nullptr);
    made->set_name("Sink");
    return made;
}

Seated seat_star(LoopbackRig &p_rig, const Ref<Script> &p_script) {
    Seated made;
    made.hub = a_sink(p_script);
    p_rig.branch(-1)->add_child(made.hub);
    made.at_hub = p_rig.server()->entity_create();
    const int64_t route = p_rig.server()->entity_admit(made.at_hub);
    REQUIRE(route > 0);
    NETW_CHECK_EQ(
        int(p_rig.server()->entity_bind_node(made.at_hub, made.hub)),
        int(OK)
    );

    Node *seats[2] = {nullptr, nullptr};
    for (int index = 0; index < 2; ++index) {
        seats[index] = a_sink(p_script);
        p_rig.branch(index)->add_child(seats[index]);
        const RID here = p_rig.client(index)->entity_create();
        NETW_CHECK_EQ(
            int(p_rig.client(index)->entity_bind_route(here, route)),
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

int calls_at(Node *p_node) {
    return int(p_node->get(StringName("calls")));
}

TEST_CASE(
    "[Networked][Rpc] RD1 an RPC declared call_local through "
    "Netw.configure_rpc runs on the server that called it and on every "
    "client, and declaring it so is not mistaken for two settings"
) {
    LoopbackRig rig(2);
    rig.mount();
    const FaultWatch faults;
    const Ref<Script> script = script_from(LOCAL_DECLARED_SINK);
    REQUIRE(script.is_valid());
    const Seated seated = seat_star(rig, script);

    Array args;
    args.push_back(5);
    netw::Netw::rpc(Callable(seated.hub, StringName("note_call")), args);
    rig.pump(6);

    NETW_CHECK_EQ(calls_at(seated.hub), 1);
    NETW_CHECK_EQ(calls_at(seated.first), 1);
    NETW_CHECK_EQ(calls_at(seated.second), 1);
    NETW_CHECK_EQ(int(seated.first->get(StringName("last_tag"))), 5);
    NETW_CHECK_EQ(
        faults.warnings_containing(String("declares both")),
        int64_t(0)
    );

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Rpc] RD2 an RPC declared controller through "
    "Netw.configure_rpc runs on the server when the controller calls it, "
    "and a client that does not control the entity is refused"
) {
    LoopbackRig rig(2);
    rig.mount();
    const FaultWatch faults;
    const Ref<Script> script = script_from(CONTROLLER_DECLARED_SINK);
    REQUIRE(script.is_valid());
    const Seated seated = seat_star(rig, script);
    rig.server()->entity_grant_control(seated.at_hub, rig.peer_id(0));
    rig.pump(4);

    Array args;
    args.push_back(7);
    netw::Netw::rpc_id(
        1,
        Callable(seated.first, StringName("note_call")),
        args
    );
    rig.pump(6);

    NETW_CHECK_EQ(calls_at(seated.hub), 1);
    NETW_CHECK_EQ(int(seated.hub->get(StringName("last_tag"))), 7);
    NETW_CHECK_EQ(
        faults.warnings_containing(String("may not call")),
        int64_t(0)
    );

    netw::Netw::rpc_id(
        1,
        Callable(seated.second, StringName("note_call")),
        args
    );
    rig.pump(6);

    NETW_CHECK_EQ(calls_at(seated.hub), 1);
    NETW_CHECK_EQ(
        faults.warnings_containing(String("may not call")),
        int64_t(1)
    );

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Rpc] RD3 an RPC declared server through Netw.configure_rpc "
    "refuses the client that controls the entity and runs everywhere the "
    "server sends it"
) {
    LoopbackRig rig(2);
    rig.mount();
    const FaultWatch faults;
    const Ref<Script> script = script_from(SERVER_DECLARED_SINK);
    REQUIRE(script.is_valid());
    const Seated seated = seat_star(rig, script);
    rig.server()->entity_grant_control(seated.at_hub, rig.peer_id(0));
    rig.pump(4);

    Array args;
    args.push_back(9);
    netw::Netw::rpc_id(
        1,
        Callable(seated.first, StringName("note_call")),
        args
    );
    rig.pump(6);

    NETW_CHECK_EQ(calls_at(seated.hub), 0);
    NETW_CHECK_EQ(
        faults.warnings_containing(String("may not call")),
        int64_t(1)
    );

    netw::Netw::rpc(Callable(seated.hub, StringName("note_call")), args);
    rig.pump(6);

    NETW_CHECK_EQ(calls_at(seated.first), 1);
    NETW_CHECK_EQ(calls_at(seated.second), 1);
    NETW_CHECK_EQ(int(seated.second->get(StringName("last_tag"))), 9);

    const Ref<netw::NetwMemberConfig> declared
        = netw::script::model::get_rpc_options(script, StringName("note_call"));
    REQUIRE(declared.is_valid());
    CHECK(declared->is_policy_declared());
    NETW_CHECK_EQ(int(declared->get_write_policy()), 3);

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Rpc] RD4 an RPC declared unreliable through "
    "Netw.configure_rpc is lost with the packet that carried it, and one "
    "declared reliable arrives over a link that loses every packet once"
) {
    LoopbackRig rig(2);
    rig.mount();
    const Ref<Script> script = script_from(TRANSFER_DECLARED_SINK);
    REQUIRE(script.is_valid());
    const Seated seated = seat_star(rig, script);

    const Ref<netw::LocalLinkConditions> lossy
        = netw::LocalLinkConditions::create(10);
    lossy->set_packet_loss(1.0);
    rig.conditions(0, lossy);

    Array args;
    args.push_back(1);
    netw::Netw::rpc(Callable(seated.hub, StringName("lossy")), args);
    netw::Netw::rpc(Callable(seated.hub, StringName("steady")), args);
    rig.pump(12);

    NETW_CHECK_EQ(int(seated.first->get(StringName("lossy_calls"))), 0);
    NETW_CHECK_EQ(int(seated.first->get(StringName("steady_calls"))), 1);
    NETW_CHECK_EQ(int(seated.second->get(StringName("lossy_calls"))), 1);
    NETW_CHECK_EQ(int(seated.second->get(StringName("steady_calls"))), 1);

    retire_star(seated);
}

TEST_CASE(
    "[Networked][Sync] RD5 a signal declared call_remote through "
    "Netw.configure_signal is one setting, so declaring it warns about "
    "nothing and the signal stays off its emitter"
) {
    const FaultWatch faults;
    const Ref<Script> script = script_from(REMOTE_SIGNAL_SINK);
    REQUIRE(script.is_valid());
    Node *emitter = a_sink(script);

    const Ref<netw::NetwMemberConfig> declared
        = netw::script::model::get_signal_config(script, StringName("changed"));
    REQUIRE(declared.is_valid());
    CHECK_FALSE(declared->get_is_call_local());
    NETW_CHECK_EQ(
        faults.warnings_containing(String("declares both")),
        int64_t(0)
    );

    memdelete(emitter);
}

} // namespace TestRpcDeclaredConfigLaws

#endif

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>

namespace TestDeclaredServiceLaws {

using namespace godot;
using namespace netw_test;

Ref<RefCounted> make_resource(const char *p_path) {
    Ref<Script> script = ResourceLoader::get_singleton()->load(p_path);
    REQUIRE(script.is_valid());
    return script->call("new");
}

Node *make_node(const char *p_path) {
    Ref<Script> script = ResourceLoader::get_singleton()->load(p_path);
    REQUIRE(script.is_valid());
    Object *object = script->call("new");
    return Object::cast_to<Node>(object);
}

TEST_CASE(
    "[Networked][Services] node and verb installs produce the same state"
) {
    LoopbackRig rig(1);

    Ref<RefCounted> lag_config = make_resource(
        "res://addons/networked/sync/sim/"
        "netw_lag_compensation_config.gd"
    );
    lag_config->set("max_future_action_ticks", 11);
    lag_config->set("input_gate_deadline_ticks", 17);
    NETW_CHECK_EQ(
        int(rig.server()->call("service_install", lag_config)),
        int(OK)
    );

    Node *lag_node = make_node(
        "res://addons/networked/sync/sim/lag_compensation.gd"
    );
    REQUIRE(lag_node != nullptr);
    lag_node->set("max_future_action_ticks", 11);
    lag_node->set("input_gate_deadline_ticks", 17);
    lag_node->call("_service_entered", rig.client(0));

    Ref<RefCounted> clock_config = make_resource(
        "res://addons/networked/sync/clock/netw_clock_config.gd"
    );
    clock_config->set("tickrate", 47);
    clock_config->set("display_offset", 6);
    NETW_CHECK_EQ(
        int(rig.server()->call("service_install", clock_config)),
        int(OK)
    );

    Node *clock_node = make_node(
        "res://addons/networked/sync/clock/multiplayer_clock.gd"
    );
    REQUIRE(clock_node != nullptr);
    clock_node->set("tickrate", 47);
    clock_node->set("display_offset", 6);
    clock_node->call("_service_entered", rig.client(0));

    Object *server_clock = rig.server()->get("_clock");
    Object *client_clock = rig.client(0)->get("_clock");
    Object *server_lag = rig.server()->get("_lagcomp");
    Object *client_lag = rig.client(0)->get("_lagcomp");
    REQUIRE(server_clock != nullptr);
    REQUIRE(client_clock != nullptr);
    REQUIRE(server_lag != nullptr);
    REQUIRE(client_lag != nullptr);

    NETW_CHECK_EQ(int(server_clock->get("tickrate")), 47);
    NETW_CHECK_EQ(int(client_clock->get("tickrate")), 47);
    NETW_CHECK_EQ(int(server_clock->get("display_offset")), 6);
    NETW_CHECK_EQ(int(client_clock->get("display_offset")), 6);
    NETW_CHECK_EQ(int(server_lag->get("max_future_action_ticks")), 11);
    NETW_CHECK_EQ(int(client_lag->get("max_future_action_ticks")), 11);
    NETW_CHECK_EQ(int(server_lag->get("input_gate_deadline_ticks")), 17);
    NETW_CHECK_EQ(int(client_lag->get("input_gate_deadline_ticks")), 17);
    Object *server_bound_clock = server_lag->get("_clock");
    Object *client_bound_clock = client_lag->get("_clock");
    CHECK(server_bound_clock == server_clock);
    CHECK(client_bound_clock == client_clock);

    const int server_frame = server_lag->get("_physics_frame");
    const int client_frame = client_lag->get("_physics_frame");
    server_clock->call("physics_step", 1.0 / 47.0);
    client_clock->call("physics_step", 1.0 / 47.0);
    NETW_CHECK_EQ(int(server_lag->get("_physics_frame")), server_frame + 1);
    NETW_CHECK_EQ(int(client_lag->get("_physics_frame")), client_frame + 1);

    memdelete(lag_node);
    memdelete(clock_node);
}

TEST_CASE("[Networked][Services] a declared world installs both services") {
    LoopbackRig rig(1);
    WorldDecl world;
    world.clocked(53, 4).lag_compensated();
    rig.declare_world(world);

    Object *server_clock = rig.server()->get("_clock");
    Object *client_clock = rig.client(0)->get("_clock");
    Object *server_lag = rig.server()->get("_lagcomp");
    Object *client_lag = rig.client(0)->get("_lagcomp");
    REQUIRE(server_clock != nullptr);
    REQUIRE(client_clock != nullptr);
    REQUIRE(server_lag != nullptr);
    REQUIRE(client_lag != nullptr);

    NETW_CHECK_EQ(int(server_clock->get("tickrate")), 53);
    NETW_CHECK_EQ(int(client_clock->get("tickrate")), 53);
    Object *server_bound_clock = server_lag->get("_clock");
    Object *client_bound_clock = client_lag->get("_clock");
    CHECK(server_bound_clock == server_clock);
    CHECK(client_bound_clock == client_clock);
}

} // namespace TestDeclaredServiceLaws

#endif

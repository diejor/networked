#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/script.hpp"

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

    const Ref<netw::NetwClockHandle> server_clock = rig.clock_of(rig.server());
    const Ref<netw::NetwClockHandle> client_clock = rig.clock_of(rig.client(0));
    Object *server_lag = rig.server();
    Object *client_lag = rig.client(0);
    REQUIRE(server_clock != nullptr);
    REQUIRE(client_clock != nullptr);
    REQUIRE(server_lag != nullptr);
    REQUIRE(client_lag != nullptr);

    NETW_CHECK_EQ(server_clock->get_tickrate(), 47);
    NETW_CHECK_EQ(client_clock->get_tickrate(), 47);
    NETW_CHECK_EQ(int(server_clock->get("display_offset")), 6);
    NETW_CHECK_EQ(int(client_clock->get("display_offset")), 6);
    NETW_CHECK_EQ(int(server_lag->get("max_future_action_ticks")), 11);
    NETW_CHECK_EQ(int(client_lag->get("max_future_action_ticks")), 11);
    NETW_CHECK_EQ(int(server_lag->get("input_gate_deadline_ticks")), 17);
    NETW_CHECK_EQ(int(client_lag->get("input_gate_deadline_ticks")), 17);

    const int server_frame = server_lag->get("_physics_frame");
    const int client_frame = client_lag->get("_physics_frame");
    server_clock->call("physics_step", 1.0 / 47.0);
    client_clock->call("physics_step", 1.0 / 47.0);
    NETW_CHECK_EQ(int(server_lag->get("_physics_frame")), server_frame + 1);
    NETW_CHECK_EQ(int(client_lag->get("_physics_frame")), client_frame + 1);

    memdelete(lag_node);
    memdelete(clock_node);
}

TEST_CASE(
    "[Networked][Services] a live session registers every peer-scoped channel"
) {
    LoopbackRig rig(1);

    Object *server_repl = rig.server()->get("_replication");
    REQUIRE(server_repl != nullptr);
    NETW_CHECK_EQ(int(server_repl->call("settle_channels")), int(OK));

    Object *client_repl = rig.client(0)->get("_replication");
    REQUIRE(client_repl != nullptr);
    NETW_CHECK_EQ(int(client_repl->call("settle_channels")), int(OK));
}

TEST_CASE("[Networked][Services] a config class with no row installs nothing") {
    LoopbackRig rig(1);
    Object *api = rig.server();
    Object *book = api->get("_install_book");
    REQUIRE(book != nullptr);

    Ref<RefCounted> clock_config = make_resource(
        "res://addons/networked/sync/clock/netw_clock_config.gd"
    );
    clock_config->set("tickrate", 41);
    const StringName before = book->call("class_of", clock_config);
    NETW_FORMAT_TEXT(before_text, String(before).utf8().get_data());
    CAPTURE(before_text);
    CHECK(before == StringName("NetwClockConfig"));

    CHECK(bool(book->call("unregister_service", StringName("NetwClockConfig"))));
    const StringName after = book->call("class_of", clock_config);
    NETW_FORMAT_TEXT(after_text, String(after).utf8().get_data());
    CAPTURE(after_text);
    CHECK(after == StringName());

    NETW_CHECK_EQ(
        int(api->call("service_install", clock_config)),
        int(ERR_INVALID_PARAMETER)
    );

    const Ref<netw::NetwClockHandle> clock = rig.clock_of(api);
    CHECK_FALSE(clock->get_is_configured());
    NETW_CHECK_EQ(int(bool(clock->get_tickrate() == 41)), 0);
}

TEST_CASE("[Networked][Services] a declared world installs both services") {
    LoopbackRig rig(1);
    WorldDecl world;
    world.clocked(53, 4).lag_compensated();
    rig.declare_world(world);

    const Ref<netw::NetwClockHandle> server_clock = rig.clock_of(rig.server());
    const Ref<netw::NetwClockHandle> client_clock = rig.clock_of(rig.client(0));

    NETW_CHECK_EQ(server_clock->get_tickrate(), 53);
    NETW_CHECK_EQ(client_clock->get_tickrate(), 53);
    CHECK(bool(rig.server()->call("is_configured")));
    CHECK(bool(rig.client(0)->call("is_configured")));
}

} // namespace TestDeclaredServiceLaws

#endif

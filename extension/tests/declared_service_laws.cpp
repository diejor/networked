#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/replication_core.hpp"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "netw/predict/engine.hpp"

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

int simulated_frame(netw::NetwMultiplayer *p_session) {
    REQUIRE(p_session != nullptr);
    return int(p_session->frame_timing().get_frame());
}

TEST_CASE(
    "[Networked][Services] a clock node and a config verb install the same "
    "state, so a game that mounts the node and a game that hands the session "
    "a config are running the same session"
) {
    LoopbackRig rig(1);

    NETW_CHECK_EQ(int(rig.server()->lagcomp_initialize(11, 17)), int(OK));
    NETW_CHECK_EQ(int(rig.client(0)->lagcomp_initialize(11, 17)), int(OK));

    Ref<netw::NetwClockConfig> clock_config;
    clock_config.instantiate();
    clock_config->set("tickrate", 47);
    clock_config->set("display_offset", 6);
    NETW_CHECK_EQ(int(rig.server()->clock_initialize(clock_config)), int(OK));

    Ref<netw::NetwClockConfig> client_clock_config;
    client_clock_config.instantiate();
    client_clock_config->set("tickrate", 47);
    client_clock_config->set("display_offset", 6);
    NETW_CHECK_EQ(
        int(rig.client(0)->clock_initialize(client_clock_config)),
        int(OK)
    );

    netw::ClockEngine &server_clock = rig.clock_of(rig.server());
    netw::ClockEngine &client_clock = rig.clock_of(rig.client(0));
    netw::NetwMultiplayer *server_lag = rig.server();
    netw::NetwMultiplayer *client_lag = rig.client(0);
    REQUIRE(server_lag != nullptr);
    REQUIRE(client_lag != nullptr);

    NETW_CHECK_EQ(server_clock.get_tickrate(), 47);
    NETW_CHECK_EQ(client_clock.get_tickrate(), 47);
    NETW_CHECK_EQ(server_clock.get_display_offset(), 6);
    NETW_CHECK_EQ(client_clock.get_display_offset(), 6);
    NETW_CHECK_EQ(int(server_lag->get_max_future_action_ticks()), 11);
    NETW_CHECK_EQ(int(client_lag->get_max_future_action_ticks()), 11);
    NETW_CHECK_EQ(int(server_lag->get_input_gate_deadline_ticks()), 17);
    NETW_CHECK_EQ(int(client_lag->get_input_gate_deadline_ticks()), 17);

    const int server_frame = simulated_frame(server_lag);
    const int client_frame = simulated_frame(client_lag);
    server_clock.physics_step(1.0 / 47.0);
    client_clock.physics_step(1.0 / 47.0);
    NETW_CHECK_EQ(simulated_frame(server_lag), server_frame + 1);
    NETW_CHECK_EQ(simulated_frame(client_lag), client_frame + 1);
}

TEST_CASE(
    "[Networked][Services] a live session registers every peer-scoped channel"
) {
    LoopbackRig rig(1);

    netw::ReplicationCore *server_repl = rig.server()->get_replication_plane();
    REQUIRE(server_repl != nullptr);
    NETW_CHECK_EQ(int(server_repl->settle_channels()), int(OK));

    netw::ReplicationCore *client_repl = rig.client(0)->get_replication_plane();
    REQUIRE(client_repl != nullptr);
    NETW_CHECK_EQ(int(client_repl->settle_channels()), int(OK));
}

TEST_CASE(
    "[Networked][Services] the engine's configuration door refuses a service "
    "config now that every one of them is declared, so a game cannot bypass "
    "one-time consumption through the door it used to install through"
) {
    LoopbackRig rig(1);
    netw::NetwMultiplayer *session = rig.server();
    netw::ClockEngine &clock = rig.clock_of(session);
    CHECK_FALSE(clock.get_configured());
    CHECK_FALSE(session->lagcomp_is_configured());

    Ref<netw::NetwClockConfig> clock_config;
    clock_config.instantiate();
    clock_config->set("tickrate", 31);
    Ref<netw::NetwSessionConfig> session_config;
    session_config.instantiate();
    Ref<netw::NetwLagCompensationConfig> lagcomp_config;
    lagcomp_config.instantiate();

    const Ref<RefCounted> declared_only[] = {
        clock_config,
        session_config,
        lagcomp_config,
    };

    Node *plain = memnew(Node);
    for (const Ref<RefCounted> &config : declared_only) {
        NETW_FORMAT_TEXT(
            netw_refused_class,
            config->get_class().utf8().get_data()
        );
        CAPTURE(netw_refused_class);
        NETW_CHECK_EQ(
            int(session->NETW_API_VIRTUAL(object_configuration_add)(
                plain,
                config
            )),
            int(ERR_UNAVAILABLE)
        );
        NETW_CHECK_EQ(
            int(session->NETW_API_VIRTUAL(object_configuration_remove)(
                plain,
                config
            )),
            int(ERR_UNAVAILABLE)
        );
    }
    CHECK_FALSE(clock.get_configured());
    CHECK_FALSE(session->lagcomp_is_configured());

    memdelete(plain);
}

TEST_CASE(
    "[Networked][Services] declaring prediction arms rewind, so a game that "
    "predicts an entity never installs a configuration to turn the engine on"
) {
    LoopbackRig rig(0);
    WorldDecl world;
    world.clocked(60, 0);
    rig.declare_world(world);

    netw::NetwMultiplayer *api = rig.server();
    CHECK_FALSE(api->lagcomp_is_configured());

    const RID entity = rig.declare_entity(
        EntityDecl()
            .named("Target")
            .on_schema("target")
            .synced(StringName("position"))
            .placed_at(Vector2(4.0, 0.0))
    );

    NETW_CHECK_EQ(int(api->predict_declare(entity)), int(OK));
    CHECK(api->lagcomp_is_configured());
    CHECK(api->predict_engine_seated(entity));
}

TEST_CASE(
    "[Networked][Services] arming rewind late adopts the entities already "
    "mounted, so a scene standing before the first declaration is not skipped "
    "by the watch that arrives after it"
) {
    LoopbackRig rig(0);
    WorldDecl world;
    world.clocked(60, 0);
    rig.declare_world(world);

    netw::NetwMultiplayer *api = rig.server();
    const RID standing = rig.declare_entity(
        EntityDecl()
            .named("Standing")
            .on_schema("target")
            .synced(StringName("position"))
            .placed_at(Vector2(1.0, 0.0))
    );
    CHECK_FALSE(api->lagcomp_is_configured());

    const RID predicted = rig.declare_entity(
        EntityDecl()
            .named("Predicted")
            .on_schema("target")
            .synced(StringName("position"))
            .placed_at(Vector2(2.0, 0.0))
    );
    NETW_CHECK_EQ(int(api->predict_declare(predicted)), int(OK));

    NETW_CHECK_EQ(int(api->lagcomp_timeline_declare(standing)), int(OK));
    CHECK(api->entity_get_node(standing) != nullptr);
}

TEST_CASE("[Networked][Services] a declared world installs both services") {
    LoopbackRig rig(1);
    WorldDecl world;
    world.clocked(53, 4).lag_compensated();
    rig.declare_world(world);

    netw::ClockEngine &server_clock = rig.clock_of(rig.server());
    netw::ClockEngine &client_clock = rig.clock_of(rig.client(0));

    NETW_CHECK_EQ(server_clock.get_tickrate(), 53);
    NETW_CHECK_EQ(client_clock.get_tickrate(), 53);
    CHECK(rig.server()->lagcomp_is_configured());
    CHECK(rig.client(0)->lagcomp_is_configured());
}

TEST_CASE(
    "[Networked][Services] a Resource the session declares no service for "
    "falls through to the engine, which refuses it, so a payload no service "
    "claims is reported rather than silently accepted"
) {
    LoopbackRig rig(1);
    netw::NetwMultiplayer *api = rig.server();

    Ref<Resource> config;
    config.instantiate();
    REQUIRE(config.is_valid());
    NETW_CHECK_EQ(
        int(netw::NetwMultiplayer::configuration_door_refuses_config(
            config.ptr()
        )),
        int(OK)
    );

    Node *subject = memnew(Node);
    NETW_CHECK_EQ(
        int(api->NETW_API_VIRTUAL(object_configuration_add)(subject, config)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(
        int(
            api->NETW_API_VIRTUAL(object_configuration_remove)(subject, config)
        ),
        int(ERR_INVALID_PARAMETER)
    );
    memdelete(subject);
}

} // namespace TestDeclaredServiceLaws

#endif

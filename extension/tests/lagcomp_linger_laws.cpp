#include "support/netw_test.h"

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/record.hpp"
#include "netw/api/timeline.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwLagCompLingerLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwDespawnOpts;
using netw::NetwEntity;
using netw::NetwTimeline;

constexpr double LINGER_SECONDS = 0.2;
constexpr int LINGER_TICKS = 6;
constexpr int64_t SEEDED_TICK = 5;
constexpr double SEEDED_X = 40.0;

Dictionary at(double p_x) {
    Dictionary out;
    out[StringName("position")] = Vector2(real_t(p_x), 0.0);
    return out;
}

TEST_CASE(
    "[Networked][LagComp][Linger] LG1 a lingering despawn keeps its "
    "target rewindable for a window the session counts in its OWN ticks, so a "
    "shot fired at a target that has just died still validates against where "
    "that target stood"
) {
    LoopbackRig rig(0);
    WorldDecl world;
    world.clocked(30, 3).lag_compensated();
    rig.declare_world(world);

    const RID entity = rig.declare_entity(
        EntityDecl()
            .named("Linger")
            .on_schema("target")
            .synced(StringName("position"))
            .placed_at(Vector2(10.0, 0.0))
    );
    netw::NetwMultiplayer *api = rig.server();
    NETW_CHECK_EQ(int(api->lagcomp_timeline_declare(entity)), int(OK));

    Node *body = api->entity_get_node(entity);
    REQUIRE(body != nullptr);
    const Ref<NetwEntity> wrapper = NetwEntity::of(body);
    REQUIRE(wrapper.is_valid());

    const Ref<NetwTimeline> history = api->lagcomp_timeline_of(entity);
    REQUIRE(history.is_valid());
    history->record_state(SEEDED_TICK, at(SEEDED_X));

    Ref<NetwDespawnOpts> opts;
    opts.instantiate();
    opts->set_reason(StringName("killed"));
    opts->set_linger(true);
    opts->set_linger_seconds(LINGER_SECONDS);
    wrapper->despawn(opts);

    rig.step_ticks(LINGER_TICKS - 1);

    CHECK_FALSE(body->is_queued_for_deletion());
    CHECK(api->lagcomp_timeline_of(entity).is_valid());

    const Ref<netw::DictionaryRecord> during
        = api->lagcomp_sample(entity, SEEDED_TICK);
    REQUIRE(during.is_valid());
    CHECK_FALSE(during->is_empty());
    NETW_CHECK_CLOSE(
        double(Vector2(during->get_value(StringName("position"))).x),
        SEEDED_X,
        0.001
    );

    SUBCASE("and the tick that closes the window is the one that frees it") {
        rig.step_ticks(1);
        CHECK(body->is_queued_for_deletion());
    }
}

TEST_CASE(
    "[Networked][LagComp][Linger] LG2 undeclaring a target takes its "
    "window with it, which is what the state pipeline does when a target "
    "leaves for good, so a rewind past that is an absence rather than a "
    "stale row"
) {
    LoopbackRig rig(0);
    WorldDecl world;
    world.clocked(30, 3).lag_compensated();
    rig.declare_world(world);

    const RID entity = rig.declare_entity(
        EntityDecl()
            .named("Linger")
            .on_schema("target")
            .synced(StringName("position"))
            .placed_at(Vector2(10.0, 0.0))
    );
    netw::NetwMultiplayer *api = rig.server();
    NETW_CHECK_EQ(int(api->lagcomp_timeline_declare(entity)), int(OK));

    Node *body = api->entity_get_node(entity);
    REQUIRE(body != nullptr);
    const Ref<NetwEntity> wrapper = NetwEntity::of(body);
    const Ref<NetwTimeline> history = api->lagcomp_timeline_of(entity);
    REQUIRE(history.is_valid());
    history->record_state(SEEDED_TICK, at(SEEDED_X));

    rig.server()->lagcomp_timeline_undeclare(entity);

    CHECK(api->lagcomp_timeline_of(entity).is_null());

    const Ref<netw::DictionaryRecord> after
        = api->lagcomp_sample(entity, SEEDED_TICK);
    REQUIRE(after.is_valid());
    CHECK(after->is_empty());
}

} // namespace TestNetwLagCompLingerLaws

#endif

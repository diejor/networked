#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/carrier.h"
#include "support/loopback_rig.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/timeline.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>

namespace TestEntityRelocationLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwReparentOpts;
using netw::NetwTimeline;
using netw_test::Carrier;

constexpr int64_t SAMPLED_TICK = 4;

struct Travelled {
    LoopbackRig rig;
    Node2D *home = nullptr;
    Node2D *away = nullptr;
    Node2D *body = nullptr;
    Ref<NetwEntity> entity;
    RID handle;

    Travelled() : rig(0) {
        rig.mount();
        flow_clocks(rig, 30);
        home = memnew(Node2D);
        home->set_name("Home");
        rig.branch(-1)->add_child(home);
        away = memnew(Node2D);
        away->set_name("Away");
        away->set_position(Vector2(100, 0));
        rig.branch(-1)->add_child(away);

        body = memnew(Node2D);
        body->set_name("Traveller");
        entity = NetwEntity::ensure(body);
        home->add_child(body);
        handle = core()->entity_of(body);
        REQUIRE(handle.is_valid());
    }

    ~Travelled() {
        home->get_parent()->remove_child(home);
        away->get_parent()->remove_child(away);
        memdelete(home);
        memdelete(away);
    }

    NetwMultiplayer *core() const {
        return flow_core(rig.server());
    }

    Ref<NetwTimeline> timeline() const {
        return core()->lagcomp_timeline_of(handle);
    }

    void move() {
        Ref<NetwReparentOpts> opts;
        opts.instantiate();
        entity->reparent_to(away, opts);
        core()->session_flush_deferred();
    }
};

TEST_CASE(
    "[Networked][Entity][Relocation][SceneTree] RL1 a settled move keeps the "
    "timeline the entity is registered in and floors what it holds, because "
    "the samples are in a frame the owner no longer stands in"
) {
    Travelled stand;
    REQUIRE(
        int(stand.core()->lagcomp_timeline_declare(stand.handle)) == int(OK)
    );
    const Ref<NetwTimeline> before = stand.timeline();
    REQUIRE(before.is_valid());

    Dictionary sample;
    sample[StringName("position")] = Vector2(1, 2);
    before->record_state(SAMPLED_TICK, sample);
    CHECK_FALSE(before->latest_state_at_or_before(SAMPLED_TICK).is_empty());

    stand.rig.step_ticks(int(SAMPLED_TICK) + 2);
    stand.move();

    const Ref<NetwTimeline> after = stand.timeline();
    NETW_CHECK_EQ(int(after.is_valid()), 1);
    NETW_CHECK_EQ(int(after == before), 1);
    NETW_CHECK_EQ(int(before->floor() > SAMPLED_TICK), 1);
    NETW_CHECK_EQ(
        int(before->latest_state_at_or_before(SAMPLED_TICK).is_empty()),
        1
    );
}

TEST_CASE(
    "[Networked][Entity][Relocation][SceneTree] RL2 a move keeps the owner's "
    "world transform under a transformed destination parent"
) {
    Travelled stand;
    stand.body->set_global_position(Vector2(5, 5));

    stand.move();

    CHECK(stand.body->get_parent() == stand.away);
    CHECK(stand.body->get_global_position() == Vector2(5, 5));
}

TEST_CASE(
    "[Networked][Entity][Relocation][SceneTree] RL3 a settled move resets the "
    "engine's interpolation on the owner that arrived, so it is drawn where "
    "it now stands rather than sliding across the gap it was moved over"
) {
    LoopbackRig rig(0);
    rig.mount();
    SceneTree *tree = netw::gd::scene_tree();
    REQUIRE(tree != nullptr);
    const bool interpolating = tree->is_physics_interpolation_enabled();
    tree->set_physics_interpolation_enabled(true);

    Node2D *away = memnew(Node2D);
    away->set_name("Away");
    away->set_position(Vector2(100, 0));
    rig.branch(-1)->add_child(away);

    Carrier *body = memnew(Carrier);
    body->set_name("Interpolated");
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    REQUIRE(entity.is_valid());
    rig.branch(-1)->add_child(body);
    NetwMultiplayer *core = flow_core(rig.server());

    Ref<NetwReparentOpts> opts;
    opts.instantiate();
    entity->reparent_to(away, opts);
    const int at_reentry = body->reset_count();

    core->session_flush_deferred();
    NETW_CHECK_EQ(body->reset_count(), at_reentry + 1);

    away->remove_child(body);
    memdelete(body);
    away->get_parent()->remove_child(away);
    memdelete(away);
    tree->set_physics_interpolation_enabled(interpolating);
}

TEST_CASE(
    "[Networked][Entity][Relocation][SceneTree] RL4 a 3D move keeps the "
    "owner's world transform under a transformed destination parent"
) {
    LoopbackRig rig(0);
    rig.mount();
    Node3D *home = memnew(Node3D);
    Node3D *away = memnew(Node3D);
    Node3D *body = memnew(Node3D);
    away->set_position(Vector3(100, 0, 0));
    rig.branch(-1)->add_child(home);
    rig.branch(-1)->add_child(away);
    home->add_child(body);
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    body->set_global_position(Vector3(5, 6, 7));

    entity->reparent_to(away, Ref<NetwReparentOpts>());
    flow_core(rig.server())->session_flush_deferred();

    CHECK(body->get_parent() == away);
    CHECK(body->get_global_position() == Vector3(5, 6, 7));
    away->remove_child(body);
    memdelete(body);
    home->get_parent()->remove_child(home);
    away->get_parent()->remove_child(away);
    memdelete(home);
    memdelete(away);
}

} // namespace TestEntityRelocationLaws

#endif

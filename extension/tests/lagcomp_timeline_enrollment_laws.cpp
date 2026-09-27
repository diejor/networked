#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/templates.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/record.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/api/timeline.hpp"
#include "netw/property_set_builder.hpp"
#include "support/carrier.h"

namespace TestNetwLagCompTimelineEnrollmentLaws {

using namespace godot;
using namespace netw_test;
using netw::Netw;
using netw::NetwClockConfig;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPropertyConfig;
using netw::NetwPropertySet;
namespace property_set_builder = netw::property_set_builder;

constexpr int COORDINATOR = 7;
constexpr int MEMBER = 9;
constexpr int TICKRATE = 30;

Node *mount_under_root(const char *p_name) {
    Node *mount = memnew(Node);
    mount->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(mount);
    return mount;
}

void drop(Node *p_mount) {
    netw::gd::scene_root()->remove_child(p_mount);
    memdelete(p_mount);
}

void arm_simulation(NetwMultiplayer *p_session) {
    REQUIRE(p_session != nullptr);
    Ref<NetwClockConfig> clock;
    clock.instantiate();
    clock->set("tickrate", TICKRATE);
    NETW_CHECK_EQ(int(p_session->clock_initialize(clock)), int(OK));
    p_session->clock_engine().set_manual_tick(true);
    NETW_CHECK_EQ(int(p_session->lagcomp_initialize(8, 12)), int(OK));
}

Ref<NetwPropertySet> a_state_stream(Node *p_node) {
    const Ref<NetwPropertyConfig> config
        = Netw::configure_property(p_node, StringName("position"), true);
    config->state();
    Dictionary configs;
    configs[StringName("position")] = config;
    const Ref<NetwPropertySet> set
        = property_set_builder::from_property_configs(
            configs,
            NetwPropertySet::RECORD_STATE
        );
    REQUIRE(set.is_valid());
    set->set_sealed(true);
    return set;
}

Carrier *a_state_body(
    NetwMultiplayer *p_session,
    Node *p_branch,
    const char *p_name,
    double p_x
) {
    Carrier *body = memnew(Carrier);
    body->set_name(StringName(p_name));
    body->set_position(Vector2(real_t(p_x), 0.0));
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    REQUIRE(entity.is_valid());
    p_branch->add_child(body);
    NETW_CHECK_EQ(
        int(p_session->sync_pipeline()
                ->register_property_set(body, a_state_stream(body))),
        int(OK)
    );
    return body;
}

RID handle_of(NetwMultiplayer *p_session, Node *p_body) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_body);
    REQUIRE(entity.is_valid());
    return p_session->entity_of(entity->get_owner());
}

int64_t newest_recorded_tick(NetwMultiplayer *p_session, const RID &p_entity) {
    const Ref<netw::NetwTimeline> history
        = p_session->lagcomp_timeline_of(p_entity);
    if (history.is_null()) {
        return -1;
    }
    return history->latest_state_tick_at_or_before(
        int64_t(p_session->clock_engine().get_tick())
    );
}

double sampled_x(
    NetwMultiplayer *p_session,
    const RID &p_entity,
    int64_t p_tick
) {
    const Ref<netw::DictionaryRecord> row
        = p_session->lagcomp_sample(p_entity, p_tick);
    REQUIRE(row.is_valid());
    return double(Vector2(row->get_value(StringName("position"))).x);
}

TEST_CASE(
    "[Networked][LagComp][Timeline] TE1 the peer holding session authority "
    "enrolls a non-predicted state binding in the rewind registry, and a "
    "member enrolls nothing, whichever transport peer id either of them "
    "happens to carry"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(MEMBER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(host->is_server()), 0);
    NETW_CHECK_EQ(int(host->is_host()), 1);
    NETW_CHECK_EQ(int(guest->is_host()), 0);

    Node *held = mount_under_root("TE1Host");
    Node *seen = mount_under_root("TE1Member");
    stand.mount(COORDINATOR, held);
    stand.mount(MEMBER, seen);
    arm_simulation(host);
    arm_simulation(guest);

    Carrier *authored = a_state_body(host, held, "Target", 10.0);
    Carrier *received = a_state_body(guest, seen, "Target", 10.0);

    NETW_CHECK_EQ(
        int(host->lagcomp_timeline_of(handle_of(host, authored)).is_valid()),
        1
    );
    NETW_CHECK_EQ(
        int(guest->lagcomp_timeline_of(handle_of(guest, received)).is_valid()),
        0
    );

    drop(seen);
    drop(held);
}

TEST_CASE(
    "[Networked][LagComp][Timeline] TE2 the timeline a coordinator of 7 "
    "enrolled records the body as it moves, so a sample reads where it stood "
    "at a past tick and a rewind stands it there and puts it back"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(MEMBER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    REQUIRE(host != nullptr);

    Node *held = mount_under_root("TE2Host");
    stand.mount(COORDINATOR, held);
    arm_simulation(host);

    Carrier *body = a_state_body(host, held, "Target", 10.0);
    const RID entity = handle_of(host, body);
    NETW_CHECK_EQ(int(host->lagcomp_timeline_of(entity).is_valid()), 1);

    stand.step_ticks(3);
    const int64_t standing = newest_recorded_tick(host, entity);
    body->set_position(Vector2(40.0, 0.0));
    stand.step_ticks(3);
    const int64_t moved = newest_recorded_tick(host, entity);

    NETW_CHECK_EQ(int(standing >= 0), 1);
    NETW_CHECK_EQ(int(moved > standing), 1);
    NETW_CHECK_CLOSE(sampled_x(host, entity, standing), 10.0, 0.001);
    NETW_CHECK_CLOSE(sampled_x(host, entity, moved), 40.0, 0.001);

    body->set_position(Vector2(99.0, 0.0));
    TypedArray<RID> targets;
    targets.push_back(entity);
    host->lagcomp_rewind(
        targets,
        standing,
        Callable(body, StringName("observe_self"))
    );

    NETW_CHECK_EQ(body->rewind_visit_count(), 1);
    NETW_CHECK_CLOSE(double(body->observed_x()), 10.0, 0.001);
    NETW_CHECK_CLOSE(double(body->get_position().x), 99.0, 0.001);

    drop(held);
}

TEST_CASE(
    "[Networked][LagComp][Timeline] TE3 removing a state binding at a "
    "coordinator of 7 takes that entity's timeline and no other, through the "
    "reconcile the entity book runs and through the dropped-binding sweep"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(MEMBER);
    stand.wire(COORDINATOR, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    REQUIRE(host != nullptr);

    Node *held = mount_under_root("TE3Host");
    stand.mount(COORDINATOR, held);
    arm_simulation(host);

    Carrier *leaving = a_state_body(host, held, "Leaving", 10.0);
    Carrier *staying = a_state_body(host, held, "Staying", 20.0);
    const RID gone = handle_of(host, leaving);
    const RID kept = handle_of(host, staying);
    NETW_CHECK_EQ(int(host->lagcomp_timeline_of(gone).is_valid()), 1);
    NETW_CHECK_EQ(int(host->lagcomp_timeline_of(kept).is_valid()), 1);

    host->sync_pipeline()->unregister_derived(leaving);
    host->sync_pipeline()->reconcile_state_timeline_for_test(leaving);

    NETW_CHECK_EQ(int(host->lagcomp_timeline_of(gone).is_valid()), 0);
    NETW_CHECK_EQ(int(host->lagcomp_timeline_of(kept).is_valid()), 1);

    host->sync_pipeline()->unregister_derived(staying);
    host->sync_pipeline()->reconcile_dropped_state_timelines_for_test();

    NETW_CHECK_EQ(int(host->lagcomp_timeline_of(kept).is_valid()), 0);

    drop(held);
}

} // namespace TestNetwLagCompTimelineEnrollmentLaws

#endif

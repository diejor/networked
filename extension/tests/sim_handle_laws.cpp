#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_call_log.h"
#include "support/scenario_run.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwSimHandleLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwSimulationHandle;

const char *SEATED_ID = "simulation_handle_member";

Node *build_member(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Array named(const String &p_name) {
    Array out;
    out.push_back(p_name);
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

int seat_route(LoopbackRig &p_rig, Node *p_parent, const String &p_name) {
    return p_rig.spawn_registered(
        StringName(SEATED_ID),
        callable_mp_static(&build_member),
        named(p_name),
        one_type(),
        p_parent
    );
}

Ref<NetwEntity> view(LoopbackRig &p_rig, int p_route, int p_client) {
    Node *node = p_rig.route_node(p_route, p_client);
    REQUIRE_MESSAGE(node != nullptr, "the spawn reached no node");
    return NetwEntity::of(node);
}

bool names(const netw::sim::Choice &p_choice, const Ref<NetwEntity> &p_entity) {
    const netw::sim::Named *held
        = p_choice.named_of(p_entity->get_rid_handle());
    return held != nullptr && held->pick == netw::sim::Pick::CHOSEN;
}

TEST_CASE(
    "[Networked][Sim][Handle] replicas written on a copy's handle reach its "
    "resolved mode, and mode_changed carries each move once with the mode it "
    "left and the mode it entered"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const int route = seat_route(rig, arena, "Crate");
    const Ref<NetwEntity> copy = view(rig, route, 0);
    REQUIRE(copy.is_valid());
    const Ref<NetwSimulationHandle> simulation = copy->get_simulation();
    REQUIRE(simulation.is_valid());
    NETW_CHECK_EQ(
        int(simulation->get_mode()),
        int(NetwSimulationHandle::MODE_NONE)
    );

    CallLog log;
    simulation->connect(StringName("mode_changed"), log.callable("moved"));

    simulation->set_replicas(NetwSimulationHandle::REPLICAS_ACTIVE);
    NETW_CHECK_EQ(
        int(simulation->get_mode()),
        int(NetwSimulationHandle::MODE_ACTIVE)
    );
    simulation->set_replicas(NetwSimulationHandle::REPLICAS_PROXY);
    NETW_CHECK_EQ(
        int(simulation->get_mode()),
        int(NetwSimulationHandle::MODE_PROXY)
    );
    simulation->set_replicas(NetwSimulationHandle::REPLICAS_PROXY);

    NETW_REQUIRE_EQ(log.count(StringName("moved")), 2);
    const Array entered = log.args(StringName("moved"), 0);
    const Array left = log.args(StringName("moved"), 1);
    NETW_CHECK_EQ(int(entered[0]), int(NetwSimulationHandle::MODE_NONE));
    NETW_CHECK_EQ(int(entered[1]), int(NetwSimulationHandle::MODE_ACTIVE));
    NETW_CHECK_EQ(int(left[0]), int(NetwSimulationHandle::MODE_ACTIVE));
    NETW_CHECK_EQ(int(left[1]), int(NetwSimulationHandle::MODE_PROXY));
}

TEST_CASE(
    "[Networked][Sim][Handle] a selection the handle made outlives the "
    "session row that unregistering prediction releases, and the next row "
    "the session makes for the entity carries it again"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner
        = view(rig, seat_route(rig, arena, "Owner"), -1);
    const Ref<NetwEntity> member
        = view(rig, seat_route(rig, arena, "Member"), -1);
    REQUIRE(owner.is_valid());
    REQUIRE(member.is_valid());

    NetwMultiplayer *core = rig.server();
    core->register_prediction(owner);
    owner->get_simulation()->simulate(member);
    const RID subject = owner->get_rid_handle();
    REQUIRE(core->sim_row_of(subject) != nullptr);
    REQUIRE(names(core->sim_row_of(subject)->choice, member));

    core->unregister_prediction(owner);
    CHECK(core->sim_row_of(subject) == nullptr);
    CHECK(names(owner->get_simulation()->selection_choice(), member));

    CHECK(names(core->sim_row(subject).choice, member));
}

TEST_CASE(
    "[Networked][Sim][Handle] simulate on the handle refuses an entity from "
    "another scene, so neither the handle nor the session row names it"
) {
    Scenario scenario;
    scenario.label = "two-scenes";
    scenario.world.scene("Arena")
        .scene("Annex")
        .entity(
            EntityDecl().named("Owner").on_route(91).placed_at(Vector2()),
            "Arena"
        )
        .entity(
            EntityDecl().named("Stranger").on_route(92).placed_at(Vector2()),
            "Annex"
        )
        .entity(
            EntityDecl().named("Neighbour").on_route(93).placed_at(Vector2()),
            "Arena"
        );
    scenario.until(2);
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *core = rig.server();
    const Ref<NetwEntity> owner = core->entity_get_view(rig.entity_of("Owner"));
    const Ref<NetwEntity> stranger
        = core->entity_get_view(rig.entity_of("Stranger"));
    const Ref<NetwEntity> neighbour
        = core->entity_get_view(rig.entity_of("Neighbour"));
    REQUIRE(owner.is_valid());
    REQUIRE(stranger.is_valid());
    REQUIRE(neighbour.is_valid());
    REQUIRE(
        core->scene_of(owner->get_rid_handle())
        != core->scene_of(stranger->get_rid_handle())
    );

    const Ref<NetwSimulationHandle> simulation = owner->get_simulation();
    simulation->simulate(stranger);
    simulation->simulate(neighbour);

    CHECK_FALSE(names(simulation->selection_choice(), stranger));
    CHECK(names(simulation->selection_choice(), neighbour));
    const netw::sim::Row *row = core->sim_row_of(owner->get_rid_handle());
    REQUIRE(row != nullptr);
    CHECK_FALSE(names(row->choice, stranger));
    CHECK(names(row->choice, neighbour));
}

} // namespace TestNetwSimHandleLaws

#endif

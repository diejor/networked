#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "support/minted_script.h"
#include "support/scenario_run.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/predict/engine.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwSimSelectSessionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredictionEngine;
using netw::NetwPredictionHandle;

const char *SEATED_ID = "selection_member";

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

Ref<NetwEntity> seat(LoopbackRig &p_rig, Node *p_parent, const String &p_name) {
    const int route = p_rig.spawn_registered(
        StringName(SEATED_ID),
        callable_mp_static(&build_member),
        named(p_name),
        one_type(),
        p_parent
    );
    Node *node = p_rig.route_node(route, -1);
    REQUIRE_MESSAGE(node != nullptr, "the spawn reached no node");
    return NetwEntity::of(node);
}

RID rid(const Ref<NetwEntity> &p_entity) {
    return p_entity->get_rid_handle();
}

void nothing() {
}

int roster_size(NetwPredictionEngine *p_pool, int64_t p_slot) {
    return p_pool
        ->roster_list(p_slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS)
        .size();
}

TEST_CASE(
    "[Networked][Sim][Select] IM1 the first roster a subject commits is a "
    "declaration rather than a change, so it opens no out-of-domain window, "
    "and the next roster that moves does"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Owner");
    const Ref<NetwEntity> first = seat(rig, arena, "First");
    const Ref<NetwEntity> second = seat(rig, arena, "Second");

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);
    pool->set_latest_input_tick(slot, 40);

    REQUIRE(core->sim_simulate(rid(owner), first) == OK);
    pool->refresh_selection(slot, false, callable_mp_static(&nothing));
    NETW_CHECK_EQ(roster_size(pool, slot), 1);
    NETW_CHECK_EQ(pool->out_of_domain_until(slot), -1);

    core->sim_forget(rid(owner), first);
    REQUIRE(core->sim_simulate(rid(owner), second) == OK);
    pool->refresh_selection(slot, false, callable_mp_static(&nothing));
    NETW_CHECK_EQ(roster_size(pool, slot), 1);
    NETW_CHECK_EQ(pool->out_of_domain_until(slot) > 40, 1);
}

TEST_CASE(
    "[Networked][Sim][Select] IM2 a subject that selects nothing keeps no "
    "roster at all, because there is no group for a member to be seated in"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Lone");
    const Ref<NetwEntity> member = seat(rig, arena, "Other");

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);

    REQUIRE(core->sim_simulate(rid(owner), member) == OK);
    pool->refresh_selection(slot, false, callable_mp_static(&nothing));
    REQUIRE(roster_size(pool, slot) == 1);

    core->sim_forget(rid(owner), member);
    REQUIRE_FALSE(core->sim_chooses(rid(owner)));
    pool->refresh_selection(slot, false, callable_mp_static(&nothing));
    NETW_CHECK_EQ(roster_size(pool, slot), 0);
}

TEST_CASE(
    "[Networked][Sim][Select] IM3 the published roster is sorted by entity "
    "id, so two peers that reached one roster by different routes report the "
    "same rows"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Zulu");
    const Ref<NetwEntity> first = seat(rig, arena, "Yankee");
    const Ref<NetwEntity> second = seat(rig, arena, "Alfa");

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);

    const bool first_is_lower
        = String(first->get_entity_id()) < String(second->get_entity_id());
    TypedArray<NetwEntity> descending;
    descending.push_back(first_is_lower ? second : first);
    descending.push_back(first_is_lower ? first : second);
    pool->roster_assign(
        slot,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        descending
    );

    pool->publish_island_roster(slot);

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(owner->get_prediction())
    );
    REQUIRE(handle != nullptr);
    const PackedStringArray published
        = handle->get_stats()->get_names_fact(
            netw::NetwPredictStats::FACT_ISLAND_MEMBERS
        );
    REQUIRE(published.size() == 2);
    NETW_CHECK_EQ(String(published[0]) < String(published[1]), 1);
}

TEST_CASE(
    "[Networked][Sim][Select] IM4 a despawned subject releases the member it "
    "selected before its own slot closes, so unregister_prediction leaves no "
    "member selected"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Owner");
    const Ref<NetwEntity> member = seat(rig, arena, "Member");
    REQUIRE(owner.is_valid());
    REQUIRE(member.is_valid());

    NetwMultiplayer *core = rig.server();
    core->register_prediction(owner);
    core->register_prediction(member);

    core->sim_note_selected_by(rid(member), rid(owner), true);
    REQUIRE(core->sim_selection_count(rid(member)) == 1);

    core->unregister_prediction(owner);

    NETW_CHECK_EQ(core->sim_selection_count(rid(member)), 0);
}

TEST_CASE(
    "[Networked][Sim][Select] the candidates name each entity once, in "
    "entity id order, and never the subject"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Owner");
    const Ref<NetwEntity> zulu = seat(rig, arena, "Zulu");
    const Ref<NetwEntity> alfa = seat(rig, arena, "Alfa");

    NetwMultiplayer *core = rig.server();
    REQUIRE(core->sim_simulate(rid(owner), zulu) == OK);
    REQUIRE(core->sim_simulate(rid(owner), alfa) == OK);
    REQUIRE(core->sim_simulate(rid(owner), zulu) == OK);
    REQUIRE(core->sim_simulate(rid(owner), owner) == OK);

    const TypedArray<NetwEntity> candidates = core->sim_candidates(owner);
    REQUIRE(candidates.size() == 2);
    const bool alfa_first
        = String(alfa->get_entity_id()) < String(zulu->get_entity_id());
    CHECK((Ref<NetwEntity>(candidates[0]) == (alfa_first ? alfa : zulu)));
    CHECK((Ref<NetwEntity>(candidates[1]) == (alfa_first ? zulu : alfa)));
}

TEST_CASE(
    "[Networked][Sim][Select] a selection from another scene is refused and "
    "names nothing"
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
        );
    scenario.until(2);
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::scenes(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *core = rig.server();
    const Ref<NetwEntity> owner = core->entity_get_view(rig.entity_of("Owner"));
    const Ref<NetwEntity> stranger
        = core->entity_get_view(rig.entity_of("Stranger"));
    REQUIRE(owner.is_valid());
    REQUIRE(stranger.is_valid());
    REQUIRE(core->scene_of(rid(owner)) != core->scene_of(rid(stranger)));

    NETW_CHECK_EQ(
        int(core->sim_simulate(rid(owner), stranger)),
        int(ERR_INVALID_PARAMETER)
    );
    CHECK_FALSE(core->sim_chooses(rid(owner)));
    NETW_CHECK_EQ(core->sim_candidates(owner).size(), 0);
}

TEST_CASE(
    "[Networked][Sim][Select] the live participants are the committed roster "
    "where one stands, and the named selection only until then"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Owner");
    const Ref<NetwEntity> declared = seat(rig, arena, "Declared");
    const Ref<NetwEntity> committed = seat(rig, arena, "Committed");

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(owner);
    REQUIRE(slot >= 0);
    REQUIRE(core->sim_simulate(rid(owner), declared) == OK);

    TypedArray<NetwEntity> live = pool->live_participants(slot);
    REQUIRE(live.size() == 1);
    CHECK((Ref<NetwEntity>(live[0]) == declared));

    pool->roster_add(
        slot,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        committed
    );
    live = pool->live_participants(slot);
    REQUIRE(live.size() == 1);
    CHECK((Ref<NetwEntity>(live[0]) == committed));
}

TEST_CASE(
    "[Networked][Sim][Select] the ranking distance is the gap between the two "
    "nodes, which is what nearest and within spend"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> owner = seat(rig, arena, "Owner");
    const Ref<NetwEntity> far = seat(rig, arena, "Far");
    const Ref<NetwEntity> near = seat(rig, arena, "Near");
    Object::cast_to<Node2D>(owner->get_owner())->set_position(Vector2(0, 0));
    Object::cast_to<Node2D>(far->get_owner())->set_position(Vector2(10, 0));
    Object::cast_to<Node2D>(near->get_owner())->set_position(Vector2(0, 2));

    NetwMultiplayer *core = rig.server();
    REQUIRE(core->sim_simulate(rid(owner), far) == OK);
    REQUIRE(core->sim_simulate(rid(owner), near) == OK);
    REQUIRE(
        core->sim_resolve(rid(owner), core->sim_body_facts(owner))
        == netw::sim::Mode::AUTHORITY
    );
    core->sim_select(owner, core->sim_candidates(owner), {}, 0);

    const netw::sim::Row *row = core->sim_row_of(rid(owner));
    REQUIRE(row != nullptr);
    const netw::sim::Selected *to_far
        = row->selection.member(int64_t(rid(far).get_id()));
    const netw::sim::Selected *to_near
        = row->selection.member(int64_t(rid(near).get_id()));
    REQUIRE(to_far != nullptr);
    REQUIRE(to_near != nullptr);
    NETW_CHECK_CLOSE(to_far->distance_squared, 100.0, 0.001);
    NETW_CHECK_CLOSE(to_near->distance_squared, 4.0, 0.001);
}

EntityDecl lane_player(const char *p_name) {
    return EntityDecl()
        .named(p_name)
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario selecting_lane() {
    Scenario scenario;
    scenario.label = "selecting-lane";
    scenario.epsilon = 0.01;
    Vector<StringName> members;
    members.push_back(StringName("Q"));
    scenario.world.clocked(30, 3)
        .lag_compensated()
        .player(lane_player("P"), 0)
        .player(lane_player("Q"), 1)
        .island(StringName("P"), members, WorldDecl::INDEPENDENT)
        .simulating(StringName("Q"));
    scenario.clients = 2;
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    scenario.hold_input(1, "Q", Vector2(1.0, 0.0));
    return scenario.until(20);
}

TEST_CASE(
    "[Networked][Sim][Select] a subject that stops leading drops its "
    "selections in the pass that resolved it, so the member it held is no "
    "longer ACTIVE before another tick runs"
) {
    const Scenario scenario = selecting_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *const predictor = rig.client(0);
    const RID subject = rig.entity_of("P", 0);
    const RID member = rig.entity_of("Q", 0);
    const netw::sim::Row *held = predictor->sim_row_of(subject);
    REQUIRE(held != nullptr);
    REQUIRE(held->mode == netw::sim::Mode::PREDICT);
    REQUIRE(predictor->sim_selection_count(member) == 1);
    REQUIRE(predictor->sim_row_of(member)->mode == netw::sim::Mode::ACTIVE);

    rig.server()->entity_grant_control(rig.entity_of("P"), rig.peer_id(1));
    rig.pump(4);

    NETW_CHECK_EQ(
        int(predictor->sim_row_of(subject)->mode),
        int(netw::sim::Mode::PROXY)
    );
    NETW_CHECK_EQ(predictor->sim_selection_count(member), 0);
    NETW_CHECK_EQ(
        int(predictor->sim_row_of(member)->mode),
        int(netw::sim::Mode::PROXY)
    );
}

constexpr const char *CRATE = R"(extends Node2D

var crate_position: Vector2
var steps := 0
var fresh_steps := 0

func _init() -> void:
	Netw.configure_property(self, &"crate_position").broadcast()

func _network_tick(_delta: float, _tick: int, fresh: bool) -> void:
	steps += 1
	if fresh:
		fresh_steps += 1
)";

int64_t steps_of(Node2D *p_node) {
    return int64_t(p_node->get(StringName("steps")));
}

TEST_CASE(
    "[Networked][Sim][Select] X05 an inputless body a subject selects runs "
    "ACTIVE and steps once per tick without authoring, and stops when the "
    "selection ends"
) {
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30, 3);
    const FlowPair subject = stand_flow_pair(rig, CRATE, "Subject");
    const FlowPair member = stand_flow_pair(rig, CRATE, "Member");
    steer(subject, rig.peer_id(0));
    rig.pump(2);

    NetwMultiplayer *client = flow_core(rig.client(0));
    const Ref<NetwEntity> chooser = NetwEntity::of(subject.mirror(0));
    const Ref<NetwEntity> chosen = NetwEntity::of(member.mirror(0));
    Node2D *copy = member.mirror(0);
    rig.step_ticks(3);
    REQUIRE(steps_of(copy) == 0);

    REQUIRE(client->sim_simulate(rid(chooser), chosen) == OK);
    rig.step_ticks(1);
    const int64_t selected_at = steps_of(copy);
    rig.step_ticks(5);
    NETW_CHECK_EQ(steps_of(copy) - selected_at, 5);
    NETW_CHECK_EQ(int64_t(copy->get(StringName("fresh_steps"))), 0);

    client->sim_forget(rid(chooser), chosen);
    rig.step_ticks(1);
    const int64_t forgotten_at = steps_of(copy);
    rig.step_ticks(5);
    NETW_CHECK_EQ(steps_of(copy), forgotten_at);
}

TEST_CASE(
    "[Networked][Sim][Select] an authoring subject with no prediction "
    "selects a copy, the copy runs ACTIVE until the subject forgets it or "
    "stops authoring, and a copy that selects runs nothing"
) {
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30, 3);
    const FlowPair subject = stand_flow_pair(rig, CRATE, "Subject");
    const FlowPair member = stand_flow_pair(rig, CRATE, "Member");
    steer(subject, rig.peer_id(0));
    rig.pump(2);

    NetwMultiplayer *client = flow_core(rig.client(0));
    const Ref<NetwEntity> chooser = NetwEntity::of(subject.mirror(0));
    const Ref<NetwEntity> chosen = NetwEntity::of(member.mirror(0));
    const auto mode_of = [&](const Ref<NetwEntity> &p_entity) {
        const netw::sim::Row *row = client->sim_row_of(rid(p_entity));
        return row == nullptr ? int(netw::sim::Mode::NONE) : int(row->mode);
    };

    REQUIRE(client->sim_simulate(rid(chooser), chosen) == OK);
    rig.step_ticks(2);
    NETW_CHECK_EQ(mode_of(chooser), int(netw::sim::Mode::AUTHORITY));
    NETW_CHECK_EQ(client->sim_selection_count(rid(chosen)), 1);
    NETW_CHECK_EQ(mode_of(chosen), int(netw::sim::Mode::ACTIVE));

    client->sim_forget(rid(chooser), chosen);
    rig.step_ticks(1);
    NETW_CHECK_EQ(client->sim_selection_count(rid(chosen)), 0);
    NETW_CHECK_EQ(mode_of(chosen), int(netw::sim::Mode::PROXY));

    REQUIRE(client->sim_simulate(rid(chooser), chosen) == OK);
    rig.step_ticks(1);
    REQUIRE(client->sim_selection_count(rid(chosen)) == 1);
    steer(subject, 0);
    rig.step_ticks(1);
    NETW_CHECK_EQ(mode_of(chooser), int(netw::sim::Mode::PROXY));
    NETW_CHECK_EQ(client->sim_selection_count(rid(chosen)), 0);
    NETW_CHECK_EQ(mode_of(chosen), int(netw::sim::Mode::PROXY));

    REQUIRE(client->sim_simulate(rid(chosen), chooser) == OK);
    rig.step_ticks(2);
    NETW_CHECK_EQ(client->sim_selection_count(rid(chooser)), 0);
    NETW_CHECK_EQ(mode_of(chooser), int(netw::sim::Mode::PROXY));
}

} // namespace TestNetwSimSelectSessionLaws

#endif

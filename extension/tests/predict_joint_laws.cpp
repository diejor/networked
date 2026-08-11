#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "netw/predict/joint.hpp"

namespace TestNetwPredictJointLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

IslandCandidate candidate(
    int64_t p_slot,
    double p_distance,
    Fidelity p_fidelity = Fidelity::UNDECLARED
) {
    IslandCandidate out;
    out.slot = p_slot;
    out.order_key = p_slot;
    out.distance_squared = p_distance * p_distance;
    out.fidelity = p_fidelity;
    out.eligible = true;
    return out;
}

LocalVector<IslandCandidate> candidates(
    const IslandCandidate &p_first,
    const IslandCandidate &p_second
) {
    LocalVector<IslandCandidate> out;
    out.push_back(p_first);
    out.push_back(p_second);
    return out;
}

Ref<NetwPredictDeclaration> declaration() {
    Ref<NetwPredictDeclaration> out;
    out.instantiate();
    out->append_field(
        StringName("value"),
        0,
        StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false
    );
    return out;
}

Array state(int p_value) {
    Array out;
    out.push_back(p_value);
    return out;
}

Ref<NetwPredictionEngine> pool() {
    Ref<NetwPredictionEngine> out;
    out.instantiate();
    return out;
}

void configure_joint(
    const Ref<NetwPredictionEngine> &p_pool,
    int64_t p_owner,
    int64_t p_member
) {
    CHECK(p_pool->configure(
        p_owner,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK(p_pool->configure(
        p_member,
        int(Schedule::TICK),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_DECLARED
    ));
}

PackedInt64Array commit_member(
    const Ref<NetwPredictionEngine> &p_pool,
    int64_t p_owner,
    int64_t p_member,
    int64_t p_frontier
) {
    PackedInt64Array members;
    members.push_back(p_member);
    PackedInt64Array order_keys;
    order_keys.push_back(10);
    PackedFloat64Array distances;
    distances.push_back(1.0);
    PackedInt32Array fidelities;
    fidelities.push_back(int(Fidelity::SIMULATED));
    PackedByteArray eligible;
    eligible.push_back(1);
    PackedByteArray contact;
    contact.push_back(0);
    return p_pool->island_commit(
        p_owner,
        20,
        members,
        order_keys,
        distances,
        fidelities,
        eligible,
        contact,
        int(Promotion::NONE),
        0,
        0.0,
        p_frontier
    );
}

TEST_CASE("[Networked][Predict][Hosted][Joint] oldest basis sets the floor") {
    LocalVector<int64_t> bases;
    bases.push_back(40);
    bases.push_back(34);
    LocalVector<int64_t> relays;
    const JointFloorDecision decision = joint_floor(bases, relays, -1, 0, 50);
    NETW_CHECK_EQ(decision.floor, 34);
    CHECK_FALSE(decision.heal);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] relay and epoch lower floor") {
    LocalVector<int64_t> bases;
    bases.push_back(40);
    LocalVector<int64_t> relays;
    relays.push_back(31);
    JointFloorDecision decision = joint_floor(bases, relays, -1, 0, 50);
    NETW_CHECK_EQ(decision.floor, 31);
    decision = joint_floor(bases, relays, 12, 0, 50);
    NETW_CHECK_EQ(decision.floor, 12);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] old floor heals to present") {
    LocalVector<int64_t> bases;
    bases.push_back(4);
    LocalVector<int64_t> relays;
    const JointFloorDecision decision = joint_floor(bases, relays, -1, 20, 50);
    CHECK(decision.heal);
    NETW_CHECK_EQ(decision.floor, 50);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] no basis stays at present") {
    LocalVector<int64_t> bases;
    LocalVector<int64_t> relays;
    const JointFloorDecision decision = joint_floor(bases, relays, -1, 0, 50);
    CHECK_FALSE(decision.heal);
    NETW_CHECK_EQ(decision.floor, 50);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] cell provenance is strict") {
    NETW_CHECK_EQ(int(joint_cell(true, true, true)), 3);
    NETW_CHECK_EQ(int(joint_cell(false, true, true)), 2);
    NETW_CHECK_EQ(int(joint_cell(false, false, true)), 1);
    NETW_CHECK_EQ(int(joint_cell(false, false, false)), 0);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a candidate joins undeclared") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t owner = engine->open(declaration());
    const int64_t candidate_slot = engine->open(declaration());
    CHECK(engine->configure(
        owner,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK(engine->configure(
        candidate_slot,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    ));

    const PackedInt64Array promoted
        = commit_member(engine, owner, candidate_slot, 0);
    NETW_CHECK_EQ(promoted.size(), 1);
    NETW_CHECK_EQ(promoted[0], candidate_slot);
    CHECK(engine->island_promoted(owner, candidate_slot));
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a framed candidate is refused") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t owner = engine->open(declaration());
    const int64_t candidate_slot = engine->open(declaration());
    CHECK(engine->configure(
        owner,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK(engine->configure(
        candidate_slot,
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    ));

    NETW_CHECK_EQ(commit_member(engine, owner, candidate_slot, 0).size(), 0);
    CHECK_FALSE(engine->island_promoted(owner, candidate_slot));
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a declared island takes frames") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t owner = engine->open(declaration());
    const int64_t candidate_slot = engine->open(declaration());
    CHECK(engine->configure(
        owner,
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_DECLARED
    ));
    CHECK(engine->configure(
        candidate_slot,
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    ));

    NETW_CHECK_EQ(commit_member(engine, owner, candidate_slot, 0).size(), 1);
    CHECK(engine->island_promoted(owner, candidate_slot));
}

TEST_CASE("[Networked][Predict][Hosted][Joint] explicit promotion wins") {
    Island island;
    island.commit(
        candidates(
            candidate(1, 20.0, Fidelity::SIMULATED),
            candidate(2, 1.0, Fidelity::PROXY)
        ),
        10
    );
    NETW_CHECK_EQ(island.promoted_count(), 1);
    CHECK(island.member(1)->promoted);
    CHECK_FALSE(island.member(2)->promoted);
    NETW_CHECK_EQ(island.member(1)->tenure.begin, 11);
    CHECK_FALSE(island.member(1)->tenure.contains(10));
    CHECK(island.member(1)->tenure.contains(11));
}

TEST_CASE("[Networked][Predict][Hosted][Joint] nearest promotion retains") {
    Island island;
    island.promotion = Promotion::NEAREST;
    island.promotion_count = 1;
    island.commit(candidates(candidate(1, 10.0), candidate(2, 10.5)), 0);
    CHECK(island.member(1)->promoted);
    island.commit(candidates(candidate(1, 10.0), candidate(2, 9.5)), 1);
    CHECK(island.member(1)->promoted);
    CHECK_FALSE(island.member(2)->promoted);
    island.commit(candidates(candidate(1, 10.0), candidate(2, 5.0)), 2);
    CHECK_FALSE(island.member(1)->promoted);
    CHECK(island.member(2)->promoted);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] contact defers a handoff") {
    Island island;
    island.promotion = Promotion::NEAREST;
    island.promotion_count = 1;
    island.commit(candidates(candidate(1, 10.0), candidate(2, 20.0)), 0);
    CHECK(island.member(1)->promoted);

    IslandCandidate near = candidate(2, 1.0);
    IslandCandidate touching = candidate(1, 10.0);
    touching.contact = true;
    island.commit(candidates(touching, near), 1);
    CHECK(island.member(1)->promoted);
    CHECK_FALSE(island.member(2)->promoted);

    island.commit(candidates(candidate(1, 10.0), near), 2);
    CHECK_FALSE(island.member(1)->promoted);
    CHECK(island.member(2)->promoted);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a deferral keeps its budget") {
    Island island;
    island.promotion = Promotion::NEAREST;
    island.promotion_count = 1;
    island.commit(candidates(candidate(1, 10.0), candidate(2, 20.0)), 0);
    CHECK(island.member(1)->promoted);

    IslandCandidate touching = candidate(1, 30.0);
    touching.contact = true;
    LocalVector<IslandCandidate> rows;
    rows.push_back(touching);
    rows.push_back(candidate(2, 1.0));
    rows.push_back(candidate(3, 2.0));
    island.commit(rows, 1);
    NETW_CHECK_EQ(island.promoted_count(), 1);
    CHECK(island.member(1)->promoted);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] radius has exit margin") {
    Island island;
    island.promotion = Promotion::WITHIN;
    island.promotion_meters = 10.0;
    island.commit(candidates(candidate(1, 9.0), candidate(2, 12.0)), 0);
    CHECK(island.member(1)->promoted);
    island.commit(candidates(candidate(1, 10.5), candidate(2, 12.0)), 1);
    CHECK(island.member(1)->promoted);
    island.commit(candidates(candidate(1, 11.1), candidate(2, 12.0)), 2);
    CHECK_FALSE(island.member(1)->promoted);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a late command keeps the state") {
    StateRow observed;
    observed.resize(1);
    observed.set(0, Variant(70));

    JointTrack track;
    track.record(4, observed, Variant(7), CellProvenance::SUBSTITUTED);
    track.record(4, StateRow(), Variant(9), CellProvenance::RELAYED);

    REQUIRE(track.state_at(4) != nullptr);
    NETW_CHECK_EQ(int(track.state_at(4)->values[0]), 70);
    REQUIRE(track.command_at(4) != nullptr);
    NETW_CHECK_EQ(int(track.command_at(4)->command), 9);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] a slot with no island records") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(declaration());
    CHECK(engine->configure(
        slot,
        int(Schedule::TICK),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    ));

    engine->joint_record(slot, 3, state(30), Variant(7), false, false, true);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 3)), 7);
    NETW_CHECK_EQ(
        engine->joint_provenance_at(slot, 3),
        int(CellProvenance::SUBSTITUTED)
    );

    // A guess never displaces the author's own command, whatever order the
    // two arrive in.
    engine->joint_record(slot, 3, Array(), Variant(9), false, true, false);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 3)), 9);
    engine->joint_record(slot, 3, Array(), Variant(11), false, false, true);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 3)), 9);
    NETW_CHECK_EQ(int(engine->joint_command_at(slot, 4)), 0);
    NETW_CHECK_EQ(engine->joint_provenance_at(slot, 4), -1);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] departure lingers to floor") {
    Island island;
    island.commit(
        candidates(
            candidate(1, 1.0, Fidelity::SIMULATED),
            candidate(2, 2.0, Fidelity::PROXY)
        ),
        4
    );
    LocalVector<IslandCandidate> remaining;
    remaining.push_back(candidate(2, 2.0, Fidelity::PROXY));
    island.commit(remaining, 8);
    CHECK(island.member(1)->tenure.contains(8));
    CHECK_FALSE(island.member(1)->tenure.contains(9));
    NETW_CHECK_EQ(island.lingering_count(), 1);
    island.release_lingering(8);
    NETW_CHECK_EQ(island.lingering_count(), 1);
    island.release_lingering(9);
    CHECK(island.member(1) == nullptr);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] explicit members do not spend budget"
) {
    Island island;
    island.promotion = Promotion::NEAREST;
    island.promotion_count = 1;
    island.commit(
        candidates(candidate(1, 20.0, Fidelity::SIMULATED), candidate(2, 1.0)),
        0
    );
    NETW_CHECK_EQ(island.promoted_count(), 2);
    CHECK(island.member(1)->promoted);
    CHECK(island.member(2)->promoted);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] command provenance improves") {
    JointTrack track;
    track.record(4, StateRow(), Variant(10), CellProvenance::SUBSTITUTED);
    track.record(4, StateRow(), Variant(20), CellProvenance::RELAYED);
    track.record(4, StateRow(), Variant(30), CellProvenance::COAST);
    NETW_CHECK_EQ(track.commands.size(), 1);
    const JointCommandRecord *command = track.command_at(4);
    REQUIRE(command != nullptr);
    NETW_CHECK_EQ(int(command->provenance), int(CellProvenance::RELAYED));
    NETW_CHECK_EQ(int(command->command), 20);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] support admits declared axes") {
    const Ref<NetwPredictionEngine> engine = pool();
    CHECK(engine->supports(
        int(Schedule::TICK),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_DECLARED
    ));
    CHECK_FALSE(engine->supports(
        int(Schedule::TICK),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT)
    ));
    CHECK(engine->supports(
        int(Schedule::STEPPED),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK_FALSE(engine->supports(
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        NetwPredictionEngine::ISLAND_JOINT
    ));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] a stepped group commits and passes"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    CHECK(engine->configure(
        owner,
        int(Schedule::STEPPED),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_JOINT
    ));
    CHECK(engine->configure(
        member,
        int(Schedule::STEPPED),
        int(Role::SIMULATE),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_DECLARED
    ));
    NETW_CHECK_EQ(commit_member(engine, owner, member, 0).size(), 1);

    engine->joint_record(owner, 0, state(0), Variant(0), true, false, false);
    engine->joint_record(member, 0, state(100), Variant(200), false, false, true);
    engine->joint_record(owner, 1, state(1), Variant(1), true, false, false);
    engine->joint_record(member, 1, state(101), Variant(201), false, false, true);
    engine->joint_note_basis(owner, 0, NetwPredictionEngine::JOINT_FLOOR_STATE);

    const Ref<NetwPredictJointPlan> plan = engine->joint_pass(owner, 1);
    REQUIRE(plan.is_valid());
    CHECK(plan->valid());
    NETW_CHECK_EQ(plan->floor(), 0);
    NETW_CHECK_EQ(plan->restore_count(), 2);
    NETW_CHECK_EQ(plan->step_count(), 2);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Joint] pass is transition then member order"
) {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    configure_joint(engine, owner, member);
    NETW_CHECK_EQ(commit_member(engine, owner, member, 0).size(), 1);

    for (int transition = 0; transition <= 3; ++transition) {
        engine->joint_record(
            owner,
            transition,
            state(transition),
            Variant(transition),
            true,
            false,
            false
        );
        engine->joint_record(
            member,
            transition,
            state(100 + transition),
            Variant(200 + transition),
            false,
            false,
            true
        );
    }
    engine
        ->joint_record(member, 2, state(102), Variant(302), false, true, false);
    engine->joint_note_basis(owner, 1, NetwPredictionEngine::JOINT_FLOOR_STATE);
    engine->joint_note_basis(owner, 0, NetwPredictionEngine::JOINT_FLOOR_STATE);

    const Ref<NetwPredictJointPlan> plan = engine->joint_pass(owner, 3);
    REQUIRE(plan.is_valid());
    CHECK(plan->valid());
    CHECK_FALSE(plan->heal());
    NETW_CHECK_EQ(plan->floor(), 1);
    NETW_CHECK_EQ(plan->restore_count(), 2);
    NETW_CHECK_EQ(plan->restore_slot(0), member);
    NETW_CHECK_EQ(plan->restore_slot(1), owner);
    NETW_CHECK_EQ(plan->step_count(), 4);
    NETW_CHECK_EQ(plan->step_transition(0), 2);
    NETW_CHECK_EQ(plan->step_slot(0), member);
    NETW_CHECK_EQ(plan->step_provenance(0), int(CellProvenance::RELAYED));
    NETW_CHECK_EQ(int(plan->step_command(0)), 302);
    NETW_CHECK_EQ(plan->step_slot(1), owner);
    NETW_CHECK_EQ(plan->step_transition(2), 3);
    NETW_CHECK_EQ(plan->step_slot(2), member);
    NETW_CHECK_EQ(int(engine->state_at(member, 0)), 101);

    const PackedInt64Array stats = engine->joint_stats(owner);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_PASSES], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_MEMBERS], 2);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_CELLS_RELAYED], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_CELLS_SUBSTITUTED], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_FLOOR], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_PRESENT], 3);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_MAX_DEPTH], 2);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_JOINT_FLOOR_STATE_MOVES], 1);
}

TEST_CASE("[Networked][Predict][Hosted][Joint] linger ends past tenure") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t owner = engine->open(declaration());
    const int64_t member = engine->open(declaration());
    configure_joint(engine, owner, member);
    commit_member(engine, owner, member, 0);

    PackedInt64Array none_i64;
    PackedFloat64Array none_f64;
    PackedInt32Array none_i32;
    PackedByteArray none_u8;
    engine->island_commit(
        owner,
        20,
        none_i64,
        none_i64,
        none_f64,
        none_i32,
        none_u8,
        none_u8,
        int(Promotion::NONE),
        0,
        0.0,
        8
    );
    NETW_CHECK_EQ(engine->tenure_end(member), 8);
    for (int transition = 8; transition <= 9; ++transition) {
        engine->joint_record(
            owner,
            transition,
            state(transition),
            Variant(),
            false,
            false,
            false
        );
        engine->joint_record(
            member,
            transition,
            state(100 + transition),
            Variant(),
            false,
            false,
            false
        );
    }
    engine->joint_note_basis(owner, 8, NetwPredictionEngine::JOINT_FLOOR_STATE);
    Ref<NetwPredictJointPlan> plan = engine->joint_pass(owner, 8);
    REQUIRE(plan.is_valid());
    NETW_CHECK_EQ(plan->restore_count(), 2);
    NETW_CHECK_EQ(
        engine->joint_stats(
            owner
        )[NetwPredictionEngine::STAT_JOINT_LINGER_HELD],
        1
    );

    engine->joint_note_basis(owner, 9, NetwPredictionEngine::JOINT_FLOOR_STATE);
    plan = engine->joint_pass(owner, 9);
    REQUIRE(plan.is_valid());
    NETW_CHECK_EQ(plan->restore_count(), 1);
    NETW_CHECK_EQ(
        engine->joint_stats(
            owner
        )[NetwPredictionEngine::STAT_JOINT_LINGER_HELD],
        0
    );
}

} // namespace TestNetwPredictJointLaws

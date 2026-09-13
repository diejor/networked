#include "support/netw_test.h"

#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "support/netw_cells.h"
#include "support/scenario_run.h"

using namespace godot;

namespace TestNetwPredictDriveLaws {

using namespace netw_test;

bool declared_rule(int64_t, const godot::StringName &) {
    return true;
}

Scenario driven_lane() {
    Scenario scenario;
    scenario.label = "driven-lane";
    scenario.epsilon = 0.5;
    scenario.warmup_ticks = 4;
    scenario.world.entity(
        EntityDecl()
            .named("P")
            .synced("position")
            .placed_at(godot::Vector2(0.0, 0.0))
    );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    return scenario.until(40);
}

Scenario lane_past_speculative_bound() {
    Scenario scenario = driven_lane();
    scenario.label = "unacknowledged-lane";
    return scenario.until(netw::predict::ACK_AGE_MAX * 2);
}

Scenario corrected_lane() {
    Scenario scenario = driven_lane();
    scenario.label = "corrected-lane";
    scenario.perturb(20, "P", godot::Vector2(6.0, 0.0));
    return scenario.until(60);
}

LawVerdict law_every_opened_row_closes(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.journal_rows() != lane.closed_rows()) {
        return law_broken(
            "%d of %d journal row(s) never closed",
            lane.journal_rows() - lane.closed_rows(),
            lane.journal_rows()
        );
    }
    if (lane.journal_rows() != lane.consumed()) {
        return law_broken(
            "%d journal row(s) for %d drive(s) that ran",
            lane.journal_rows(),
            lane.consumed()
        );
    }
    return law_held();
}

LawVerdict law_an_uncorrected_lane_chains(const ScenarioRun &p_run) {
    if (p_run.scenario().declares("perturb")) {
        return law_held();
    }
    const Lane lane = p_run.lane("P");
    if (lane.chain_breaks() != 0) {
        return law_broken(
            "an uncorrected lane broke its state chain %d time(s)",
            lane.chain_breaks()
        );
    }
    return law_held();
}

LawVerdict law_speculation_stops_at_the_horizon(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.journal_rows() > netw::predict::ACK_AGE_MAX) {
        return law_broken(
            "a lane nothing acknowledged opened %d transition(s) past a "
            "horizon of %d",
            lane.journal_rows(),
            netw::predict::ACK_AGE_MAX
        );
    }
    if (p_run.scenario().run_ticks <= netw::predict::ACK_AGE_MAX) {
        return law_held();
    }
    if (lane.held() == 0) {
        return law_broken(
            "a lane ran %d tick(s) past a horizon of %d without holding once",
            p_run.scenario().run_ticks,
            netw::predict::ACK_AGE_MAX
        );
    }
    return law_held();
}

const LawRow LAWS[] = {
    {"L-CLOSE",
     "every journal row a drive opened is closed by that drive, and there is "
     "one row per drive that ran",
     law_every_opened_row_closes},
    {"L-CHAIN",
     "a lane nothing corrected enters each transition holding the state the "
     "previous one produced, because a correction writes the body from "
     "outside the engine and a write no operator accounts for is what a "
     "chain break is",
     law_an_uncorrected_lane_chains},
    {"L-HORIZON",
     "a lane authority never acknowledges stops opening transitions at the "
     "speculative bound",
     law_speculation_stops_at_the_horizon},
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the drive laws hold across the corpus"
) {
    const Scenario CORPUS[] = {
        driven_lane(),
        lane_past_speculative_bound(),
        corrected_lane(),
    };
    for (const Scenario &scenario : CORPUS) {
        const ScenarioRun run = ScenarioRun::lanes(scenario);
        REQUIRE(run.regime_reached());
        REQUIRE(run.decisions() > 0);
        for (const LawRow &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

struct RedProof {
    const LawRow &law;
    Scenario (*scenario)();
    Plant plant;
};

const RedProof RED_PROOFS[] = {
    {LAWS[0], driven_lane, PLANT_SKIP_CLOSE},
    {LAWS[1], driven_lane, PLANT_FORGE_PRE},
    {LAWS[2], lane_past_speculative_bound, PLANT_ALWAYS_ACK},
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] every drive law breaks against its "
    "plant, paired with the one scenario carrying the evidence it reads: "
    "L-CLOSE and L-CHAIN on a lane driven and nothing more, L-HORIZON on a "
    "lane run long enough to reach the speculative bound"
) {
    for (const RedProof &proof : RED_PROOFS) {
        const Scenario scenario = proof.scenario();
        const ScenarioRun planted = ScenarioRun::lanes(scenario, proof.plant);
        REQUIRE(planted.decisions() > 0);
        NETW_CELL(proof.law, scenario);
        NETW_LAW_BREAKS(proof.law, planted);
    }
}

Scenario missing_input_lane(netw::MissingInput p_policy) {
    Scenario scenario;
    scenario.label = p_policy == netw::MissingInput::STALL
        ? "stalled-input-lane"
        : "repeat-last-input-lane";
    scenario.world.entity(
        EntityDecl()
            .named("P")
            .synced("position")
            .placed_at(godot::Vector2())
            .missing_input(p_policy)
    );
    scenario.input_at(1, "P", godot::Vector2(1.0, 0.0));
    scenario.input_at(3, "P", godot::Vector2(1.0, 0.0));
    return scenario.until(3);
}

LawVerdict law_missing_input_policy(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.missing() != 1 || lane.consumed() != 2) {
        return law_broken(
            "the lane consumed %d fresh input(s) and found %d missing",
            lane.consumed(),
            lane.missing()
        );
    }
    const netw::MissingInput policy
        = p_run.scenario().world.entity_at(0).missing_input_policy();
    const double expected = policy == netw::MissingInput::STALL ? 2.0 : 3.0;
    const double actual = godot::Vector2(lane.position()).x;
    if (std::abs(actual - expected) > 0.001) {
        return law_broken(
            "the policy advanced the lane by %g rather than %g",
            actual,
            expected
        );
    }
    return law_held();
}

const LawRow L_MISSING = {
    "L-MISSING",
    "a missing input advances once under REPEAT_LAST and never under STALL",
    law_missing_input_policy,
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] missing input obeys the declared policy"
) {
    const netw::MissingInput POLICIES[] = {
        netw::MissingInput::STALL,
        netw::MissingInput::REPEAT_LAST,
    };
    for (netw::MissingInput policy : POLICIES) {
        const Scenario scenario = missing_input_lane(policy);
        const ScenarioRun run = ScenarioRun::consume(scenario);
        REQUIRE(run.regime_reached());
        REQUIRE(run.decisions() == 3);
        NETW_CELL(L_MISSING, scenario);
        NETW_LAW_HOLDS(L_MISSING, run);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] STALL breaks when a missing input "
    "repeats"
) {
    const Scenario scenario = missing_input_lane(netw::MissingInput::STALL);
    const ScenarioRun run
        = ScenarioRun::consume(scenario, PLANT_REPEAT_MISSING);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_MISSING, scenario);
    NETW_LAW_BREAKS(L_MISSING, run);
}

namespace {

using godot::Dictionary;
using godot::PackedInt64Array;
using godot::Ref;
using netw::NetwPredictionEngine;

int64_t scheduled_slot(
    NetwPredictionEngine *p_pool,
    netw::Schedule p_schedule
) {
    godot::LocalVector<netw::predict::FieldDecl> fields;
    fields.push_back(
        netw::field_decl(
            godot::StringName("position"),
            int(netw::predict::PropertyClass::CAUSAL)
        )
    );
    const int64_t slot = p_pool->open(fields);
    REQUIRE(p_pool->configure(
        slot,
        int(p_schedule),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));
    return slot;
}

int64_t framed_slot(NetwPredictionEngine *p_pool) {
    return scheduled_slot(p_pool, netw::Schedule::FRAME);
}

Dictionary marked(int64_t p_value) {
    Dictionary out;
    out[godot::StringName("mark")] = p_value;
    return out;
}

int64_t stat_of(NetwPredictionEngine *p_pool, int64_t p_slot, int p_column) {
    return p_pool->drive_stats(p_slot)[p_column];
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a refused pass is counted by the "
    "reason it was refused"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    const netw::predict::DriveRecord held = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        false,
        0,
        0,
        0,
        0
    );
    CHECK(held.clamped);
    CHECK_FALSE(held.ran);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_AUTHORING_CLAMPED),
        1
    );
    NETW_CHECK_EQ(stat_of(pool, slot, NetwPredictionEngine::STAT_DRIVE_SEQ), 0);

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord first = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(first.ran);
    pool->close_drive(slot, first.transition, 0, 0, 0, 0);
    const netw::predict::DriveRecord repeat = pool->open_drive(
        slot,
        Dictionary(),
        1,
        2,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    CHECK(repeat.clamped);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_AUTHORING_CLAMPED),
        2
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] speculation past the confirmed "
    "horizon is held and counted"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    int64_t tick = 1;
    int held = 0;
    for (int at = 0; at < netw::predict::ACK_AGE_MAX + 4; ++at) {
        pool->record_input(slot, tick, int32_t(tick));
        const netw::predict::DriveRecord drive = pool->open_drive(
            slot,
            Dictionary(),
            tick,
            tick,
            1.0 / 60.0,
            1,
            true,
            0,
            0,
            0,
            0
        );
        if (drive.held) {
            held += 1;
        } else if (drive.ran) {
            pool->close_drive(slot, drive.transition, 0, 0, 0, 0);
        }
        tick += 1;
    }

    CHECK(held > 0);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_SPECULATION_HELD),
        held
    );
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        netw::predict::ACK_AGE_MAX
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a transition that did not continue "
    "the one before it breaks the chain, and an off-cadence frame faults the "
    "quantum"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord first = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        11,
        0,
        0,
        0
    );
    REQUIRE(first.ran);
    pool->close_drive(slot, first.transition, 77, 0, 0, 0);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_CHAIN_BREAKS),
        0
    );

    pool->record_input(slot, 2, 2);
    const netw::predict::DriveRecord broken = pool->open_drive(
        slot,
        Dictionary(),
        2,
        4,
        1.0 / 60.0,
        1,
        true,
        78,
        0,
        0,
        0
    );
    REQUIRE(broken.ran);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_CHAIN_BREAKS),
        1
    );

    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_QUANTUM_STEPS),
        3
    );
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_QUANTUM_DECLARED),
        1
    );
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_QUANTUM_FAULTS),
        1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a measured quantum saturates rather "
    "than reporting the whole stall, so a hitch fingerprints as a hitch"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord first = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(first.ran);
    pool->close_drive(slot, first.transition, 0, 0, 0, 0);

    pool->record_input(slot, 2, 2);
    const netw::predict::DriveRecord stalled = pool->open_drive(
        slot,
        Dictionary(),
        2,
        500,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(stalled.ran);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_QUANTUM_STEPS),
        15
    );
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_QUANTUM_FAULTS),
        1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] the speculative span is measured from "
    "the freshest proof on either lane"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    int64_t tick = 1;
    for (int at = 0; at < 6; ++at) {
        pool->record_input(slot, tick, int32_t(tick));
        const netw::predict::DriveRecord drive = pool->open_drive(
            slot,
            Dictionary(),
            tick,
            tick,
            1.0 / 60.0,
            1,
            true,
            0,
            0,
            0,
            0
        );
        REQUIRE(drive.ran);
        pool->close_drive(slot, drive.transition, 0, 0, 0, 0);
        tick += 1;
    }
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        5
    );
    pool->refresh_ack_age(slot);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        6
    );

    NETW_CHECK_EQ(pool->mark_authority_ack(slot, 3), 2);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        2
    );

    NETW_CHECK_EQ(pool->mark_authority_ack(slot, 1), 2);
    NETW_CHECK_EQ(pool->mark_authority_ack(slot, 5), 0);
    NETW_CHECK_EQ(pool->mark_authority_ack(slot + 9000, 5), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a clock that jumps past the "
    "contiguous lane re-keys the tape, and a frame transitions once"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    const int64_t epoch
        = pool->drive_cursors(slot)[NetwPredictionEngine::CURSOR_TAPE_EPOCH];
    pool->tape_reset(slot, epoch + 1);
    PackedInt64Array cursors = pool->drive_cursors(slot);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_TAPE_EPOCH], epoch + 1);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_NEXT_TAPE_ENTRY], 0);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_LAST_DRIVEN_ENTRY], -1);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_LATEST_INPUT_TICK], -1);
    NETW_CHECK_EQ(
        cursors[NetwPredictionEngine::CURSOR_LAST_FRAME_TRANSITION_TICK],
        -1
    );

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord drive = pool->open_drive(
        slot,
        Dictionary(),
        7,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    pool->close_drive(slot, drive.transition, 0, 0, 0, 0);
    cursors = pool->drive_cursors(slot);
    NETW_CHECK_EQ(
        cursors[NetwPredictionEngine::CURSOR_LAST_FRAME_TRANSITION_TICK],
        7
    );
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_NEXT_TAPE_ENTRY], 1);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_LAST_DRIVEN_ENTRY], 0);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_LATEST_INPUT_TICK], 1);
    NETW_CHECK_EQ(
        cursors[NetwPredictionEngine::CURSOR_LAST_DRIVEN_INPUT_TICK],
        1
    );

    NETW_CHECK_EQ(
        int(pool->drive_cursors(slot + 9000).size()),
        int(NetwPredictionEngine::CURSOR_COUNT)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a command authored without a "
    "transition takes a tape entry, and the span names what the ring holds"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    PackedInt64Array span = pool->tape_span(slot);
    NETW_CHECK_EQ(span[0], -1);
    NETW_CHECK_EQ(span[1], -1);

    pool->tape_author(slot, 11, true);
    pool->tape_author(slot, 12, false);
    span = pool->tape_span(slot);
    NETW_CHECK_EQ(span[0], 0);
    NETW_CHECK_EQ(span[1], 1);
    NETW_CHECK_EQ(pool->tape_size(slot), 2);
    NETW_CHECK_EQ(pool->tape_label_of(slot, 0), 11);
    NETW_CHECK_EQ(pool->tape_label_of(slot, 1), 12);
    CHECK(pool->tape_is_fresh(slot, 0));
    CHECK_FALSE(pool->tape_is_fresh(slot, 1));

    CHECK_FALSE(pool->journal_has(slot, 0));

    const int64_t epoch
        = pool->drive_cursors(slot)[NetwPredictionEngine::CURSOR_TAPE_EPOCH];
    pool->tape_prepare_tick(slot, 2);
    NETW_CHECK_EQ(pool->tape_size(slot), 2);
    NETW_CHECK_EQ(
        pool->drive_cursors(slot)[NetwPredictionEngine::CURSOR_TAPE_EPOCH],
        epoch
    );
    pool->tape_prepare_tick(slot, 40);
    NETW_CHECK_EQ(pool->tape_size(slot), 0);
    NETW_CHECK_EQ(pool->tape_span(slot)[1], -1);
    NETW_CHECK_EQ(
        pool->drive_cursors(slot)[NetwPredictionEngine::CURSOR_TAPE_EPOCH],
        epoch + 1
    );

    pool->tape_author(slot, 40, true);
    NETW_CHECK_EQ(pool->tape_span(slot)[0], 40);

    NETW_CHECK_EQ(pool->tape_span(slot + 9000)[0], -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] the journal answers for the "
    "transitions it holds and never for one it has not seen"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    CHECK_FALSE(pool->journal_has(slot, 0));
    CHECK_FALSE(pool->journal_has(slot, 4));

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord drive = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    const int64_t at = drive.transition;
    CHECK(pool->journal_has(slot, at));
    CHECK_FALSE(pool->journal_has(slot, at + 1));
    CHECK_FALSE(pool->journal_has(slot + 9000, at));

    pool->tape_reset(slot, 9);
    CHECK_FALSE(pool->journal_has(slot, at));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] an acknowledged transition retires "
    "the entry book by index and the input lane by the label it authored"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t framed = framed_slot(pool);

    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(framed, lane);
    const Ref<netw::NetwTimeline> entries = pool->entry_history(framed);
    REQUIRE(entries.is_valid());

    for (int64_t index = 0; index < 3; index += 1) {
        pool->tape_author(framed, 100 + index, true);
        entries->record_state(index, marked(index));
        lane->record_input(100 + index, marked(index));
    }

    pool->trim_history(framed, 1);
    NETW_CHECK_EQ(entries->floor(), 1);
    NETW_CHECK_EQ(lane->floor(), 101);
    CHECK(entries->state_at(0).is_empty());
    CHECK_FALSE(entries->state_at(1).is_empty());
    CHECK_FALSE(lane->has_input_at(100));
    CHECK(lane->has_input_at(101));

    const int64_t ticked = scheduled_slot(pool, netw::Schedule::TICK);
    Ref<netw::NetwTimeline> ticks = netw::NetwTimeline::create(64);
    pool->bind_timeline(ticked, ticks);
    for (int64_t tick = 5; tick < 8; tick += 1) {
        ticks->record_input(tick, marked(tick));
    }

    pool->trim_history(ticked, 6);
    NETW_CHECK_EQ(ticks->floor(), 6);
    CHECK_FALSE(ticks->has_input_at(5));
    CHECK(ticks->has_input_at(6));

    pool->trim_history(framed + 9000, 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a repeated FRAME entry replays the "
    "command the last fresh one authored"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    pool->tape_author(slot, 10, true);
    pool->tape_author(slot, 11, false);
    pool->tape_author(slot, 12, true);
    lane->record_input(10, marked(10));
    lane->record_input(11, marked(11));
    lane->record_input(12, marked(12));

    godot::LocalVector<netw::predict::ReplayEntry> entries
        = pool->replay_entries(slot, -1);
    REQUIRE(entries.size() == 3);
    const netw::predict::ReplayEntry repeated = entries[1];
    NETW_CHECK_EQ(repeated.index, 1);
    NETW_CHECK_EQ(repeated.label, 11);
    NETW_CHECK_EQ(int64_t(repeated.input[godot::StringName("mark")]), 10);

    const netw::predict::ReplayEntry fresh = entries[2];
    NETW_CHECK_EQ(fresh.index, 2);
    NETW_CHECK_EQ(int64_t(fresh.input[godot::StringName("mark")]), 12);

    entries = pool->replay_entries(slot, 0);
    REQUIRE(entries.size() == 2);
    const netw::predict::ReplayEntry first = entries[0];
    NETW_CHECK_EQ(first.index, 1);
    NETW_CHECK_EQ(int64_t(first.input[godot::StringName("mark")]), 10);

    CHECK(pool->replay_entries(slot + 9000, -1).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a tick tier walks every tick past "
    "the basis, and reads its states out of the lane"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = scheduled_slot(pool, netw::Schedule::TICK);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    lane->record_input(5, marked(5));
    lane->record_input(7, marked(7));
    pool->record_input(slot, 7, 0);

    const godot::LocalVector<netw::predict::ReplayEntry> entries
        = pool->replay_entries(slot, 4);
    REQUIRE(entries.size() == 3);
    for (uint32_t at = 0; at < 3; at += 1) {
        const netw::predict::ReplayEntry &entry = entries[at];
        NETW_CHECK_EQ(entry.index, 5 + int64_t(at));
        NETW_CHECK_EQ(entry.label, 5 + int64_t(at));
    }
    const netw::predict::ReplayEntry silent = entries[1];
    CHECK(silent.input.is_empty());

    lane->record_state(6, marked(66));
    NETW_CHECK_EQ(
        int64_t(pool->state_before(slot, 6)[godot::StringName("mark")]),
        66
    );
    CHECK(pool->state_before(slot + 9000, 6).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a FRAME transition reads its "
    "pre-state out of the entry book and not out of the lane"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    pool->entry_history(slot)->record_state(3, marked(3));
    lane->record_state(3, marked(99));

    NETW_CHECK_EQ(
        int64_t(pool->state_before(slot, 3)[godot::StringName("mark")]),
        3
    );

    const Ref<netw::NetwTimeline> held = pool->entry_history(slot);
    pool->configure(
        slot,
        int(netw::Schedule::FRAME),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    CHECK(pool->entry_history(slot) == held);
    NETW_CHECK_EQ(
        int64_t(pool->state_before(slot, 3)[godot::StringName("mark")]),
        3
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a tape epoch mints a fresh entry "
    "book, and the bound input lane survives it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);
    lane->record_input(100, marked(100));
    pool->entry_history(slot)->record_state(0, marked(0));

    pool->tape_reset(slot, 9);
    const Ref<netw::NetwTimeline> entries = pool->entry_history(slot);
    REQUIRE(entries.is_valid());
    CHECK(entries->state_at(0).is_empty());

    CHECK(lane->has_input_at(100));

    CHECK(pool->entry_history(slot + 9000).is_null());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] the drive frontier is the cursor the "
    "schedule keys its own history by, and the two are different numbers"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t framed = framed_slot(pool);
    const int64_t ticked = scheduled_slot(pool, netw::Schedule::TICK);

    NETW_CHECK_EQ(pool->drive_frontier(framed), -1);
    NETW_CHECK_EQ(pool->drive_frontier(ticked), -1);
    NETW_CHECK_EQ(pool->drive_frontier(framed + 9000), -1);

    pool->set_last_driven_entry_index(framed, 4);
    pool->set_latest_input_tick(framed, 77);
    NETW_CHECK_EQ(pool->drive_frontier(framed), 4);

    pool->set_last_driven_entry_index(ticked, 4);
    pool->set_latest_input_tick(ticked, 77);
    NETW_CHECK_EQ(pool->drive_frontier(ticked), 77);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a clean witness run is contiguous "
    "from the basis, and an open row ends the run rather than failing it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::FRAME),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE,
        true
    ));

    CHECK_FALSE(pool->recent_witness_clean(slot, 0));

    int64_t tick = 1;
    for (int at = 0; at < 3; ++at) {
        pool->record_input(slot, tick, int32_t(tick));
        const netw::predict::DriveRecord drive = pool->open_drive(
            slot,
            Dictionary(),
            tick,
            tick,
            1.0 / 60.0,
            1,
            true,
            0,
            0,
            0,
            0,
            0,
            int(netw::NetwPredictJournal::EVIDENCE_WITNESS)
        );
        REQUIRE(drive.ran);
        PackedStringArray touched;
        touched.append("path:/root/Ground");
        PackedInt32Array classes;
        classes.append(int(netw::NetwPredict::WITNESS_CLASS_STATIC));
        PackedInt32Array realized;
        realized.append(int(netw::NetwPredict::CONTACT_CLASS_OTHER_STATIC));
        PackedByteArray outside;
        outside.append(0);
        pool->record_evidence(
            slot,
            drive.transition,
            0,
            Dictionary(),
            Dictionary(),
            touched,
            classes,
            realized,
            outside,
            false
        );
        pool->close_drive(slot, drive.transition, 0, 0, 0, 0);
        tick += 1;
    }
    const PackedInt64Array held = pool->journal_transitions(slot);
    REQUIRE(int(held.size()) == 3);
    const int64_t first = held[0];

    CHECK(pool->recent_witness_clean(slot, first));

    pool->record_input(slot, tick, int32_t(tick));
    const netw::predict::DriveRecord open = pool->open_drive(
        slot,
        Dictionary(),
        tick,
        tick,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(open.ran);
    CHECK(pool->recent_witness_clean(slot, first));
    CHECK_FALSE(pool->recent_witness_clean(slot, open.transition));

    CHECK_FALSE(pool->recent_witness_clean(slot, first + 900));
    CHECK_FALSE(pool->recent_witness_clean(slot + 9000, first));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive][SceneTree] a solve is sealed by what "
    "it touched, deduplicated and sorted so two peers that touched the same "
    "world in a different order compare equal"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Node *scene = netw::gd::scene_root();
    REQUIRE(scene != nullptr);

    Node *own = memnew(Node);
    own->set_name("Own");
    const Ref<netw::NetwEntity> mine = netw::NetwEntity::ensure(own);
    mine->set_entity_id(StringName("mine"));
    scene->add_child(own);
    const int64_t slot = pool->slot_register(mine);

    Node *ground = memnew(StaticBody3D);
    ground->set_name("Ground");
    scene->add_child(ground);
    Node *crate = memnew(Node);
    crate->set_name("Crate");
    scene->add_child(crate);

    Dictionary base;
    base[StringName("body_mode")] = 0;

    const Dictionary silent = pool->solve_evidence(
        slot,
        base,
        7,
        Dictionary(),
        PackedStringArray()
    );
    CHECK(Dictionary(silent[StringName("detail")]).is_empty());
    CHECK_FALSE(silent.has(StringName("breach")));

    Array colliders;
    colliders.append(crate);
    colliders.append(ground);
    colliders.append(crate);
    Dictionary sample;
    sample[StringName("colliders")] = colliders;
    sample[StringName("sleeping")] = false;
    sample[StringName("collision_layer")] = 5;

    const Dictionary sealed
        = pool->solve_evidence(slot, base, 7, sample, PackedStringArray());
    const Dictionary detail = sealed[StringName("detail")];

    const PackedStringArray ids = detail[StringName("collider_ids")];
    NETW_CHECK_EQ(int(ids.size()), 2);
    const bool sorted = String(ids[0]) < String(ids[1]);
    CHECK(sorted);

    NETW_CHECK_EQ(int(Array(sealed[StringName("contacts")]).size()), 3);
    NETW_CHECK_EQ(int64_t(detail[StringName("solve_ordinal")]), 7);
    CHECK(bool(detail[StringName("breach")]));
    CHECK(bool(sealed[StringName("breach")]));

    const PackedStringArray outside
        = detail[StringName("outside_boundary_ids")];
    NETW_CHECK_EQ(int(outside.size()), 1);

    const Dictionary facts = sealed[StringName("topology_facts")];
    NETW_CHECK_EQ(int64_t(facts[StringName("collision_layer")]), 5);
    NETW_CHECK_EQ(int64_t(facts[StringName("body_mode")]), 0);
    CHECK_FALSE(base.has(StringName("collision_layer")));

    CHECK_FALSE(bool(detail[StringName("woke")]));
    sample[StringName("sleeping")] = true;
    pool->solve_evidence(slot, base, 8, sample, PackedStringArray());
    sample[StringName("sleeping")] = false;
    const Dictionary awake
        = pool->solve_evidence(slot, base, 9, sample, PackedStringArray());
    CHECK(bool(Dictionary(awake[StringName("detail")])[StringName("woke")]));

    pool->slot_unregister(mine);
    scene->remove_child(own);
    memdelete(own);
    scene->remove_child(ground);
    memdelete(ground);
    scene->remove_child(crate);
    memdelete(crate);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive][SceneTree] only an un-stepped "
    "dynamic body is a stand-in the closure cannot reproduce, and the body's "
    "own slot is never outside its own boundary"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Node *scene = netw::gd::scene_root();
    REQUIRE(scene != nullptr);

    Node *own = memnew(Node);
    own->set_name("Own");
    const Ref<netw::NetwEntity> mine = netw::NetwEntity::ensure(own);
    mine->set_entity_id(StringName("mine"));
    scene->add_child(own);
    const int64_t slot = pool->slot_register(mine);

    Node *peer = memnew(Node);
    peer->set_name("Peer");
    const Ref<netw::NetwEntity> theirs = netw::NetwEntity::ensure(peer);
    theirs->set_entity_id(StringName("theirs"));
    scene->add_child(peer);

    Node *ground = memnew(StaticBody3D);
    ground->set_name("Ground");
    scene->add_child(ground);

    Node *bare = memnew(Node);
    bare->set_name("Bare");
    scene->add_child(bare);

    PackedStringArray none;
    PackedStringArray joined;
    joined.append("theirs");

    CHECK_FALSE(pool->contact_breaches_boundary(slot, own, String(), none));
    CHECK_FALSE(pool->contact_breaches_boundary(slot, ground, String(), none));
    CHECK(pool->contact_breaches_boundary(slot, bare, String(), none));
    CHECK(pool->contact_breaches_boundary(slot, peer, String(), none));

    const Ref<netw::NetwPredictionHandle> declared = theirs->get_prediction();
    REQUIRE(declared.is_valid());
    declared->set_sim_mode(int(netw::NetwPredict::SIM_MODE_SPECULATIVE));
    CHECK_FALSE(pool->contact_breaches_boundary(slot, peer, String(), joined));

    declared->set_sim_mode(int(netw::NetwPredict::SIM_MODE_DISPLAY));
    CHECK(pool->contact_breaches_boundary(slot, peer, String(), joined));

    CHECK_FALSE(pool->contact_breaches_boundary(
        slot,
        bare,
        pool->collider_identity(bare),
        none
    ));

    NETW_CHECK_EQ(
        pool->witness_class_of(ground, pool->collider_identity(ground)),
        pool->witness_class(ground, true)
    );
    NETW_CHECK_EQ(
        pool->witness_class_of(ground, String()),
        pool->witness_class(ground, false)
    );

    pool->slot_unregister(mine);
    scene->remove_child(own);
    memdelete(own);
    scene->remove_child(peer);
    memdelete(peer);
    scene->remove_child(ground);
    memdelete(ground);
    scene->remove_child(bare);
    memdelete(bare);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive][SceneTree] a contacted body is "
    "classified by what it costs a closure to reproduce, and the declared "
    "support outranks every other answer about the same body"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Node *scene = netw::gd::scene_root();
    REQUIRE(scene != nullptr);

    Node *plain = memnew(Node);
    plain->set_name("Loose");
    const Ref<netw::NetwEntity> loose = netw::NetwEntity::ensure(plain);
    loose->set_entity_id(StringName("loose"));
    scene->add_child(plain);

    NETW_CHECK_EQ(
        pool->classify_collider(plain, String()),
        int(netw::NetwPredict::CONTACT_CLASS_UNPREDICTED_DYNAMIC)
    );

    pool->slot_register(loose);
    NETW_CHECK_EQ(
        pool->classify_collider(plain, String()),
        int(netw::NetwPredict::CONTACT_CLASS_PREDICTED_DYNAMIC)
    );

    NETW_CHECK_EQ(
        pool->classify_collider(plain, pool->collider_identity(plain)),
        int(netw::NetwPredict::CONTACT_CLASS_DECLARED_SUPPORT)
    );

    pool->slot_unregister(loose);
    NETW_CHECK_EQ(
        pool->classify_collider(plain, String()),
        int(netw::NetwPredict::CONTACT_CLASS_UNPREDICTED_DYNAMIC)
    );

    NETW_CHECK_EQ(
        pool->classify_collider(nullptr, String()),
        int(netw::NetwPredict::CONTACT_CLASS_UNPREDICTED_DYNAMIC)
    );

    Node *ground = memnew(StaticBody3D);
    ground->set_name("Ground");
    scene->add_child(ground);
    NETW_CHECK_EQ(
        pool->classify_collider(ground, String()),
        int(netw::NetwPredict::CONTACT_CLASS_OTHER_STATIC)
    );
    NETW_CHECK_EQ(
        pool->classify_collider(ground, pool->collider_identity(ground)),
        int(netw::NetwPredict::CONTACT_CLASS_DECLARED_SUPPORT)
    );

    Node *lift = memnew(AnimatableBody3D);
    lift->set_name("Lift");
    scene->add_child(lift);
    NETW_CHECK_EQ(
        pool->classify_collider(lift, String()),
        int(netw::NetwPredict::CONTACT_CLASS_KINEMATIC_PROXY)
    );

    RigidBody3D *crate = memnew(RigidBody3D);
    crate->set_name("Crate");
    scene->add_child(crate);
    NETW_CHECK_EQ(
        pool->classify_collider(crate, String()),
        int(netw::NetwPredict::CONTACT_CLASS_UNPREDICTED_DYNAMIC)
    );
    crate->set("freeze", true);
    NETW_CHECK_EQ(
        pool->classify_collider(crate, String()),
        int(netw::NetwPredict::CONTACT_CLASS_KINEMATIC_PROXY)
    );

    scene->remove_child(plain);
    memdelete(plain);
    scene->remove_child(ground);
    memdelete(ground);
    scene->remove_child(lift);
    memdelete(lift);
    scene->remove_child(crate);
    memdelete(crate);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive][SceneTree] a contact names the "
    "entity it belongs to, and a scene wrapper is a container rather than a "
    "body"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Node *scene = netw::gd::scene_root();
    REQUIRE(scene != nullptr);

    Node *plain = memnew(Node);
    plain->set_name("Plain");
    scene->add_child(plain);

    Node *body = memnew(Node);
    body->set_name("Body");
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(body);
    entity->set_entity_id(StringName("racer"));
    scene->add_child(body);

    Node *wrapper = memnew(Node);
    wrapper->set_name("Wrapper");
    const Ref<netw::NetwEntity> scenery = netw::NetwEntity::ensure(wrapper);
    scenery->set_entity_id(StringName("level"));
    scenery->set_declares_scene(true);
    scene->add_child(wrapper);

    CHECK(pool->contact_entity(plain).is_null());
    const bool named_the_body = pool->contact_entity(body) == entity;
    CHECK(named_the_body);
    CHECK(pool->contact_entity(wrapper).is_null());
    CHECK(pool->contact_entity(nullptr).is_null());

    const bool named_by_entity
        = pool->collider_identity(body) == String("entity:racer");
    CHECK(named_by_entity);
    const bool plain_is_a_path
        = pool->collider_identity(plain).begins_with("path:");
    CHECK(plain_is_a_path);
    const bool wrapper_is_a_path
        = pool->collider_identity(wrapper).begins_with("path:");
    CHECK(wrapper_is_a_path);
    const bool nothing_names_nothing
        = pool->collider_identity(nullptr) == String("object:");
    CHECK(nothing_names_nothing);

    scene->remove_child(plain);
    memdelete(plain);
    scene->remove_child(body);
    memdelete(body);
    scene->remove_child(wrapper);
    memdelete(wrapper);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a payload is laid out in declared "
    "order with a hole for every field it does not carry, and a tolerance "
    "nothing named reads as unmeasured rather than as zero"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const StringName field("position");

    Dictionary payload;
    payload[field] = godot::Vector2(2.0, 5.0);
    payload[StringName("unknown")] = 9.0;

    const Array columns = pool->state_columns_of(slot, payload);
    NETW_CHECK_EQ(int(columns.size()), 1);
    CHECK(columns[0] == godot::Vector2(2.0, 5.0));

    const Array absent = pool->state_columns_of(slot, Dictionary());
    NETW_CHECK_EQ(int(absent.size()), 1);
    CHECK(absent[0].get_type() == Variant::NIL);

    NETW_CHECK_EQ(int(pool->state_columns_of(slot + 9000, payload).size()), 0);

    Dictionary tolerances;
    tolerances[field] = 0.5;
    const PackedFloat64Array named
        = pool->tolerance_columns_of(slot, tolerances);
    NETW_CHECK_EQ(int(named.size()), 1);
    NETW_CHECK_CLOSE(double(named[0]), 0.5, 1e-9);

    const PackedFloat64Array unnamed
        = pool->tolerance_columns_of(slot, Dictionary());
    NETW_CHECK_EQ(int(unnamed.size()), 1);
    NETW_CHECK_CLOSE(double(unnamed[0]), -1.0, 1e-9);

    NETW_CHECK_EQ(
        int(pool->tolerance_columns_of(slot + 9000, tolerances).size()),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a write plan is read back by the "
    "declaration that keyed it, and a plan that does not exist is a skip "
    "rather than an empty write"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    const netw::RecoveryPlan absent
        = pool->recovery_of(slot, netw::predict::WritePlan());
    CHECK(absent.skip);
    CHECK(absent.restore.is_empty());
    CHECK(absent.write.is_empty());

    Array payload;
    payload.append(godot::Vector2(3.0, 4.0));
    const netw::predict::WritePlan planned
        = pool->align_reseed(slot, 4, payload, 9);

    const netw::RecoveryPlan named = pool->recovery_of(slot, planned);
    NETW_CHECK_EQ(named.skip, planned.skip);
    NETW_CHECK_EQ(named.teleport, planned.teleport);
    if (planned.restore.has(0)) {
        CHECK(named.restore.has(StringName("position")));
        CHECK(
            named.restore[StringName("position")] == planned.restore.values[0]
        );
    }

    const netw::RecoveryPlan closed = pool->recovery_of(slot + 9000, planned);
    CHECK(closed.skip);
    CHECK(closed.restore.is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a repaired field is charged to the "
    "ledger only where the field is causal, and armed only where a "
    "divergence named it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const StringName field("position");

    Dictionary deltas;
    deltas[field] = godot::Vector2(1.0, 0.0);
    deltas[StringName("unknown")] = 1.0;

    CHECK_FALSE(pool->ledger_armed(slot));
    pool->ledger_note_writes(slot, deltas);
    CHECK_FALSE(pool->ledger_armed(slot));

    Dictionary reported;
    reported[field] = 4.0;
    pool->note_divergence(slot, reported);
    pool->ledger_note_writes(slot, deltas);
    CHECK(pool->ledger_armed(slot));

    pool->ledger_note_writes(slot + 9000, deltas);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a restore names an operator the "
    "journal can carry or it is refused whole, writing nothing and booking "
    "nothing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const StringName target("P");

    REQUIRE(
        pool->open_episode(slot, 4, int(netw::NetwPredictJournal::CONTACT))
    );

    CHECK_FALSE(pool->apply_restore(
        slot,
        marked(1),
        int(netw::NetwPredictJournal::JOINT_REBASE) + 1,
        4,
        -1,
        target,
        3,
        0.01,
        false,
        false
    ));
    CHECK(pool->pending_provenance(slot).is_empty());

    CHECK_FALSE(pool->apply_restore(
        slot,
        marked(1),
        int(netw::NetwPredictJournal::REBASE_EXACT),
        -2,
        -1,
        target,
        3,
        0.01,
        false,
        false
    ));
    CHECK(pool->pending_provenance(slot).is_empty());

    CHECK_FALSE(pool->apply_restore(
        slot + 9000,
        marked(1),
        int(netw::NetwPredictJournal::REBASE_EXACT),
        4,
        -1,
        target,
        3,
        0.01,
        false,
        false
    ));

    CHECK(pool->apply_restore(
        slot,
        marked(2),
        int(netw::NetwPredictJournal::REBASE_EXACT),
        4,
        -1,
        target,
        3,
        0.01,
        false,
        false
    ));
    const Dictionary staged = pool->pending_provenance(slot);
    REQUIRE_FALSE(staged.is_empty());
    NETW_CHECK_EQ(int64_t(staged[StringName("basis")]), 4);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a restore stamps the provenance the "
    "next comparison judges it by, and an operator naming nothing stages no "
    "provenance at all"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const StringName target("P");

    pool->record_restore(
        slot,
        marked(1),
        int(netw::NetwPredictJournal::NONE),
        4,
        -1,
        target,
        3,
        0.01,
        false,
        false
    );
    CHECK(pool->pending_provenance(slot).is_empty());

    REQUIRE(
        pool->open_episode(slot, 4, int(netw::NetwPredictJournal::CONTACT))
    );
    pool->record_restore(
        slot,
        marked(2),
        int(netw::NetwPredictJournal::REBASE_EXACT),
        4,
        -1,
        target,
        3,
        0.01,
        false,
        false
    );

    const Dictionary staged = pool->pending_provenance(slot);
    REQUIRE_FALSE(staged.is_empty());
    NETW_CHECK_EQ(
        int(staged[StringName("operator")]),
        int(netw::NetwPredictJournal::REBASE_EXACT)
    );
    NETW_CHECK_EQ(int64_t(staged[StringName("basis")]), 4);
    NETW_CHECK_EQ(
        int64_t(staged[StringName("episode")]),
        pool->episode_stats(slot)[NetwPredictionEngine::STAT_EPISODE_ID]
    );
    NETW_CHECK_EQ(
        int64_t(staged[StringName("write_id")]),
        pool->episode_stats(
            slot
        )[NetwPredictionEngine::STAT_EPISODE_LAST_WRITE_ID]
    );

    const int64_t before = pool->episode_stats(
        slot
    )[NetwPredictionEngine::STAT_EPISODE_WRITE_COUNT];
    pool->record_restore(
        slot,
        marked(3),
        int(netw::NetwPredictJournal::REBASE_EXACT),
        5,
        -1,
        target,
        3,
        0.01,
        false,
        true
    );
    NETW_CHECK_EQ(
        pool->episode_stats(
            slot
        )[NetwPredictionEngine::STAT_EPISODE_WRITE_COUNT],
        before
    );

    pool->record_restore(
        slot + 9000,
        marked(4),
        int(netw::NetwPredictJournal::REBASE_EXACT),
        6,
        -1,
        target,
        3,
        0.01,
        false,
        false
    );
    NETW_CHECK_EQ(
        int64_t(pool->pending_provenance(slot)[StringName("basis")]),
        5
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a carry that is refused leaves the "
    "acknowledged value, the caller's own payload is never written through, "
    "and every field the fold touched is named back to the caller"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const StringName field("position");

    Dictionary payload;
    payload[field] = godot::Vector2(3.0, 4.0);

    const Dictionary unruled = pool->carry_payload(slot, payload, 1, 2.0, 0.01);
    NETW_CHECK_EQ(int(pool->carry_attempts(slot).size()), 0);
    CHECK(unruled[field] == godot::Vector2(3.0, 4.0));

    pool->set_carry(slot, field, callable_mp_static(&declared_rule));
    const Dictionary refused = pool->carry_payload(slot, payload, 1, 2.0, 0.01);

    const LocalVector<netw::predict::CarryAttempt> charged
        = pool->carry_attempts(slot);
    REQUIRE(charged.size() == 1);
    const netw::predict::CarryAttempt &only = charged[0];
    CHECK(only.field == field);
    NETW_CHECK_EQ(only.verdict, int(NetwPredictionEngine::CARRY_DECLINED));

    CHECK(refused[field] == godot::Vector2(3.0, 4.0));
    CHECK(payload[field] == godot::Vector2(3.0, 4.0));

    CHECK(pool->carry_payload(slot + 9000, payload, 1, 2.0, 0.01) == payload);
    NETW_CHECK_EQ(int(pool->carry_attempts(slot + 9000).size()), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a divergence is charged to the "
    "evidence that named it, and to nothing at all where none did"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const int unknown = int(netw::NetwPredictJournal::UNKNOWN);

    NETW_CHECK_EQ(pool->attribution_for(slot + 9000, 1), unknown);
    NETW_CHECK_EQ(pool->attribution_for(slot, 1), unknown);

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord drive = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    const int64_t opened = drive.transition;
    pool->close_drive(slot, opened, 0, 0, 0, 0);
    NETW_CHECK_EQ(pool->attribution_for(slot, opened), unknown);

    pool->note_attribution(
        slot,
        int(netw::NetwPredictJournal::ENVIRONMENT),
        opened
    );
    NETW_CHECK_EQ(
        pool->attribution_for(slot, opened),
        int(netw::NetwPredictJournal::ENVIRONMENT)
    );
    NETW_CHECK_EQ(pool->attribution_for(slot, opened + 1), unknown);

    pool->mark_attribution(
        slot,
        opened,
        int(netw::NetwPredictJournal::CONTACT)
    );
    NETW_CHECK_EQ(
        pool->attribution_for(slot, opened),
        int(netw::NetwPredictJournal::CONTACT)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a divergence answered with no write "
    "names why, and an agreeing comparison is never refused"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);
    const int none = int(netw::NetwPredict::VERDICT_REASON_NONE);

    NETW_CHECK_EQ(pool->refuse_recovery(slot, false), none);
    NETW_CHECK_EQ(pool->refuse_recovery(slot, true), none);
    NETW_CHECK_EQ(pool->refuse_recovery(slot + 9000, true), none);

    REQUIRE(
        pool->open_episode(slot, 3, int(netw::NetwPredictJournal::CONTACT))
    );
    pool->record_episode_write(
        slot,
        int(netw::NetwPredictJournal::TRANSPORT_DELTA),
        3,
        0,
        godot::StringName("body"),
        3,
        0,
        false,
        false
    );
    NETW_CHECK_EQ(
        pool->refuse_recovery(slot, true),
        int(netw::NetwPredict::VERDICT_REASON_TRANSPORT_PENDING)
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_TRANSPORT_PENDING)
    );

    pool->note_verdict_reason(slot, none);
    NETW_CHECK_EQ(pool->refuse_recovery(slot, false), none);
    NETW_CHECK_EQ(pool->verdict_reason_of(slot), none);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a settled comparison writes its "
    "error onto the row it judged, and a transition with no row is not a "
    "place to write one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    pool->mark_aligned_error(slot, 40, 2.5);
    pool->mark_aligned_error(slot + 9000, 40, 2.5);

    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord drive = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    const int64_t opened = drive.transition;
    pool->close_drive(slot, opened, 0, 0, 0, 0);

    const int at = pool->journal_slot_of(slot, opened);
    REQUIRE(at >= 0);
    NETW_CHECK_EQ(pool->journal_aligned_error_at(slot, at), 0.0);
    pool->mark_aligned_error(slot, opened, 2.5);
    NETW_CHECK_EQ(pool->journal_aligned_error_at(slot, at), 2.5);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] opening a comparison reads the four "
    "facts it is judged against before anything consumes them, and a stream "
    "with no gain edge yet is refused rather than compared"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    const PackedInt64Array absent = pool->open_comparison(slot + 9000, 1);
    NETW_CHECK_EQ(
        int(absent.size()),
        int(NetwPredictionEngine::COMPARE_COLUMN_COUNT)
    );
    NETW_CHECK_EQ(
        absent[NetwPredictionEngine::COMPARE_DOMAIN],
        int(netw::NetwPredictJournal::OUT_OF_DOMAIN)
    );
    NETW_CHECK_EQ(absent[NetwPredictionEngine::COMPARE_EPISODE_STATE], -1);

    pool->note_divergence(slot, Dictionary());
    const PackedInt64Array unarmed = pool->open_comparison(slot, 1);
    NETW_CHECK_EQ(unarmed[NetwPredictionEngine::COMPARE_RECONSTRUCTED], 0);
    NETW_CHECK_EQ(
        unarmed[NetwPredictionEngine::COMPARE_DOMAIN],
        int(netw::NetwPredictJournal::OUT_OF_DOMAIN)
    );
    NETW_CHECK_EQ(unarmed[NetwPredictionEngine::COMPARE_ROW_FLAGS], 0);
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_AWAITING_RECONSTRUCTION)
    );

    pool->set_stream_reconstructed(slot, true);
    pool->record_input(slot, 1, 1);
    const netw::predict::DriveRecord drive = pool->open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    const int64_t opened_row = drive.transition;
    pool->close_drive(slot, opened_row, 0, 0, 0, 0);
    pool->mark_domain(
        slot,
        opened_row,
        int(netw::NetwPredictJournal::IN_DOMAIN)
    );

    const PackedInt64Array armed = pool->open_comparison(slot, opened_row);
    NETW_CHECK_EQ(armed[NetwPredictionEngine::COMPARE_RECONSTRUCTED], 1);
    NETW_CHECK_EQ(
        armed[NetwPredictionEngine::COMPARE_DOMAIN],
        int(netw::NetwPredictJournal::IN_DOMAIN)
    );
    NETW_CHECK_EQ(
        armed[NetwPredictionEngine::COMPARE_ROW_FLAGS],
        pool->journal_row(slot, opened_row).flags
    );
    NETW_CHECK_EQ(
        armed[NetwPredictionEngine::COMPARE_EPISODE_STATE],
        pool->episode_state(slot)
    );
    NETW_CHECK_EQ(
        armed[NetwPredictionEngine::COMPARE_PROBATION],
        pool->probation_pending(slot) ? 1 : 0
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_AWAITING_RECONSTRUCTION)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] an acknowledgement frame carries "
    "every field of every record through an encode and a decode, and the raw "
    "fingerprint only where the evidence mask claims one"
) {
    netw::predict::AckFrame frame;
    frame.epoch = 3;
    frame.base = 40;
    for (int at = 0; at < 4; ++at) {
        netw::predict::AckEvidenceWire record;
        record.pre_fp = at;
        record.c_hash = at;
        record.e_digest = at;
        record.post_fp = at;
        record.topo_fp = at;
        record.witness_fp = at;
        record.pre_pose_fp = 1;
        record.pre_momentum_fp = 2;
        record.pre_controller_fp = 3;
        record.post_pose_fp = 1;
        record.post_momentum_fp = 2;
        record.post_controller_fp = 3;
        record.raw_fp = at + 1;
        record.evidence_mask
            = uint8_t(at % 2 == 0 ? int(netw::predict::EVIDENCE_RAW) : 0);
        record.flags = netw::predict::ack_flags(0, at & 3);
        frame.records.push_back(record);
    }
    NETW_CHECK_EQ(int(frame.records.size()), 4);

    netw::predict::AckFrame decoded;
    REQUIRE(
        netw::predict::decode_ack(netw::predict::encode_ack(frame), decoded)
    );
    NETW_CHECK_EQ(int(decoded.epoch), int(frame.epoch));
    NETW_CHECK_EQ(int64_t(decoded.base), int64_t(frame.base));
    REQUIRE(decoded.records.size() == frame.records.size());
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        const netw::predict::AckEvidenceWire &sent = frame.records[at];
        const netw::predict::AckEvidenceWire &got = decoded.records[at];
        NETW_CHECK_EQ(int(got.evidence_mask), int(sent.evidence_mask));
        NETW_CHECK_EQ(got.pre_fp, sent.pre_fp);
        NETW_CHECK_EQ(got.c_hash, sent.c_hash);
        NETW_CHECK_EQ(got.e_digest, sent.e_digest);
        NETW_CHECK_EQ(got.post_fp, sent.post_fp);
        NETW_CHECK_EQ(got.topo_fp, sent.topo_fp);
        NETW_CHECK_EQ(got.witness_fp, sent.witness_fp);
        NETW_CHECK_EQ(got.pre_pose_fp, sent.pre_pose_fp);
        NETW_CHECK_EQ(got.pre_momentum_fp, sent.pre_momentum_fp);
        NETW_CHECK_EQ(got.pre_controller_fp, sent.pre_controller_fp);
        NETW_CHECK_EQ(got.post_pose_fp, sent.post_pose_fp);
        NETW_CHECK_EQ(got.post_momentum_fp, sent.post_momentum_fp);
        NETW_CHECK_EQ(got.post_controller_fp, sent.post_controller_fp);
        const bool carries_raw
            = (sent.evidence_mask & int(netw::predict::EVIDENCE_RAW)) != 0;
        NETW_CHECK_EQ(got.raw_fp, carries_raw ? sent.raw_fp : 0);
        NETW_CHECK_EQ(int(got.flags), int(sent.flags));
        NETW_CHECK_EQ(netw::predict::ack_witness_class(got.flags), int(at) & 3);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a rewire re-keys the tape and drops "
    "every cursor expressed in the numbering it retired, and keeps the two "
    "facts that outlive it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    pool->set_stream_reconstructed(slot, true);
    pool->set_ack_domain_confirmed(slot, true);
    pool->record_input(slot, 4, 4);
    pool->tape_author(slot, 11, true);
    pool->set_command_epoch(slot, 7);
    pool->set_owner_ack_floor(slot, 3);
    const int64_t epoch = pool->tape_epoch_of(slot);

    pool->reset_for_rewire(slot, false, true, true, marked(9));

    NETW_CHECK_EQ(pool->tape_epoch_of(slot), (epoch + 1) & 0xFF);
    NETW_CHECK_EQ(pool->next_tape_entry_index_of(slot), 0);
    NETW_CHECK_EQ(pool->last_driven_entry_index_of(slot), -1);
    NETW_CHECK_EQ(pool->command_epoch_of(slot), -1);
    NETW_CHECK_EQ(pool->owner_ack_floor_of(slot), -1);
    NETW_CHECK_EQ(pool->latest_input_tick_of(slot), -1);
    NETW_CHECK_EQ(pool->last_recorded_input_tick_of(slot), -1);
    NETW_CHECK_EQ(pool->attribution_of(slot), 0);
    NETW_CHECK_EQ(pool->attributed_transition_of(slot), -1);
    NETW_CHECK_EQ(
        int64_t(pool->stall_input_of(slot)[godot::StringName("mark")]),
        9
    );
    CHECK(pool->raw_fingerprints_of(slot));

    CHECK(pool->stream_reconstructed_of(slot));
    CHECK(pool->ack_domain_confirmed_of(slot));

    pool->reset_for_rewire(slot, true, false, false, Dictionary());
    CHECK_FALSE(pool->stream_reconstructed_of(slot));
    CHECK_FALSE(pool->ack_domain_confirmed_of(slot));
    CHECK_FALSE(pool->raw_fingerprints_of(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] an authoritative frame is admitted "
    "or named for why it was not, and every admission starts from a cleared "
    "reason so no frame inherits the one before it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    NETW_CHECK_EQ(
        pool->admit_state(slot + 9000, 1),
        int(NetwPredictionEngine::ADMIT_CLOSED)
    );

    pool->note_verdict_reason(
        slot,
        int(netw::NetwPredict::VERDICT_REASON_DECLINED)
    );
    NETW_CHECK_EQ(
        pool->admit_state(slot, 1),
        int(NetwPredictionEngine::ADMIT_PROCEED)
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_NONE)
    );

    pool->adopt_alignment(slot, 9, 12);
    NETW_CHECK_EQ(
        pool->admit_state(slot, 12),
        int(NetwPredictionEngine::ADMIT_RESEED_IGNORED)
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_RESEED_IGNORED)
    );
    NETW_CHECK_EQ(
        pool->admit_state(slot, 13),
        int(NetwPredictionEngine::ADMIT_PROCEED)
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_NONE)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a reseed still waiting on its epoch "
    "reports the wait, and a confirmed one hands the alignment back to the "
    "caller that staged it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    Array payload;
    payload.append(godot::Vector2(1.0, 0.0));
    pool->enter_quarantine(slot, 4, true, 0, false);
    for (int64_t basis = 5; basis <= 7; ++basis) {
        pool->quarantine_state(slot, basis, basis, payload, true);
        pool->quarantine_witness(slot, basis, 1);
    }
    REQUIRE(pool->reseed_align_pending(slot));
    REQUIRE_FALSE(pool->reseed_epoch_confirmed(slot));

    NETW_CHECK_EQ(
        pool->admit_state(slot, 8),
        int(NetwPredictionEngine::ADMIT_REALIGN_PENDING)
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_REALIGN_PENDING)
    );

    pool->confirm_reseed_epoch(slot);
    NETW_CHECK_EQ(
        pool->admit_state(slot, 8),
        int(NetwPredictionEngine::ADMIT_REALIGN)
    );
    NETW_CHECK_EQ(
        pool->verdict_reason_of(slot),
        int(netw::NetwPredict::VERDICT_REASON_NONE)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] the schedule chooses which lane a "
    "comparison is taken from, and a frame with nothing to compare refuses "
    "rather than comparing against an empty state"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t framed = framed_slot(pool);

    CHECK(pool->select_comparison(framed, 0).is_empty());
    pool->tape_author(framed, 11, true);
    CHECK(pool->select_comparison(framed, 0).is_empty());

    pool->entry_history(framed)->record_state(1, marked(42));
    const Dictionary chosen = pool->select_comparison(framed, 0);
    REQUIRE_FALSE(chosen.is_empty());
    NETW_CHECK_EQ(int64_t(chosen[godot::StringName("label")]), 11);
    const Dictionary predicted = chosen[godot::StringName("predicted")];
    NETW_CHECK_EQ(int64_t(predicted[godot::StringName("mark")]), 42);
    NETW_CHECK_EQ(pool->compare_staleness_of(framed), 0);

    const int64_t ticked = scheduled_slot(pool, netw::Schedule::TICK);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(ticked, lane);

    const Dictionary unstated = pool->select_comparison(ticked, 5);
    REQUIRE_FALSE(unstated.is_empty());
    NETW_CHECK_EQ(int64_t(unstated[godot::StringName("label")]), 5);
    CHECK(Dictionary(unstated[godot::StringName("predicted")]).is_empty());
    NETW_CHECK_EQ(pool->compare_staleness_of(ticked), -1);

    lane->record_state(3, marked(7));
    const Dictionary stale = pool->select_comparison(ticked, 5);
    NETW_CHECK_EQ(
        int64_t(Dictionary(
            stale[godot::StringName("predicted")]
        )[godot::StringName("mark")]),
        7
    );
    NETW_CHECK_EQ(pool->compare_staleness_of(ticked), 3);

    CHECK(pool->select_comparison(framed + 9000, 0).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a recorded pass opens its own row "
    "and records the input for it, and a caller that already recorded one is "
    "taken at its word"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = framed_slot(pool);

    const Dictionary refused = pool->record_drive(
        slot,
        -1,
        1,
        int(netw::DriveKind::NONE),
        marked(1),
        -1,
        false
    );
    CHECK(refused.is_empty());
    NETW_CHECK_EQ(stat_of(pool, slot, NetwPredictionEngine::STAT_DRIVE_SEQ), 0);

    const Dictionary drove = pool->record_drive(
        slot,
        -1,
        5,
        int(netw::DriveKind::NONE),
        marked(5),
        1,
        false
    );
    REQUIRE_FALSE(drove.is_empty());
    const int64_t opened = drove[godot::StringName("transition")];
    CHECK(opened >= 0);
    NETW_CHECK_EQ(int64_t(drove[godot::StringName("label")]), 5);
    NETW_CHECK_EQ(
        int(drove[godot::StringName("kind")]),
        int(netw::DriveKind::FRESH)
    );
    CHECK(bool(drove[godot::StringName("fresh")]));
    CHECK(pool->journal_has(slot, opened));
    NETW_CHECK_EQ(
        pool->journal_row(slot, opened).c_hash,
        netw::NetwPredictJournal::fnv1a(
            pool->canonical_input_bytes(slot, marked(5))
        )
    );
    NETW_CHECK_EQ(
        pool->drive_cursors(
            slot
        )[NetwPredictionEngine::CURSOR_LATEST_INPUT_TICK],
        5
    );
    pool->close_drive(slot, opened, 0, 0, 0, 0);

    pool->record_drive(
        slot,
        -1,
        6,
        int(netw::DriveKind::NONE),
        marked(6),
        2,
        false,
        true
    );
    NETW_CHECK_EQ(
        pool->drive_cursors(
            slot
        )[NetwPredictionEngine::CURSOR_LATEST_INPUT_TICK],
        5
    );
}

} // namespace TestNetwPredictDriveLaws

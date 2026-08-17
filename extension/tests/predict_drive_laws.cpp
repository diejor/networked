// Laws for the drive path an engine OWNS: the tape it authors, the journal row
// each pass opens and closes, and the horizon that stops it speculating past
// what authority has confirmed.
//
// These are the questions `predict_lane_laws.cpp` cannot ask. A kernel is a
// function and has no memory, so a law about what an engine remembers between
// two ticks has to stand on a driver that holds an engine.

#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "support/netw_cells.h"
#include "support/scenario_run.h"

namespace TestNetwPredictDriveLaws {

using namespace netw_test;

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

// Long enough that the speculative span reaches its bound, which no scenario
// shorter than the bound can show anything about.
Scenario unacknowledged_lane() {
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
    // A correction writes the body from outside the engine, and a write no
    // operator accounts for IS a chain break. Only a lane nothing corrected
    // says anything about whether the drive path chains on its own.
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
    { "L-CLOSE",
      "every journal row a drive opened is closed by that drive, and there is "
      "one row per drive that ran",
      law_every_opened_row_closes },
    { "L-CHAIN",
      "a lane nothing corrected enters each transition holding the state the "
      "previous one produced",
      law_an_uncorrected_lane_chains },
    { "L-HORIZON",
      "a lane authority never acknowledges stops opening transitions at the "
      "speculative bound",
      law_speculation_stops_at_the_horizon },
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the drive laws hold across the corpus"
) {
    const Scenario CORPUS[] = {
        driven_lane(),
        unacknowledged_lane(),
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

// Each pair names the one scenario carrying the evidence the law reads.
// L-HORIZON cannot be broken on a lane shorter than the bound, and L-CHAIN
// cannot be broken on a lane a correction already broke.
const RedProof RED_PROOFS[] = {
    { LAWS[0], driven_lane, PLANT_SKIP_CLOSE },
    { LAWS[1], driven_lane, PLANT_FORGE_PRE },
    { LAWS[2], unacknowledged_lane, PLANT_ALWAYS_ACK },
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] every drive law breaks against its "
    "plant"
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
    const Ref<NetwPredictionEngine> &p_pool,
    netw::Schedule p_schedule
) {
    Ref<netw::NetwPredictDeclaration> fields;
    fields.instantiate();
    fields->append_field(
        godot::StringName("position"),
        int(netw::predict::PropertyClass::CAUSAL),
        godot::StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false
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

int64_t framed_slot(const Ref<NetwPredictionEngine> &p_pool) {
    return scheduled_slot(p_pool, netw::Schedule::FRAME);
}

Dictionary marked(int64_t p_value) {
    Dictionary out;
    out[godot::StringName("mark")] = p_value;
    return out;
}

int64_t stat_of(
    const Ref<NetwPredictionEngine> &p_pool,
    int64_t p_slot,
    int p_column
) {
    return p_pool->drive_stats(p_slot)[p_column];
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a refused pass is counted by the "
    "reason it was refused"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    // A frame the clock held bought no simulated time, so it opens no
    // transition and is counted as a clamp rather than as a drive.
    const Ref<netw::NetwPredictDrive> held = pool->open_drive(
        slot, Dictionary(), 1, 1, 1.0 / 60.0, 1, false, 0, 0, 0, 0
    );
    REQUIRE(held.is_valid());
    CHECK(held->clamped());
    CHECK_FALSE(held->ran());
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_AUTHORING_CLAMPED),
        1
    );
    NETW_CHECK_EQ(stat_of(pool, slot, NetwPredictionEngine::STAT_DRIVE_SEQ), 0);

    // A second frame at a tick already transitioned is the same refusal.
    pool->record_input(slot, 1, 1);
    const Ref<netw::NetwPredictDrive> first = pool->open_drive(
        slot, Dictionary(), 1, 1, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    REQUIRE(first->ran());
    pool->close_drive(slot, first->transition(), 0, 0, 0, 0);
    const Ref<netw::NetwPredictDrive> repeat = pool->open_drive(
        slot, Dictionary(), 1, 2, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    CHECK(repeat->clamped());
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_AUTHORING_CLAMPED),
        2
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] speculation past the confirmed "
    "horizon is held and counted"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    int64_t tick = 1;
    int held = 0;
    for (int at = 0; at < netw::predict::ACK_AGE_MAX + 4; ++at) {
        pool->record_input(slot, tick, int32_t(tick));
        const Ref<netw::NetwPredictDrive> drive = pool->open_drive(
            slot, Dictionary(), tick, tick, 1.0 / 60.0, 1, true, 0, 0, 0, 0
        );
        REQUIRE(drive.is_valid());
        if (drive->held()) {
            held += 1;
        } else if (drive->ran()) {
            pool->close_drive(slot, drive->transition(), 0, 0, 0, 0);
        }
        tick += 1;
    }

    CHECK(held > 0);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_SPECULATION_HELD),
        held
    );
    // Input capture continues while the body holds, so the horizon bounds the
    // speculation and never the command lane.
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
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    pool->record_input(slot, 1, 1);
    const Ref<netw::NetwPredictDrive> first = pool->open_drive(
        slot, Dictionary(), 1, 1, 1.0 / 60.0, 1, true, 11, 0, 0, 0
    );
    REQUIRE(first->ran());
    // The post state this transition ENDED at, which the next one's pre state
    // has to equal or the two did not run back to back.
    pool->close_drive(slot, first->transition(), 77, 0, 0, 0);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_CHAIN_BREAKS),
        0
    );

    pool->record_input(slot, 2, 2);
    const Ref<netw::NetwPredictDrive> broken = pool->open_drive(
        slot, Dictionary(), 2, 4, 1.0 / 60.0, 1, true, 78, 0, 0, 0
    );
    REQUIRE(broken->ran());
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_CHAIN_BREAKS),
        1
    );

    // Three physics frames advanced across a transition declared to span one,
    // so this peer integrated its solver by an amount no transition accounts
    // for and its predictions carry an error the compare cannot attribute.
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
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    pool->record_input(slot, 1, 1);
    const Ref<netw::NetwPredictDrive> first = pool->open_drive(
        slot, Dictionary(), 1, 1, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    REQUIRE(first->ran());
    pool->close_drive(slot, first->transition(), 0, 0, 0, 0);

    pool->record_input(slot, 2, 2);
    const Ref<netw::NetwPredictDrive> stalled = pool->open_drive(
        slot, Dictionary(), 2, 500, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    REQUIRE(stalled->ran());
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
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    int64_t tick = 1;
    for (int at = 0; at < 6; ++at) {
        pool->record_input(slot, tick, int32_t(tick));
        const Ref<netw::NetwPredictDrive> drive = pool->open_drive(
            slot, Dictionary(), tick, tick, 1.0 / 60.0, 1, true, 0, 0, 0, 0
        );
        REQUIRE(drive->ran());
        pool->close_drive(slot, drive->transition(), 0, 0, 0, 0);
        tick += 1;
    }
    // The span is measured when a pass asks, not continuously: `open_drive`
    // refreshes it BEFORE the entry it is about to author, so a caller reading
    // it after a drive is reading the span as of that drive's start.
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        5
    );
    pool->refresh_ack_age(slot);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        6
    );

    // The frontier moves for a transition this slot may hold no row for: an
    // ack run names transitions, and a lane the pool does not decode is still
    // a proof. Measuring from the newest ROW instead would keep speculating
    // past what authority has confirmed.
    NETW_CHECK_EQ(pool->mark_authority_ack(slot, 3), 2);
    NETW_CHECK_EQ(
        stat_of(pool, slot, NetwPredictionEngine::STAT_ACK_AGE_TICKS),
        2
    );

    // The frontier never retreats, so a late frame from an older ack cannot
    // re-open a horizon a newer one already closed.
    NETW_CHECK_EQ(pool->mark_authority_ack(slot, 1), 2);
    NETW_CHECK_EQ(pool->mark_authority_ack(slot, 5), 0);
    NETW_CHECK_EQ(pool->mark_authority_ack(slot + 9000, 5), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a clock that jumps past the "
    "contiguous lane re-keys the tape, and a frame transitions once"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    const int64_t epoch
        = pool->drive_cursors(slot)[NetwPredictionEngine::CURSOR_TAPE_EPOCH];
    pool->tape_reset(slot, epoch + 1);
    PackedInt64Array cursors = pool->drive_cursors(slot);
    NETW_CHECK_EQ(
        cursors[NetwPredictionEngine::CURSOR_TAPE_EPOCH],
        epoch + 1
    );
    // A reset re-keys every cursor with the epoch, because a transition is
    // injective only within one and a retained position would name a
    // transition the new epoch is about to reuse.
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_NEXT_TAPE_ENTRY], 0);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_LAST_DRIVEN_ENTRY], -1);
    NETW_CHECK_EQ(cursors[NetwPredictionEngine::CURSOR_LATEST_INPUT_TICK], -1);
    NETW_CHECK_EQ(
        cursors[NetwPredictionEngine::CURSOR_LAST_FRAME_TRANSITION_TICK],
        -1
    );

    pool->record_input(slot, 1, 1);
    const Ref<netw::NetwPredictDrive> drive = pool->open_drive(
        slot, Dictionary(), 7, 1, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    REQUIRE(drive->ran());
    pool->close_drive(slot, drive->transition(), 0, 0, 0, 0);
    cursors = pool->drive_cursors(slot);
    // The frame's TICK is what a second frame at the same tick is refused
    // against, and it is not the transition the entry was authored at.
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
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    // An empty tape spans nothing, which is the absence a caller walking the
    // span already handles rather than a range of one.
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

    // The entry is a tape entry and nothing more: an authored command opens no
    // transition, so the journal must not answer for one.
    CHECK_FALSE(pool->journal_has(slot, 0));

    // A re-key at the contiguous tick keeps the ring, and one past it opens a
    // new epoch and drops what the previous numbering held.
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

    // A closed slot answers rather than writing somewhere, because a caller
    // reading a span it did not open would walk a range of a live entity's.
    NETW_CHECK_EQ(pool->tape_span(slot + 9000)[0], -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] the journal answers for the "
    "transitions it holds and never for one it has not seen"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    // A slot that has driven nothing holds nothing. Answering yes here would
    // tell a caller the pool has a row behind every number it can name.
    CHECK_FALSE(pool->journal_has(slot, 0));
    CHECK_FALSE(pool->journal_has(slot, 4));

    pool->record_input(slot, 1, 1);
    const Ref<netw::NetwPredictDrive> drive = pool->open_drive(
        slot, Dictionary(), 1, 1, 1.0 / 60.0, 1, true, 0, 0, 0, 0
    );
    REQUIRE(drive->ran());
    const int64_t at = drive->transition();
    CHECK(pool->journal_has(slot, at));
    CHECK_FALSE(pool->journal_has(slot, at + 1));
    CHECK_FALSE(pool->journal_has(slot + 9000, at));

    // The epoch re-keys the journal, so a transition the previous one held is
    // one the new one has not seen, whatever its number.
    pool->tape_reset(slot, 9);
    CHECK_FALSE(pool->journal_has(slot, at));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] an acknowledged transition retires "
    "the entry book by index and the input lane by the label it authored"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
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

    // Entry 1 is acknowledged, so entry 0 and the label entry 0 authored are
    // both unreachable. The two numberings are unrelated: acknowledging entry
    // 1 retires input tick 100, not input tick 0.
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

    // A TICK transition IS its tick, so the acknowledgement retires the lane
    // at the number it names and there is no second book to reach.
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
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    pool->tape_author(slot, 10, true);
    pool->tape_author(slot, 11, false);
    pool->tape_author(slot, 12, true);
    lane->record_input(10, marked(10));
    lane->record_input(11, marked(11));
    lane->record_input(12, marked(12));

    godot::TypedArray<netw::NetwPredictReplayEntry> entries
        = pool->replay_entries(slot, -1);
    REQUIRE(entries.size() == 3);
    const Ref<netw::NetwPredictReplayEntry> repeated = entries[1];
    NETW_CHECK_EQ(repeated->index(), 1);
    NETW_CHECK_EQ(repeated->label(), 11);
    // The owner ran the previous sample again rather than authoring a new one,
    // so the command filed under this entry's own label is not the one it ran.
    NETW_CHECK_EQ(int64_t(repeated->input()[godot::StringName("mark")]), 10);

    const Ref<netw::NetwPredictReplayEntry> fresh = entries[2];
    NETW_CHECK_EQ(fresh->index(), 2);
    NETW_CHECK_EQ(int64_t(fresh->input()[godot::StringName("mark")]), 12);

    // A basis inside the tape seeds the carry from the command the basis entry
    // itself ran, so the first repeated entry past it is not left empty.
    entries = pool->replay_entries(slot, 0);
    REQUIRE(entries.size() == 2);
    const Ref<netw::NetwPredictReplayEntry> first = entries[0];
    NETW_CHECK_EQ(first->index(), 1);
    NETW_CHECK_EQ(int64_t(first->input()[godot::StringName("mark")]), 10);

    CHECK(pool->replay_entries(slot + 9000, -1).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a tick tier walks every tick past "
    "the basis, and reads its states out of the lane"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = scheduled_slot(pool, netw::Schedule::TICK);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    lane->record_input(5, marked(5));
    lane->record_input(7, marked(7));
    pool->record_input(slot, 7, 0);

    // A tick the owner recorded no command at is still a transition it drove,
    // so the walk is over the ticks rather than over the recorded commands.
    const godot::TypedArray<netw::NetwPredictReplayEntry> entries
        = pool->replay_entries(slot, 4);
    REQUIRE(entries.size() == 3);
    for (int at = 0; at < 3; at += 1) {
        const Ref<netw::NetwPredictReplayEntry> entry = entries[at];
        NETW_CHECK_EQ(entry->index(), 5 + at);
        NETW_CHECK_EQ(entry->label(), 5 + at);
    }
    const Ref<netw::NetwPredictReplayEntry> silent = entries[1];
    CHECK(silent->input().is_empty());

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
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    // Both books hold an entry at 3, under two unrelated numberings: the entry
    // book is keyed by tape entry and the lane by tick.
    pool->entry_history(slot)->record_state(3, marked(3));
    lane->record_state(3, marked(99));

    NETW_CHECK_EQ(int64_t(pool->state_before(slot, 3)[godot::StringName("mark")]), 3);

    // A reconfigure names the axes and not the history, and the pool is asked
    // to configure a slot on every drive. Minting a book here would empty it
    // under a caller writing into the one it already holds.
    const Ref<netw::NetwTimeline> held = pool->entry_history(slot);
    pool->configure(
        slot,
        int(netw::Schedule::FRAME),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    CHECK(pool->entry_history(slot) == held);
    NETW_CHECK_EQ(int64_t(pool->state_before(slot, 3)[godot::StringName("mark")]), 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Drive] a tape epoch mints a fresh entry "
    "book, and the bound input lane survives it"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = framed_slot(pool);

    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);
    lane->record_input(100, marked(100));
    pool->entry_history(slot)->record_state(0, marked(0));

    // The entry index is injective only within an epoch, so a state the
    // previous numbering filed under 0 would answer for the transition the new
    // one is about to open there.
    pool->tape_reset(slot, 9);
    const Ref<netw::NetwTimeline> entries = pool->entry_history(slot);
    REQUIRE(entries.is_valid());
    CHECK(entries->state_at(0).is_empty());

    // The lane is keyed by the tick the owner authored at and is shared with
    // whatever else records into it, so a re-key of this slot's transitions
    // cannot be allowed to drop it.
    CHECK(lane->has_input_at(100));

    CHECK(pool->entry_history(slot + 9000).is_null());
}

} // namespace TestNetwPredictDriveLaws

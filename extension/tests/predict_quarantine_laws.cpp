#include "support/netw_test.h"

#include "netw/predict/drive.hpp"
#include "netw/predict/engine.hpp"

namespace TestNetwPredictQuarantineLaws {

using namespace godot;
using namespace netw::predict;
using netw::NetwPredictionEngine;

StateRow state(double p_value) {
    StateRow out;
    out.resize(1);
    out.set(0, p_value);
    return out;
}

StateRow moving_state(double p_position, double p_velocity) {
    StateRow out;
    out.resize(2);
    out.set(0, p_position);
    out.set(1, p_velocity);
    return out;
}

Wiring moving_wiring() {
    LocalVector<FieldDecl> fields;
    FieldDecl position;
    position.key = StringName("position");
    position.carry_channel = StringName("velocity");
    fields.push_back(position);
    FieldDecl velocity;
    velocity.key = StringName("velocity");
    fields.push_back(velocity);
    return compile(fields);
}

void append_row(
    Journal &r_journal,
    int64_t p_transition,
    Attribution p_attribution
) {
    JournalOpen row;
    row.pre_fp = int32_t(p_transition + 1);
    r_journal.open(p_transition, row);
    r_journal
        .close(p_transition, int32_t(p_transition + 2), FamilyFingerprints());
    r_journal.mark_domain(p_transition, Domain::OUT_OF_DOMAIN);
    r_journal.mark_attribution(p_transition, p_attribution);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] resume scales from ack age"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    NETW_CHECK_EQ(quarantine.target, 3);
    quarantine.enter(6, 1, true);
    NETW_CHECK_EQ(quarantine.target, 12);
    quarantine.enter(64, 4, true);
    NETW_CHECK_EQ(quarantine.target, 256);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] distinct coherent rows reseed"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    for (int64_t basis = 1; basis <= 3; ++basis) {
        quarantine.admit_state(basis, basis, state(basis), true);
        const QuarantinePlan plan
            = quarantine.admit_witness(basis, WITNESS_SUPPORT);
        CHECK_EQ(plan.ready, basis == 3);
    }
    CHECK_FALSE(quarantine.latched);
    CHECK(quarantine.align_pending);
    NETW_CHECK_EQ(quarantine.reseed_basis, 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] dirty and gaps reset proof"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    quarantine.admit_witness(1, 4);
    quarantine.admit_witness(2, WITNESS_STATIC);
    quarantine.admit_witness(4, WITNESS_SUPPORT);
    NETW_CHECK_EQ(quarantine.clean_run, 1);
    quarantine.admit_witness(5, 0);
    NETW_CHECK_EQ(quarantine.clean_run, 2);
    CHECK(quarantine.latched);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] stale unknown seed is bounded"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    quarantine.target = 2;
    quarantine.admit_state(1, 3, state(3.0), true);
    for (int64_t basis = 40; basis <= 41; ++basis) {
        quarantine.admit_witness(basis, 0);
    }
    CHECK(quarantine.latched);
    QuarantinePlan plan;
    for (int64_t basis = 42; basis < 42 + QUARANTINE_RUN_CAP; ++basis) {
        plan = quarantine.admit_witness(basis, 0);
    }
    CHECK(plan.ready);
    NETW_CHECK_EQ(plan.basis, 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] known dirty seed is skipped"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    quarantine.target = 2;
    quarantine.admit_witness(2, 0);
    quarantine.admit_state(1, 2, state(2.0), true);
    quarantine.admit_witness(3, 4);
    quarantine.admit_state(2, 3, state(3.0), true);
    quarantine.admit_witness(40, 0);
    quarantine.admit_witness(41, 0);
    quarantine.wait_anchor = 1;
    quarantine.last_basis = 1 + QUARANTINE_RUN_CAP;
    const QuarantinePlan plan = quarantine.admit_witness(41, 0);
    CHECK(plan.ready);
    NETW_CHECK_EQ(plan.basis, 2);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] fresh seed wins during wait"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    quarantine.admit_state(1, 3, state(3.0), true);
    quarantine.admit_witness(40, 0);
    quarantine.admit_witness(41, 0);
    CHECK(quarantine.latched);
    quarantine.admit_witness(42, 0);
    const QuarantinePlan plan
        = quarantine.admit_state(2, 42, state(42.0), true);
    CHECK(plan.ready);
    NETW_CHECK_EQ(plan.basis, 42);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] epoch admits one alignment"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    for (int64_t basis = 1; basis <= 3; ++basis) {
        quarantine.admit_witness(basis, 0);
        quarantine.admit_state(basis, basis, state(basis), true);
    }
    CHECK_FALSE(quarantine.align(4, state(4.0), 8).ready);
    quarantine.confirm_epoch();
    const QuarantinePlan aligned = quarantine.align(4, state(4.0), 8);
    CHECK(aligned.ready);
    CHECK(quarantine.corrections_suppressed());
    CHECK_FALSE(quarantine.admit_post_reseed(8));
    CHECK(quarantine.admit_post_reseed(9));
    CHECK(quarantine.finish_probation(true));
    CHECK_FALSE(quarantine.corrections_suppressed());
    CHECK_FALSE(quarantine.finish_probation(true));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] pending state pool is bounded"
) {
    Quarantine quarantine;
    quarantine.enter(0, 1, true);
    for (int basis = 0; basis < QUARANTINE_PENDING_LIMIT + 8; ++basis) {
        quarantine.admit_state(basis, basis, state(basis), true);
    }
    NETW_CHECK_EQ(
        int(quarantine.pending_states.size()),
        QUARANTINE_PENDING_LIMIT
    );
    NETW_CHECK_EQ(quarantine.pending_states[0].basis, 8);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] reseed projection honors "
    "restore mode"
) {
    Config config;
    config.restore = int(netw::RestoreMode::EXTRAPOLATED);
    config.max_restore_ticks = 3;
    const StateRow projected = advance_seed(
        moving_wiring(),
        config,
        moving_state(10.0, 2.0),
        5,
        0.5
    );
    NETW_CHECK_CLOSE(double(projected.values[0]), 13.0, 0.000001);

    config.restore = int(netw::RestoreMode::EXACT);
    const StateRow exact = advance_seed(
        moving_wiring(),
        config,
        moving_state(10.0, 2.0),
        5,
        0.5
    );
    NETW_CHECK_CLOSE(double(exact.values[0]), 10.0, 0.000001);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] a replay correction projects "
    "the reseed exactly as a snap correction does"
) {
    Config replayed;
    replayed.correction = int(netw::CorrectionMode::REPLAY);
    replayed.restore = int(netw::RestoreMode::EXTRAPOLATED);
    replayed.max_restore_ticks = 3;
    const StateRow under_replay = advance_seed(
        moving_wiring(),
        replayed,
        moving_state(10.0, 2.0),
        5,
        0.5
    );

    Config snapped = replayed;
    snapped.correction = int(netw::CorrectionMode::SNAP);
    const StateRow under_snap = advance_seed(
        moving_wiring(),
        snapped,
        moving_state(10.0, 2.0),
        5,
        0.5
    );

    NETW_CHECK_CLOSE(double(under_replay.values[0]), 13.0, 0.000001);
    NETW_CHECK_CLOSE(
        double(under_replay.values[0]),
        double(under_snap.values[0]),
        0.000001
    );

    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    CHECK(pool->supports(
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::REPLAY),
        int(netw::RestoreMode::EXTRAPOLATED),
        NetwPredictionEngine::ISLAND_NONE
    ));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] alignment publishes replay "
    "horizon"
) {
    Slot slot;
    slot.open = true;
    slot.rewire(moving_wiring());
    slot.config.correction = int(netw::CorrectionMode::REPLAY);
    slot.episode.active = true;
    slot.episode.id = 1;
    slot.episode.state = EpisodeState::FALLBACK;
    slot.quarantine.align_pending = true;
    slot.quarantine.confirm_epoch();

    const WritePlan plan = slot.align_reseed(4, moving_state(4.0, 1.0), 8);
    CHECK_FALSE(plan.skip);
    NETW_CHECK_EQ(plan.replay_from, 5);
    NETW_CHECK_EQ(plan.replay_through, 8);
    CHECK(slot.quarantine.probation_pending);
    CHECK_FALSE(slot.episode.active);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] standing causes do not price "
    "the hold"
) {
    Journal journal;
    append_row(journal, 0, Attribution::COMMAND);
    Episode episode;
    episode.open(journal, 0, Attribution::COMMAND);
    episode.enter_fallback(0);
    episode.close_fallback(0);

    append_row(journal, 1, Attribution::COMMAND);
    episode.open(journal, 1, Attribution::COMMAND);
    NETW_CHECK_EQ(episode.flap_multiplier(), 1);
    episode.enter_fallback(1);
    episode.close_fallback(1);

    append_row(journal, 2, Attribution::CONTACT);
    episode.open(journal, 2, Attribution::CONTACT);
    NETW_CHECK_EQ(episode.flap_multiplier(), 2);
    episode.enter_fallback(2);
    episode.close_fallback(2);

    append_row(journal, 3, Attribution::TOPOLOGY);
    episode.open(journal, 3, Attribution::TOPOLOGY);
    NETW_CHECK_EQ(episode.flap_multiplier(), 2);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] dirty probation reopens "
    "without flap pricing"
) {
    Slot slot;
    slot.open = true;
    slot.rewire(moving_wiring());
    append_row(slot.journal, 0, Attribution::PRE_STATE);
    slot.episode.open(slot.journal, 0, Attribution::PRE_STATE);
    slot.episode.enter_fallback(0);
    slot.episode.close_fallback(0);
    slot.quarantine.probation_pending = true;
    append_row(slot.journal, 1, Attribution::COMMAND);

    LocalVector<double> tolerances;
    tolerances.resize(2);
    tolerances[0] = 0.0;
    tolerances[1] = 0.0;
    const StateVerdict verdict = slot.compare(
        1,
        1,
        moving_state(0.0, 0.0),
        moving_state(1.0, 0.0),
        tolerances,
        tolerances,
        0.0,
        true,
        false
    );
    CHECK(verdict.corrected);
    NETW_CHECK_EQ(slot.episode.reopened_from, 1);
    NETW_CHECK_EQ(int(slot.episode.state), int(EpisodeState::FALLBACK));
    NETW_CHECK_EQ(slot.episode.flap_multiplier(), 1);
    CHECK(slot.quarantine.latched);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Quarantine] an adopted alignment retires the "
    "fallback and arms probation on the tick the caller landed it at"
) {
    Slot slot;
    slot.rewire(moving_wiring());
    append_row(slot.journal, 4, Attribution::CONTACT);
    slot.latch_quarantine(4, true, Attribution::CONTACT);
    CHECK(slot.quarantine.latched);
    NETW_CHECK_EQ(int(slot.episode.state), int(EpisodeState::FALLBACK));

    for (int64_t basis = 5; basis <= 7; ++basis) {
        slot.quarantine.admit_state(basis, basis, moving_state(1.0, 0.0), true);
        slot.quarantine.admit_witness(basis, WITNESS_SUPPORT);
    }
    REQUIRE(slot.quarantine.align_pending);
    CHECK_FALSE(slot.quarantine.epoch_confirmed);

    // An alignment the caller staged itself: the epoch is confirmed the same
    // way the pool's own path confirms it, and the adoption is what makes the
    // episode stop being a fallback.
    slot.confirm_reseed_epoch();
    CHECK(slot.quarantine.epoch_confirmed);
    slot.adopt_alignment(9, 12);

    CHECK_FALSE(slot.quarantine.align_pending);
    CHECK_FALSE(slot.quarantine.epoch_confirmed);
    CHECK(slot.quarantine.probation_pending);
    NETW_CHECK_EQ(slot.quarantine.aligned_transition, 9);
    NETW_CHECK_EQ(slot.episode.aligned_transition, 9);
    NETW_CHECK_EQ(slot.episode.closed_transition, 9);
    CHECK_FALSE(slot.episode.active);

    // Every transition through the horizon it named is already answered by
    // the seed, so admitting one would compare the past against the present.
    CHECK_FALSE(slot.admit_post_reseed(12));
    CHECK(slot.admit_post_reseed(13));
    // Admitting clears the horizon, so the next transition is judged on its
    // own rather than against a window that has already been left.
    CHECK(slot.admit_post_reseed(1));

    // Probation is a verdict one comparison consumes, and only the first.
    CHECK(slot.finish_probation(true));
    CHECK_FALSE(slot.quarantine.probation_pending);
    CHECK_FALSE(slot.finish_probation(true));
}

} // namespace TestNetwPredictQuarantineLaws

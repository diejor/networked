#include "support/netw_test.h"

#include "netw/predict/engine.hpp"

namespace TestNetwPredictReplayLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

const int SCHEDULES[] = {int(Schedule::TICK), int(Schedule::FRAME)};

int64_t consuming_slot(NetwPredictionEngine *p_pool, int p_schedule) {
    const int64_t slot = p_pool->open();
    p_pool->configure(
        slot,
        p_schedule,
        int(Role::CONSUME),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    );
    return slot;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] a replay journals the transition, "
    "label and kind the caller names"
) {
    for (const int schedule : SCHEDULES) {
        CAPTURE(schedule);
        NetwPredictionEngine held_pool;
        NetwPredictionEngine *const pool = &held_pool;
        const int64_t slot = consuming_slot(pool, schedule);

        pool->record_input(slot, 40, 0x5151);
        const netw::predict::DriveRecord drive = pool->replay_drive(
            slot,
            Dictionary(),
            7,
            40,
            int(DriveKind::FRESH),
            0,
            0,
            1.0 / 60.0,
            1,
            3,
            0,
            0,
            0
        );

        NETW_CHECK_EQ(drive.ran, true);
        NETW_CHECK_EQ(drive.transition, 7);
        NETW_CHECK_EQ(drive.label, 40);
        NETW_CHECK_EQ(pool->journal_size(slot), 1);
        NETW_CHECK_EQ(pool->journal_transition_at(slot, 0), 7);
        NETW_CHECK_EQ(pool->journal_label_at(slot, 0), 40);
        NETW_CHECK_EQ(pool->journal_c_hash_at(slot, 0), 0x5151);
    }
}

TEST_CASE("[Networked][Predict][Hosted][Law] a replay authors no tape entry") {
    for (const int schedule : SCHEDULES) {
        CAPTURE(schedule);
        NetwPredictionEngine held_pool;
        NetwPredictionEngine *const pool = &held_pool;
        const int64_t slot = consuming_slot(pool, schedule);

        for (int64_t transition = 0; transition < 4; transition++) {
            pool->record_input(slot, transition, 0x21 + transition);
            pool->replay_drive(
                slot,
                Dictionary(),
                transition,
                transition,
                int(DriveKind::FRESH),
                0,
                0,
                1.0 / 60.0,
                1,
                1,
                0,
                0,
                0
            );
        }

        NETW_CHECK_EQ(pool->tape_size(slot), 0);
        NETW_CHECK_EQ(pool->journal_size(slot), 4);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] an authoring replay on a tick schedule "
    "advances the tape and numbers the transition the owner's drive would have"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::TICK));

    for (int64_t tick = 0; tick < 3; tick++) {
        pool->record_input(slot, tick, 0x41 + tick);
        const netw::predict::DriveRecord drive = pool->replay_drive(
            slot,
            Dictionary(),
            tick,
            tick,
            int(DriveKind::FRESH),
            tick,
            0,
            1.0 / 60.0,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            true
        );
        NETW_CHECK_EQ(drive.transition, tick);
        NETW_CHECK_EQ(pool->tape_size(slot), tick + 1);
    }

    NETW_CHECK_EQ(pool->tape_size(slot), 3);
    NETW_CHECK_EQ(pool->journal_size(slot), 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] a frame slot authors no tape entry "
    "even when the caller says it is authoring, because the fold is the entry"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::FRAME));

    for (int64_t entry = 0; entry < 3; entry++) {
        pool->record_input(slot, entry, 0x51 + entry);
        pool->replay_drive(
            slot,
            Dictionary(),
            entry,
            entry,
            int(DriveKind::FRESH),
            0,
            0,
            1.0 / 60.0,
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            true
        );
    }

    NETW_CHECK_EQ(pool->tape_size(slot), 0);
    NETW_CHECK_EQ(pool->journal_size(slot), 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] a frame slot replays every entry at "
    "one timing where it authors only the first"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t replaying = consuming_slot(pool, int(Schedule::FRAME));
    const int64_t authoring = pool->open();
    pool->configure(
        authoring,
        int(Schedule::FRAME),
        int(Role::PREDICT),
        int(CorrectionMode::SNAP),
        int(RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE
    );

    for (int64_t entry = 0; entry < 3; entry++) {
        pool->record_input(replaying, entry, 0x31 + entry);
        pool->replay_drive(
            replaying,
            Dictionary(),
            entry,
            entry,
            int(DriveKind::FRESH),
            0,
            0,
            1.0 / 60.0,
            1,
            1,
            0,
            0,
            0
        );

        pool->record_input(authoring, entry, 0x31 + entry);
        pool->open_drive(
            authoring,
            Dictionary(),
            9,
            0,
            1.0 / 60.0,
            1,
            true,
            1,
            0,
            0,
            0
        );
    }

    NETW_CHECK_EQ(pool->journal_size(replaying), 3);
    NETW_CHECK_EQ(pool->journal_size(authoring), 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the decoded window holds the first "
    "arrival of a transition and never a later one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::FRAME));

    Dictionary first;
    first["motion"] = 1;
    Dictionary second;
    second["motion"] = 2;

    CHECK(pool->command_admit(slot, 4, 40, true, first));
    CHECK(pool->command_has(slot, 4));
    NETW_CHECK_EQ(pool->command_label_of(slot, 4), 40);
    CHECK(pool->command_is_fresh(slot, 4));
    NETW_CHECK_EQ(int(pool->command_payload_of(slot, 4)["motion"]), 1);

    CHECK_FALSE(pool->command_admit(slot, 4, 99, false, second));
    NETW_CHECK_EQ(pool->command_label_of(slot, 4), 40);
    NETW_CHECK_EQ(int(pool->command_payload_of(slot, 4)["motion"]), 1);

    CHECK_FALSE(pool->command_has(slot, 5));
    NETW_CHECK_EQ(pool->command_label_of(slot, 5), -1);
    CHECK(pool->command_payload_of(slot, 5).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the consume depth stops at the first "
    "transition the window is missing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::FRAME));

    NETW_CHECK_EQ(pool->command_depth_from(slot, 0), 0);
    for (int64_t at = 0; at < 3; ++at) {
        CHECK(pool->command_admit(slot, at, at, true, Dictionary()));
    }
    CHECK(pool->command_admit(slot, 4, 4, true, Dictionary()));

    NETW_CHECK_EQ(pool->command_depth_from(slot, 0), 3);
    NETW_CHECK_EQ(pool->command_depth_from(slot, 2), 1);
    NETW_CHECK_EQ(pool->command_depth_from(slot, 3), 0);
    NETW_CHECK_EQ(pool->command_depth_from(slot, 4), 1);
    NETW_CHECK_EQ(pool->command_depth_from(slot, -1), 0);

    const PackedInt64Array held = pool->command_transitions(slot);
    NETW_CHECK_EQ(int(held.size()), 4);
    NETW_CHECK_EQ(held[3], 4);

    pool->tape_reset(slot, 3);
    NETW_CHECK_EQ(pool->command_depth_from(slot, 0), 0);
    CHECK_FALSE(pool->command_has(slot, 0));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the decoded window is bounded, and "
    "drops its oldest transition rather than its newest"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::FRAME));

    const int over = TAPE_HISTORY_LIMIT + 8;
    for (int64_t at = 0; at < over; ++at) {
        CHECK(pool->command_admit(slot, at, at, true, Dictionary()));
    }

    NETW_CHECK_EQ(
        int(pool->command_transitions(slot).size()),
        TAPE_HISTORY_LIMIT
    );
    CHECK_FALSE(pool->command_has(slot, 7));
    CHECK(pool->command_has(slot, 8));
    CHECK(pool->command_has(slot, over - 1));
    NETW_CHECK_EQ(pool->command_depth_from(slot, 8), TAPE_HISTORY_LIMIT);

    CHECK_FALSE(pool->command_admit(slot + 9000, 0, 0, true, Dictionary()));
    CHECK_FALSE(pool->command_has(slot + 9000, 0));
    NETW_CHECK_EQ(pool->command_depth_from(slot + 9000, 0), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the replay depth is a high-water mark, "
    "not the last window walked"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::TICK));

    NETW_CHECK_EQ(
        pool->drive_stats(slot)[NetwPredictionEngine::STAT_MAX_REPLAY_DEPTH],
        int64_t(0)
    );

    pool->note_replay_depth(slot, 3);
    NETW_CHECK_EQ(
        pool->drive_stats(slot)[NetwPredictionEngine::STAT_MAX_REPLAY_DEPTH],
        int64_t(3)
    );

    pool->note_replay_depth(slot, 11);
    NETW_CHECK_EQ(
        pool->drive_stats(slot)[NetwPredictionEngine::STAT_MAX_REPLAY_DEPTH],
        int64_t(11)
    );

    pool->note_replay_depth(slot, 1);
    NETW_CHECK_EQ(
        pool->drive_stats(slot)[NetwPredictionEngine::STAT_MAX_REPLAY_DEPTH],
        int64_t(11)
    );

    pool->note_replay_depth(slot, 0);
    NETW_CHECK_EQ(
        pool->drive_stats(slot)[NetwPredictionEngine::STAT_MAX_REPLAY_DEPTH],
        int64_t(11)
    );

    pool->note_replay_depth(slot + 9000, 40);
    NETW_CHECK_EQ(
        pool->drive_stats(slot)[NetwPredictionEngine::STAT_MAX_REPLAY_DEPTH],
        int64_t(11)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] the fingerprint ledger is one column "
    "the rewire clears"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming_slot(pool, int(Schedule::TICK));

    pool->record_input(slot, 0, 0x11);
    pool->replay_drive(
        slot,
        Dictionary(),
        0,
        0,
        int(DriveKind::FRESH),
        0,
        0,
        1.0 / 60.0,
        1,
        3,
        0,
        0,
        0
    );
    pool->admit_ack(slot, 0, netw::predict::EvidenceRow(), false);

    PackedInt64Array ledger = pool->compare_stats(slot);
    NETW_CHECK_EQ(ledger[NetwPredictionEngine::STAT_FP_VERIFIED], int64_t(1));

    pool->reset_for_rewire(slot, true, true, false, Dictionary());

    ledger = pool->compare_stats(slot);
    NETW_CHECK_EQ(ledger[NetwPredictionEngine::STAT_FP_VERIFIED], int64_t(0));
    NETW_CHECK_EQ(ledger[NetwPredictionEngine::STAT_FP_MISMATCHES], int64_t(0));
    NETW_CHECK_EQ(
        ledger[NetwPredictionEngine::STAT_FIRST_DIVERGENT_TRANSITION],
        int64_t(-1)
    );
}

} // namespace TestNetwPredictReplayLaws

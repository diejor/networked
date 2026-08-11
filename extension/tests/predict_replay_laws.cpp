// Laws for the transitions a slot did not author.
//
// A consuming peer re-runs a lane its owner already numbered, labelled and
// folded, so the answers `open_drive` computes are answers it must not
// recompute. The distinction is the whole reason `replay_drive` exists beside
// it, and the clamp is where the two part company: an authoring pass opens at
// most one transition per tick, where a consume pass replays every queued
// entry at one timing.

#include "support/netw_test.h"

#include "netw/predict/engine.hpp"

namespace TestNetwPredictReplayLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

const int SCHEDULES[] = { int(Schedule::TICK), int(Schedule::FRAME) };

int64_t consuming_slot(const Ref<NetwPredictionEngine> &p_pool, int p_schedule) {
    const int64_t slot = p_pool->open(Ref<NetwPredictDeclaration>());
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
        Ref<NetwPredictionEngine> pool;
        pool.instantiate();
        const int64_t slot = consuming_slot(pool, schedule);

        pool->record_input(slot, 40, 0x5151);
        const Ref<NetwPredictDrive> drive = pool->replay_drive(
            slot, Dictionary(), 7, 40, int(DriveKind::FRESH), 0, 0, 1.0 / 60.0, 1, 3, 0, 0, 0
        );

        NETW_CHECK_EQ(drive->ran(), true);
        NETW_CHECK_EQ(drive->transition(), 7);
        NETW_CHECK_EQ(drive->label(), 40);
        NETW_CHECK_EQ(pool->journal_size(slot), 1);
        NETW_CHECK_EQ(pool->journal_transition_at(slot, 0), 7);
        NETW_CHECK_EQ(pool->journal_label_at(slot, 0), 40);
        NETW_CHECK_EQ(pool->journal_c_hash_at(slot, 0), 0x5151);
    }
}

// The tape is what an owner publishes and a consumer receives, so a slot that
// authored an entry while replaying one would be claiming authorship of a lane
// it is following.
TEST_CASE(
    "[Networked][Predict][Hosted][Law] a replay authors no tape entry"
) {
    for (const int schedule : SCHEDULES) {
        CAPTURE(schedule);
        Ref<NetwPredictionEngine> pool;
        pool.instantiate();
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

// The one claim the shell's consume path could not make through `open_drive`.
// A FRAME slot admits one authored transition per tick and a consume pass has
// exactly one timing for however many entries it drains, so routing the replay
// through the authoring verb loses every entry after the first.
TEST_CASE(
    "[Networked][Predict][Hosted][Law] a frame slot replays every entry at "
    "one timing where it authors only the first"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t replaying = consuming_slot(pool, int(Schedule::FRAME));
    const int64_t authoring = pool->open(Ref<NetwPredictDeclaration>());
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
        pool->open_drive(authoring, Dictionary(), 9, 0, 1.0 / 60.0, 1, true, 1, 0, 0, 0);
    }

    NETW_CHECK_EQ(pool->journal_size(replaying), 3);
    NETW_CHECK_EQ(pool->journal_size(authoring), 1);
}

} // namespace TestNetwPredictReplayLaws

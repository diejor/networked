#include "support/netw_test.h"

#include "netw/predict/drive.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/transition_book.hpp"

namespace TestNetwPredictRingDepthLaws {

using netw::NetwPredictionEngine;
using netw::predict::ACK_AGE_MAX;
using netw::predict::TAPE_HISTORY_LIMIT;

constexpr int OVER = 8;

int64_t consuming(NetwPredictionEngine *p_pool, netw::Schedule p_schedule) {
    const int64_t slot = p_pool->open();
    REQUIRE(p_pool->configure(
        slot,
        int(p_schedule),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));
    return slot;
}

void drive(NetwPredictionEngine *p_pool, int64_t p_slot, int p_transition) {
    p_pool->record_input(p_slot, p_transition, p_transition + 100);
    REQUIRE(p_pool
                ->replay_drive(
                    p_slot,
                    godot::Dictionary(),
                    p_transition,
                    p_transition,
                    int(netw::DriveKind::FRESH),
                    p_transition,
                    p_transition,
                    1.0 / 60.0,
                    1,
                    p_transition,
                    0,
                    0,
                    0
                )
                .ran);
    p_pool->close_drive(p_slot, p_transition, p_transition, 0, 0, 0);
}

void drive_through(NetwPredictionEngine *p_pool, int64_t p_slot, int p_count) {
    for (int transition = 0; transition < p_count; ++transition) {
        drive(p_pool, p_slot, transition);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted] a tick slot holds the 64-tick window in "
    "its predicted book and its journal, and drops the oldest transition"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming(pool, netw::Schedule::TICK);
    const int count = ACK_AGE_MAX + OVER;
    drive_through(pool, slot, count);

    NETW_CHECK_EQ(int(pool->book_depth(slot)), ACK_AGE_MAX);
    NETW_CHECK_EQ(pool->journal_capacity(slot), ACK_AGE_MAX);
    NETW_CHECK_EQ(pool->journal_size(slot), ACK_AGE_MAX);
    NETW_CHECK_EQ(pool->journal_transition_at(slot, 0), int64_t(OVER));
    const netw::predict::TransitionBook *inputs = pool->input_book(slot);
    REQUIRE(inputs != nullptr);
    NETW_CHECK_EQ(int(inputs->capacity()), TAPE_HISTORY_LIMIT);
}

TEST_CASE(
    "[Networked][Predict][Hosted] a frame slot keeps the state of every tape "
    "entry, the tape's whole depth"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming(pool, netw::Schedule::FRAME);
    const int count = TAPE_HISTORY_LIMIT + OVER;
    drive_through(pool, slot, count);

    NETW_CHECK_EQ(int(pool->book_depth(slot)), TAPE_HISTORY_LIMIT);
    NETW_CHECK_EQ(pool->journal_capacity(slot), TAPE_HISTORY_LIMIT);
    NETW_CHECK_EQ(pool->journal_size(slot), TAPE_HISTORY_LIMIT);
    NETW_CHECK_EQ(pool->journal_transition_at(slot, 0), int64_t(OVER));
}

TEST_CASE(
    "[Networked][Predict][Hosted] a journal carries its diagnostics only "
    "while a reader is attached"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = consuming(pool, netw::Schedule::TICK);
    drive(pool, slot, 0);

    CHECK_FALSE(pool->journal_diagnostics_armed(slot));
    NETW_CHECK_EQ(int(pool->journal_row(slot, 0).kind), 0);

    pool->attach_journal_reader(slot, true);
    drive(pool, slot, 1);
    CHECK(pool->journal_diagnostics_armed(slot));
    NETW_CHECK_EQ(
        int(pool->journal_row(slot, 1).kind),
        int(netw::DriveKind::FRESH)
    );

    pool->attach_journal_reader(slot, false);
    CHECK_FALSE(pool->journal_diagnostics_armed(slot));
    NETW_CHECK_EQ(int(pool->journal_row(slot, 1).kind), 0);
    CHECK(pool->journal_row(slot, 1).present);

    pool->attach_journal_readers(true);
    const int64_t later = consuming(pool, netw::Schedule::TICK);
    CHECK(pool->journal_diagnostics_armed(later));
    CHECK(pool->journal_diagnostics_armed(slot));
}

} // namespace TestNetwPredictRingDepthLaws

#include "support/netw_test.h"

#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestNetwPredictProvenanceLaws {

namespace {

using godot::Dictionary;
using godot::Ref;
using godot::StringName;
using netw::NetwPredictionEngine;

int64_t driving_slot(NetwPredictionEngine *p_pool) {
    godot::LocalVector<netw::predict::FieldDecl> fields;
    fields.push_back(
        netw::field_decl(
            StringName("position"),
            int(netw::predict::PropertyClass::CAUSAL)
        )
    );
    const int64_t slot = p_pool->open(fields);
    REQUIRE(p_pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));
    return slot;
}

Dictionary write_of(int64_t p_write_id) {
    Dictionary out;
    out[StringName("write_id")] = p_write_id;
    return out;
}

Dictionary staged_write(
    int64_t p_write_id,
    int64_t p_episode,
    int64_t p_basis
) {
    Dictionary out = write_of(p_write_id);
    out[StringName("episode")] = p_episode;
    out[StringName("operator")] = int(netw::predict::Operator::RESEED);
    out[StringName("basis")] = p_basis;
    return out;
}

int64_t driven(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_tick,
    int64_t p_pre_fp
) {
    p_pool->record_input(p_slot, p_tick, int32_t(p_tick));
    const netw::predict::DriveRecord drive = p_pool->open_drive(
        p_slot,
        Dictionary(),
        p_tick,
        p_tick,
        1.0 / 60.0,
        1,
        true,
        p_pre_fp,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    return drive.transition;
}

int row_of(NetwPredictionEngine *p_pool, int64_t p_slot, int64_t p_transition) {
    const int index = p_pool->journal_slot_of(p_slot, p_transition);
    REQUIRE(index >= 0);
    return index;
}

bool chain_broken(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_transition
) {
    const int index = row_of(p_pool, p_slot, p_transition);
    const int flags = p_pool->journal_flags_at(p_slot, index);
    return (flags & netw::predict::ROW_CHAIN_BROKEN) != 0;
}

int64_t chain_breaks(NetwPredictionEngine *p_pool, int64_t p_slot) {
    return p_pool->drive_stats(p_slot)[NetwPredictionEngine::STAT_CHAIN_BREAKS];
}

void close_a_row_the_next_drive_cannot_continue(
    NetwPredictionEngine *p_pool,
    int64_t p_slot
) {
    const int64_t first = driven(p_pool, p_slot, 1, 100);
    p_pool->close_drive(p_slot, first, 500, 0, 0, 0);
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Provenance] a staged write stands on its "
    "slot until it is replaced"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t written = driving_slot(pool);
    const int64_t untouched = driving_slot(pool);

    CHECK(pool->pending_provenance(written).is_empty());

    pool->set_pending_provenance(written, write_of(7));

    NETW_CHECK_EQ(
        int64_t(pool->pending_provenance(written)[StringName("write_id")]),
        7
    );
    CHECK(pool->pending_provenance(untouched).is_empty());

    pool->set_pending_provenance(written, Dictionary());

    CHECK(pool->pending_provenance(written).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Provenance] a slot that was never opened "
    "stages nothing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t opened = driving_slot(pool);

    CHECK(pool->pending_provenance(-1).is_empty());

    pool->set_pending_provenance(-1, write_of(7));

    CHECK(pool->pending_provenance(-1).is_empty());
    CHECK(pool->pending_provenance(opened).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Provenance] a discontinuity no write "
    "explains breaks the chain"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    close_a_row_the_next_drive_cannot_continue(pool, slot);

    const int64_t second = driven(pool, slot, 2, 900);
    pool->stamp_pending_provenance(slot, second);

    CHECK(chain_broken(pool, slot, second));
    NETW_CHECK_EQ(chain_breaks(pool, slot), 1);
    NETW_CHECK_EQ(
        pool->journal_write_id_at(slot, row_of(pool, slot, second)),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Provenance] a staged write is stamped onto "
    "the row it opened and retires the break it explains"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    close_a_row_the_next_drive_cannot_continue(pool, slot);

    pool->set_pending_provenance(slot, staged_write(7, 3, 42));
    const int64_t second = driven(pool, slot, 2, 900);
    REQUIRE(chain_broken(pool, slot, second));

    pool->stamp_pending_provenance(slot, second);

    const int index = row_of(pool, slot, second);
    NETW_CHECK_EQ(pool->journal_write_id_at(slot, index), 7);
    NETW_CHECK_EQ(pool->journal_episode_id_at(slot, index), 3);
    NETW_CHECK_EQ(pool->journal_basis_at(slot, index), 42);
    NETW_CHECK_EQ(
        pool->journal_operator_at(slot, index),
        int(netw::predict::Operator::RESEED)
    );
    CHECK_FALSE(chain_broken(pool, slot, second));
    NETW_CHECK_EQ(chain_breaks(pool, slot), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Provenance] a stamp with nothing staged "
    "leaves the row as the drive wrote it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    close_a_row_the_next_drive_cannot_continue(pool, slot);

    pool->set_pending_provenance(slot, write_of(0));
    const int64_t second = driven(pool, slot, 2, 900);
    pool->stamp_pending_provenance(slot, second);

    NETW_CHECK_EQ(
        pool->journal_write_id_at(slot, row_of(pool, slot, second)),
        0
    );
    CHECK(chain_broken(pool, slot, second));
    NETW_CHECK_EQ(chain_breaks(pool, slot), 1);
}

} // namespace TestNetwPredictProvenanceLaws

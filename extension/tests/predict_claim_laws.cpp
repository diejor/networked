#include "support/netw_test.h"

#include "netw/api/predict.hpp"
#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestNetwPredictClaimLaws {

namespace {

using godot::PackedInt32Array;
using godot::PackedInt64Array;
using godot::Ref;
using netw::NetwPredictionEngine;
using netw::SchemaCore;
using netw::table::SchemaRecord;

constexpr int64_t DRIVEN = 4;
constexpr int64_t PRE_FP = 201;
constexpr int64_t PRE_POSE = 211;
constexpr int64_t PRE_MOMENTUM = 212;
constexpr int64_t PRE_CONTROLLER = 213;
constexpr int64_t RAW_FP = 221;
constexpr int64_t POST_FP = 301;
constexpr int64_t POST_POSE = 311;
constexpr int64_t POST_MOMENTUM = 312;
constexpr int64_t POST_CONTROLLER = 313;

const SchemaRecord &input_schema() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = "PredictInput";
        SchemaCore::append_column(&made, "button", SchemaCore::U8, 1);
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

netw::predict::CommandFrameRecord frame_of(
    const PackedInt64Array &p_transitions,
    int64_t p_post_fp,
    int64_t p_topo_fp = 0
) {
    netw::predict::CommandFrameRecord frame;
    netw::predict::CommandFrameRecord::open(input_schema(), frame);
    PackedInt32Array pre_families;
    pre_families.push_back(int32_t(PRE_POSE));
    pre_families.push_back(int32_t(PRE_MOMENTUM));
    pre_families.push_back(int32_t(PRE_CONTROLLER));
    PackedInt32Array post_families;
    post_families.push_back(int32_t(POST_POSE));
    post_families.push_back(int32_t(POST_MOMENTUM));
    post_families.push_back(int32_t(POST_CONTROLLER));
    for (int at = 0; at < p_transitions.size(); ++at) {
        frame.append_transition(p_transitions[at], p_transitions[at], false);
        frame.append_evidence(
            int(netw::predict::EVIDENCE_WITNESS),
            int(PRE_FP),
            int(p_post_fp),
            0,
            int(p_topo_fp),
            0,
            pre_families,
            post_families,
            int(RAW_FP)
        );
    }
    return frame;
}

PackedInt64Array one(int64_t p_transition) {
    PackedInt64Array out;
    out.push_back(p_transition);
    return out;
}

int64_t driving_slot(NetwPredictionEngine *p_pool) {
    const int64_t slot = p_pool->open();
    p_pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    return slot;
}

void drive_one(NetwPredictionEngine *p_pool, int64_t p_slot) {
    p_pool->record_input(p_slot, DRIVEN, 100);
    p_pool->replay_drive(
        p_slot,
        godot::Dictionary(),
        DRIVEN,
        DRIVEN,
        int(netw::DriveKind::FRESH),
        DRIVEN,
        DRIVEN,
        1.0 / 60.0,
        1,
        PRE_FP,
        PRE_POSE,
        PRE_MOMENTUM,
        PRE_CONTROLLER,
        RAW_FP,
        int(netw::predict::EVIDENCE_WITNESS)
    );
    p_pool->close_drive(
        p_slot,
        DRIVEN,
        POST_FP,
        POST_POSE,
        POST_MOMENTUM,
        POST_CONTROLLER
    );
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a filed claim is judged once and "
    "then held no longer"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP));
    NETW_CHECK_EQ(pool->owner_claim_count(slot), 1);

    const PackedInt64Array first
        = pool->judge_owner_claim(slot, DRIVEN, POST_FP);
    NETW_CHECK_EQ(first[NetwPredictionEngine::CLAIM_JUDGED], 1);
    NETW_CHECK_EQ(first[NetwPredictionEngine::CLAIM_MATCHED], 1);
    NETW_CHECK_EQ(pool->owner_claim_count(slot), 0);

    const PackedInt64Array again
        = pool->judge_owner_claim(slot, DRIVEN, POST_FP);
    NETW_CHECK_EQ(again[NetwPredictionEngine::CLAIM_JUDGED], 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a transition no claim was filed for "
    "is unjudged rather than agreeing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP));

    const PackedInt64Array verdict
        = pool->judge_owner_claim(slot, DRIVEN + 1, POST_FP);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_JUDGED], 0);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_MATCHED], 0);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_ATTRIBUTION], -1);
    NETW_CHECK_EQ(pool->owner_claim_count(slot), 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a disagreement with no journal row "
    "to compare against charges nothing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP));

    const PackedInt64Array verdict
        = pool->judge_owner_claim(slot, DRIVEN, POST_FP + 1);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_JUDGED], 1);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_MATCHED], 0);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_ATTRIBUTION], -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a disagreement whose antecedents all "
    "agreed is charged to CLOSURE"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    drive_one(pool, slot);

    const netw::predict::JournalRow row = pool->journal_row(slot, DRIVEN);
    REQUIRE(row.present);
    pool->record_owner_claims(
        slot,
        frame_of(one(DRIVEN), POST_FP + 1, row.topo_fp)
    );

    const PackedInt64Array verdict
        = pool->judge_owner_claim(slot, DRIVEN, POST_FP);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_JUDGED], 1);
    NETW_CHECK_EQ(verdict[NetwPredictionEngine::CLAIM_MATCHED], 0);
    NETW_CHECK_EQ(
        verdict[NetwPredictionEngine::CLAIM_ATTRIBUTION],
        int(netw::predict::Attribution::CLOSURE)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a disagreement over the state the "
    "transition started from is charged to PRE_STATE"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    pool->record_input(slot, DRIVEN, 100);
    pool->replay_drive(
        slot,
        godot::Dictionary(),
        DRIVEN,
        DRIVEN,
        int(netw::DriveKind::FRESH),
        DRIVEN,
        DRIVEN,
        1.0 / 60.0,
        1,
        PRE_FP + 7,
        PRE_POSE + 7,
        PRE_MOMENTUM,
        PRE_CONTROLLER,
        RAW_FP,
        int(netw::predict::EVIDENCE_WITNESS)
    );
    pool->close_drive(
        slot,
        DRIVEN,
        POST_FP,
        POST_POSE,
        POST_MOMENTUM,
        POST_CONTROLLER
    );

    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP + 1));

    const PackedInt64Array verdict
        = pool->judge_owner_claim(slot, DRIVEN, POST_FP);
    NETW_CHECK_EQ(
        verdict[NetwPredictionEngine::CLAIM_ATTRIBUTION],
        int(netw::predict::Attribution::PRE_STATE)
    );
    const netw::predict::JournalRow row = pool->journal_row(slot, DRIVEN);
    REQUIRE(row.present);
    NETW_CHECK_EQ(
        int(row.attribution),
        int(netw::predict::Attribution::PRE_STATE)
    );
    NETW_CHECK_EQ(
        int(row.differing_family),
        int(netw::predict::DifferingFamily::POSE)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] the book keeps the newest claims and "
    "drops the oldest transition when it overflows"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    PackedInt64Array first;
    PackedInt64Array second;
    for (int64_t transition = 0; transition < 255; ++transition) {
        first.push_back(transition);
        second.push_back(255 + transition);
    }
    pool->record_owner_claims(slot, frame_of(first, POST_FP));
    pool->record_owner_claims(slot, frame_of(second, POST_FP));

    NETW_CHECK_EQ(pool->owner_claim_count(slot), 256);
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            253,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        0
    );
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            509,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        1
    );
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            254,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a transition numbered below zero is "
    "not a transition and files no claim"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    PackedInt64Array run;
    run.push_back(-1);
    run.push_back(0);
    pool->record_owner_claims(slot, frame_of(run, POST_FP));

    NETW_CHECK_EQ(pool->owner_claim_count(slot), 1);
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            -1,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        0
    );
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            0,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] re-filing a held transition replaces "
    "the claim rather than doubling it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP));
    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP + 5));

    NETW_CHECK_EQ(pool->owner_claim_count(slot), 1);
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            DRIVEN,
            POST_FP + 5
        )[NetwPredictionEngine::CLAIM_MATCHED],
        1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] clearing the book leaves every "
    "transition unjudged"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->record_owner_claims(slot, frame_of(one(DRIVEN), POST_FP));
    pool->clear_owner_claims(slot);

    NETW_CHECK_EQ(pool->owner_claim_count(slot), 0);
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            slot,
            DRIVEN,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a claim belongs to the slot it was "
    "filed against"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t owned = driving_slot(pool);
    const int64_t other = driving_slot(pool);

    pool->record_owner_claims(owned, frame_of(one(DRIVEN), POST_FP));

    NETW_CHECK_EQ(pool->owner_claim_count(other), 0);
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            other,
            DRIVEN,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        0
    );
    NETW_CHECK_EQ(pool->owner_claim_count(owned), 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a slot that was never opened holds "
    "no claim and judges none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;

    pool->record_owner_claims(-1, frame_of(one(DRIVEN), POST_FP));

    NETW_CHECK_EQ(pool->owner_claim_count(-1), 0);
    NETW_CHECK_EQ(
        pool->judge_owner_claim(
            -1,
            DRIVEN,
            POST_FP
        )[NetwPredictionEngine::CLAIM_JUDGED],
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a transition authority said nothing "
    "about reads -1 rather than a class"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    NETW_CHECK_EQ(pool->authority_witness_class(slot, DRIVEN), -1);

    pool->record_authority_witness_class(slot, DRIVEN, 0);

    NETW_CHECK_EQ(pool->authority_witness_class(slot, DRIVEN), 0);
    NETW_CHECK_EQ(pool->authority_witness_class(slot, DRIVEN + 1), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a re-reported witness class replaces "
    "the one held and is read back unchanged"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->record_authority_witness_class(slot, DRIVEN, 2);
    pool->record_authority_witness_class(slot, DRIVEN, 5);

    NETW_CHECK_EQ(pool->authority_witness_class(slot, DRIVEN), 5);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] the witness book drops its oldest "
    "transition when it overflows"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    for (int64_t transition = 0; transition < 300; ++transition) {
        pool->record_authority_witness_class(slot, transition, 1);
    }

    NETW_CHECK_EQ(pool->authority_witness_class(slot, 43), -1);
    NETW_CHECK_EQ(pool->authority_witness_class(slot, 44), 1);
    NETW_CHECK_EQ(pool->authority_witness_class(slot, 299), 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] clearing the witness book forgets "
    "every transition, and a witness class belongs to its slot"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t reported = driving_slot(pool);
    const int64_t quiet = driving_slot(pool);

    pool->record_authority_witness_class(reported, DRIVEN, 3);
    NETW_CHECK_EQ(pool->authority_witness_class(quiet, DRIVEN), -1);

    pool->clear_authority_witness_classes(quiet);
    NETW_CHECK_EQ(pool->authority_witness_class(reported, DRIVEN), 3);

    pool->clear_authority_witness_classes(reported);
    NETW_CHECK_EQ(pool->authority_witness_class(reported, DRIVEN), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a slot that was never opened reports "
    "no witness class"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;

    pool->record_authority_witness_class(-1, DRIVEN, 3);

    NETW_CHECK_EQ(pool->authority_witness_class(-1, DRIVEN), -1);
}

namespace {

godot::PackedFloat64Array measured(double p_first, double p_second) {
    godot::PackedFloat64Array out;
    out.push_back(p_first);
    out.push_back(p_second);
    return out;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a slot with no write outstanding is "
    "not armed"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    CHECK_FALSE(pool->ledger_armed(slot));

    pool->ledger_arm(slot, 0, 4.0, 10);

    CHECK(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a comparison that has not reached "
    "past the write leaves the question open"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    pool->ledger_arm(slot, 0, 4.0, 10);

    NETW_CHECK_EQ(pool->ledger_settle(slot, 10, measured(1.0, 1.0)).size(), 0);
    CHECK(pool->ledger_armed(slot));

    NETW_CHECK_EQ(pool->ledger_settle(slot, 5, measured(1.0, 1.0)).size(), 0);
    CHECK(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a shrink past the write is a "
    "contraction and closes the question"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    pool->ledger_arm(slot, 0, 4.0, 10);

    const PackedInt32Array settled
        = pool->ledger_settle(slot, 11, measured(1.0, 1.0));

    NETW_CHECK_EQ(settled.size(), 1);
    NETW_CHECK_EQ(settled[0], 0);
    CHECK_FALSE(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a write that did not shrink closes "
    "uncredited"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    pool->ledger_arm(slot, 0, 4.0, 10);

    NETW_CHECK_EQ(pool->ledger_settle(slot, 11, measured(4.0, 0.0)).size(), 0);
    CHECK_FALSE(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a field the comparison did not "
    "measure is unbounded rather than zero"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    pool->ledger_arm(slot, 3, 4.0, 10);

    NETW_CHECK_EQ(pool->ledger_settle(slot, 11, measured(0.0, 0.0)).size(), 0);
    CHECK_FALSE(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a second write to one field replaces "
    "the question rather than queueing behind it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->ledger_arm(slot, 0, 4.0, 10);
    pool->ledger_arm(slot, 0, 1.0, 20);

    NETW_CHECK_EQ(pool->ledger_settle(slot, 11, measured(0.5, 0.0)).size(), 0);
    CHECK(pool->ledger_armed(slot));

    const PackedInt32Array settled
        = pool->ledger_settle(slot, 21, measured(0.5, 0.0));
    NETW_CHECK_EQ(settled.size(), 1);
    CHECK_FALSE(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] each field carries its own question "
    "and only the reached ones settle"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->ledger_arm(slot, 0, 4.0, 10);
    pool->ledger_arm(slot, 1, 4.0, 30);

    const PackedInt32Array settled
        = pool->ledger_settle(slot, 11, measured(1.0, 1.0));

    NETW_CHECK_EQ(settled.size(), 1);
    NETW_CHECK_EQ(settled[0], 0);
    CHECK(pool->ledger_armed(slot));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] clearing drops the questions "
    "unanswered, and a question belongs to its slot"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t armed = driving_slot(pool);
    const int64_t quiet = driving_slot(pool);

    pool->ledger_arm(armed, 0, 4.0, 10);
    CHECK_FALSE(pool->ledger_armed(quiet));

    pool->ledger_clear(quiet);
    CHECK(pool->ledger_armed(armed));

    pool->ledger_clear(armed);
    CHECK_FALSE(pool->ledger_armed(armed));
    NETW_CHECK_EQ(pool->ledger_settle(armed, 11, measured(1.0, 1.0)).size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Ledger] a field below zero arms nothing, and "
    "a slot that was never opened holds no question"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->ledger_arm(slot, -1, 4.0, 10);
    CHECK_FALSE(pool->ledger_armed(slot));

    pool->ledger_arm(-1, 0, 4.0, 10);
    CHECK_FALSE(pool->ledger_armed(-1));
    NETW_CHECK_EQ(pool->ledger_settle(-1, 11, measured(1.0, 1.0)).size(), 0);
}

namespace {

godot::Dictionary detail_of(int p_contacts) {
    godot::Dictionary out;
    out[StringName("contacts")] = p_contacts;
    return out;
}

godot::Dictionary details_in(NetwPredictionEngine *p_pool, int64_t p_slot) {
    const Ref<netw::NetwPredictJournal> snapshot
        = p_pool->journal_snapshot(p_slot);
    const godot::Dictionary row = snapshot->row_at(DRIVEN);
    return row.get(StringName("witness_detail"), godot::Dictionary());
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Witness] a filed solve detail reaches the "
    "journal snapshot's row"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    drive_one(pool, slot);

    pool->record_witness_detail(slot, DRIVEN, detail_of(3));

    const godot::Dictionary carried = details_in(pool, slot);
    NETW_CHECK_EQ(int(carried[StringName("contacts")]), 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Witness] a filed solve detail is copied, so "
    "a caller reusing its row cannot rewrite what was filed"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    drive_one(pool, slot);

    godot::Dictionary reused = detail_of(3);
    pool->record_witness_detail(slot, DRIVEN, reused);
    reused[StringName("contacts")] = 9;

    NETW_CHECK_EQ(int(details_in(pool, slot)[StringName("contacts")]), 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Witness] clearing the detail leaves the "
    "journal row carrying none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    drive_one(pool, slot);

    pool->record_witness_detail(slot, DRIVEN, detail_of(3));
    pool->clear_witness_details(slot);

    CHECK(details_in(pool, slot).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Witness] a solve detail belongs to the slot "
    "it was filed against, and an unopened slot files none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t filed = driving_slot(pool);
    const int64_t quiet = driving_slot(pool);
    drive_one(pool, filed);
    drive_one(pool, quiet);

    pool->record_witness_detail(filed, DRIVEN, detail_of(3));
    pool->record_witness_detail(-1, DRIVEN, detail_of(7));

    NETW_CHECK_EQ(int(details_in(pool, filed)[StringName("contacts")]), 3);
    CHECK(details_in(pool, quiet).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Deferral] a held row reads back by basis and "
    "tick, and releasing it takes it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), -1);
    NETW_CHECK_EQ(pool->deferred_operator_recv_tick(slot), -1);

    pool->hold_deferred_operator(slot, DRIVEN, 77, detail_of(3));

    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), DRIVEN);
    NETW_CHECK_EQ(pool->deferred_operator_recv_tick(slot), 77);

    const godot::Dictionary taken = pool->release_deferred_operator(slot);
    NETW_CHECK_EQ(int(taken[StringName("contacts")]), 3);
    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), -1);
    CHECK(pool->release_deferred_operator(slot).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Deferral] a held row is copied, so a caller "
    "reusing its payload cannot rewrite what is waiting"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    godot::Dictionary reused = detail_of(3);
    pool->hold_deferred_operator(slot, DRIVEN, 77, reused);
    reused[StringName("contacts")] = 9;

    const godot::Dictionary taken = pool->release_deferred_operator(slot);
    NETW_CHECK_EQ(int(taken[StringName("contacts")]), 3);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Deferral] at most one row is held, and a "
    "newer basis replaces the older"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->hold_deferred_operator(slot, DRIVEN, 77, detail_of(3));
    pool->hold_deferred_operator(slot, DRIVEN + 1, 78, detail_of(5));

    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), DRIVEN + 1);
    const godot::Dictionary taken = pool->release_deferred_operator(slot);
    NETW_CHECK_EQ(int(taken[StringName("contacts")]), 5);
    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), -1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Deferral] dropping abandons the row without "
    "handing it back"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->hold_deferred_operator(slot, DRIVEN, 77, detail_of(3));
    pool->drop_deferred_operator(slot);

    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), -1);
    NETW_CHECK_EQ(pool->deferred_operator_recv_tick(slot), -1);
    CHECK(pool->release_deferred_operator(slot).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Deferral] holding a basis below zero is "
    "holding nothing, because held-ness IS a basis at or above zero"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    pool->hold_deferred_operator(slot, -1, 77, detail_of(3));

    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), -1);
    CHECK(pool->release_deferred_operator(slot).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Deferral] a held row belongs to its slot, "
    "and an unopened slot holds none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t waiting = driving_slot(pool);
    const int64_t quiet = driving_slot(pool);

    pool->hold_deferred_operator(waiting, DRIVEN, 77, detail_of(3));
    pool->hold_deferred_operator(-1, DRIVEN, 77, detail_of(3));

    NETW_CHECK_EQ(pool->deferred_operator_basis(quiet), -1);
    NETW_CHECK_EQ(pool->deferred_operator_basis(-1), -1);
    NETW_CHECK_EQ(pool->deferred_operator_basis(waiting), DRIVEN);
}

namespace {

godot::PackedStringArray roster(const char *p_first, const char *p_second) {
    godot::PackedStringArray out;
    out.push_back(p_first);
    out.push_back(p_second);
    return out;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Topology] a roster is sorted on the way in, "
    "so two peers that reached it by different routes agree"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t one = driving_slot(pool);
    const int64_t other = driving_slot(pool);

    pool->set_island_participants(one, roster("beta", "alpha"));
    pool->set_island_participants(other, roster("alpha", "beta"));

    const godot::PackedStringArray held = pool->island_participants(one);
    NETW_CHECK_EQ(held.size(), 2);
    if (held.size() == 2) {
        const bool first = String(held[0]) == String("alpha");
        const bool second = String(held[1]) == String("beta");
        CHECK(first);
        CHECK(second);
    }
    const bool agreed = held == pool->island_participants(other);
    CHECK(agreed);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Topology] the facts carry the tier and world "
    "version the caller names, and the roster the slot holds"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    pool->set_island_participants(slot, roster("beta", "alpha"));

    const godot::Dictionary facts = pool->topology_facts(slot, 2, 9);

    NETW_CHECK_EQ(int(facts[StringName("schedule")]), 2);
    NETW_CHECK_EQ(int(facts[StringName("epoch")]), 9);
    const godot::PackedStringArray named = facts[StringName("participants")];
    NETW_CHECK_EQ(named.size(), 2);
    if (named.size() == 2) {
        const bool leads = String(named[0]) == String("alpha");
        CHECK(leads);
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Topology] one roster fingerprints alike "
    "however it was ordered, and a moved roster fingerprints apart"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t one = driving_slot(pool);
    const int64_t other = driving_slot(pool);

    pool->set_island_participants(one, roster("beta", "alpha"));
    pool->set_island_participants(other, roster("alpha", "beta"));

    const int64_t reached = netw::prediction_core::fact_fingerprint(
        pool->topology_facts(one, 2, 9)
    );
    NETW_CHECK_EQ(
        reached,
        netw::prediction_core::fact_fingerprint(
            pool->topology_facts(other, 2, 9)
        )
    );

    pool->set_island_participants(other, roster("alpha", "gamma"));
    const int64_t moved = netw::prediction_core::fact_fingerprint(
        pool->topology_facts(other, 2, 9)
    );
    const bool apart = reached != moved;
    CHECK(apart);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Topology] a roster belongs to its slot, and "
    "a slot that was never opened holds none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t named = driving_slot(pool);
    const int64_t quiet = driving_slot(pool);

    pool->set_island_participants(named, roster("beta", "alpha"));
    pool->set_island_participants(-1, roster("beta", "alpha"));

    NETW_CHECK_EQ(pool->island_participants(quiet).size(), 0);
    NETW_CHECK_EQ(pool->island_participants(-1).size(), 0);
    NETW_CHECK_EQ(pool->island_participants(named).size(), 2);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] sealing a transition files the cell "
    "with the command its OWN provenance names, and a closed row is sealed "
    "once"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    Dictionary authored;
    authored[StringName("button")] = 7;
    lane->record_input(DRIVEN, authored);

    drive_one(pool, slot);
    CHECK(pool->seal_transition(
                  slot,
                  DRIVEN,
                  godot::Dictionary(),
                  godot::Dictionary(),
                  PackedStringArray()
    )
              .is_empty());

    NETW_CHECK_EQ(
        pool->joint_provenance_at(slot, DRIVEN),
        int(netw::NetwPredict::CELL_PROVENANCE_AUTHORED)
    );
    const Dictionary filed = pool->joint_command_at(slot, DRIVEN);
    NETW_CHECK_EQ(int64_t(filed[StringName("button")]), 7);

    const int64_t quiet = driving_slot(pool);
    Ref<netw::NetwTimeline> silent = netw::NetwTimeline::create(64);
    pool->bind_timeline(quiet, silent);
    drive_one(pool, quiet);
    pool->seal_transition(
        quiet,
        DRIVEN,
        godot::Dictionary(),
        godot::Dictionary(),
        PackedStringArray()
    );
    NETW_CHECK_EQ(
        pool->joint_provenance_at(quiet, DRIVEN),
        int(netw::NetwPredict::CELL_PROVENANCE_COAST)
    );

    CHECK(pool->seal_transition(
                  slot + 9000,
                  DRIVEN,
                  godot::Dictionary(),
                  godot::Dictionary(),
                  PackedStringArray()
    )
              .is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] the pool's ack lane names a cause "
    "only from complete peer evidence, and an incomplete run still judges"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    drive_one(pool, slot);

    const netw::predict::EvidenceRow peer = netw::evidence_row(
        PRE_FP + 1,
        0,
        0,
        POST_FP + 1,
        PRE_POSE + 1,
        PRE_MOMENTUM,
        PRE_CONTROLLER,
        POST_POSE,
        POST_MOMENTUM,
        POST_CONTROLLER,
        0,
        0,
        0,
        int(netw::predict::EVIDENCE_WITNESS),
        true
    );
    const netw::predict::AckVerdict named
        = pool->admit_ack(slot, DRIVEN, peer, false);
    CHECK(named.evidence_complete);
    NETW_CHECK_EQ(
        int(named.attribution),
        int(netw::NetwPredictJournal::PRE_STATE)
    );

    const int64_t other = driving_slot(pool);
    drive_one(pool, other);
    const netw::predict::EvidenceRow partial = netw::
        evidence_row(0, 0, 0, POST_FP + 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false);
    const netw::predict::AckVerdict unnamed
        = pool->admit_ack(other, DRIVEN, partial, false);
    CHECK_FALSE(unnamed.evidence_complete);
    NETW_CHECK_EQ(
        int(unnamed.attribution),
        int(netw::NetwPredictJournal::UNKNOWN)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a relayed command supersedes a guess "
    "about it once, and a re-sent one changes nothing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);

    Array keys;
    keys.append(StringName("button"));

    netw::predict::CommandFrameRecord frame;
    REQUIRE(netw::predict::CommandFrameRecord::open(input_schema(), frame));
    frame.set_epoch(1);
    frame.append_transition(6, 6, true);
    Array only;
    only.append(3);
    frame.append_payload(only);

    const PackedInt64Array first
        = pool->admit_relayed_frame(slot, frame, keys, false);
    NETW_CHECK_EQ(first[NetwPredictionEngine::RELAY_RECORDED], 1);
    NETW_CHECK_EQ(first[NetwPredictionEngine::RELAY_DROPPED_LATE], 0);
    NETW_CHECK_EQ(
        pool->joint_provenance_at(slot, 6),
        int(netw::NetwPredict::CELL_PROVENANCE_RELAYED)
    );
    NETW_CHECK_EQ(pool->newest_matrix_transition_of(slot), 6);
    NETW_CHECK_EQ(pool->relayed_epoch_of(slot), 1);

    const PackedInt64Array again
        = pool->admit_relayed_frame(slot, frame, keys, false);
    NETW_CHECK_EQ(again[NetwPredictionEngine::RELAY_RECORDED], 0);

    netw::predict::CommandFrameRecord stale;
    REQUIRE(netw::predict::CommandFrameRecord::open(input_schema(), stale));
    stale.set_epoch(1);
    stale.append_transition(6 - netw::predict::TAPE_HISTORY_LIMIT, 0, true);
    stale.append_payload(only);
    const PackedInt64Array late
        = pool->admit_relayed_frame(slot, stale, keys, false);
    NETW_CHECK_EQ(late[NetwPredictionEngine::RELAY_DROPPED_LATE], 1);
    NETW_CHECK_EQ(late[NetwPredictionEngine::RELAY_RECORDED], 0);

    netw::predict::CommandFrameRecord next;
    REQUIRE(netw::predict::CommandFrameRecord::open(input_schema(), next));
    next.set_epoch(2);
    next.append_transition(6, 6, true);
    next.append_payload(only);
    const PackedInt64Array rekeyed
        = pool->admit_relayed_frame(slot, next, keys, true);
    NETW_CHECK_EQ(rekeyed[NetwPredictionEngine::RELAY_RECORDED], 1);
    NETW_CHECK_EQ(pool->relayed_epoch_of(slot), 2);
    NETW_CHECK_EQ(pool->joint_epoch_floor_of(slot), 6);

    NETW_CHECK_EQ(
        int(pool->admit_relayed_frame(slot + 9000, next, keys, false).size()),
        int(NetwPredictionEngine::RELAY_COLUMN_COUNT)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a command frame's transitions are "
    "one contiguous run, because the header ships a base and a count and a "
    "gap would make every later row name the wrong transition"
) {
    netw::predict::CommandFrameRecord frame;
    REQUIRE(netw::predict::CommandFrameRecord::open(input_schema(), frame));

    CHECK(frame.append_transition(6, 6, false));
    CHECK(frame.append_transition(7, 7, false));
    NETW_CHECK_EQ(frame.transition_count(), 2);

    CHECK_FALSE(frame.append_transition(9, 9, false));
    CHECK_FALSE(frame.append_transition(7, 7, false));
    NETW_CHECK_EQ(frame.transition_count(), 2);

    CHECK(frame.append_transition(8, 8, false));
    NETW_CHECK_EQ(frame.transition_count(), 3);
    NETW_CHECK_EQ(frame.transition_at(2).index, 8);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] the owner ships the acknowledgement "
    "floor forward and claims a fingerprint only for what it has closed"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    Array keys;
    keys.append(StringName("button"));

    CHECK(pool->build_command_frame(slot, input_schema(), keys, 8).is_empty());

    for (int64_t at = 0; at < 4; ++at) {
        Dictionary command;
        command[StringName("button")] = int(at + 1);
        lane->record_input(at, command);
        pool->tape_author(slot, at, true);
    }

    netw::predict::CommandFrameRecord shipped;
    REQUIRE(
        netw::predict::CommandFrameRecord::decode(
            input_schema(),
            pool->build_command_frame(slot, input_schema(), keys, 8),
            shipped
        )
    );
    NETW_CHECK_EQ(shipped.transition_count(), 4);
    NETW_CHECK_EQ(shipped.payload_count(), 4);
    NETW_CHECK_EQ(shipped.evidence_count(), 0);

    netw::predict::CommandFrameRecord windowed;
    REQUIRE(
        netw::predict::CommandFrameRecord::decode(
            input_schema(),
            pool->build_command_frame(slot, input_schema(), keys, 2),
            windowed
        )
    );
    NETW_CHECK_EQ(windowed.transition_count(), 2);
    NETW_CHECK_EQ(windowed.transition_at(0).index, 2);

    pool->set_ack_of_acks(slot, 1);
    netw::predict::CommandFrameRecord floored;
    REQUIRE(
        netw::predict::CommandFrameRecord::decode(
            input_schema(),
            pool->build_command_frame(slot, input_schema(), keys, 8),
            floored
        )
    );
    NETW_CHECK_EQ(floored.transition_count(), 2);
    NETW_CHECK_EQ(floored.transition_at(0).index, 2);
    NETW_CHECK_EQ(floored.ack_of_acks(), 1);

    pool->set_ack_of_acks(slot, -1);
    for (int64_t at = 0; at < 3; ++at) {
        pool->record_input(slot, at, int32_t(at + 1));
        const netw::predict::DriveRecord drive = pool->replay_drive(
            slot,
            godot::Dictionary(),
            at,
            at,
            int(netw::DriveKind::FRESH),
            at,
            at,
            1.0 / 60.0,
            1,
            int(PRE_FP),
            int(PRE_POSE),
            int(PRE_MOMENTUM),
            int(PRE_CONTROLLER),
            int(RAW_FP),
            0,
            false
        );
        if (at < 2) {
            pool->close_drive(
                slot,
                drive.transition,
                int(POST_FP),
                int(POST_POSE),
                int(POST_MOMENTUM),
                int(POST_CONTROLLER)
            );
        }
    }
    NETW_CHECK_EQ(pool->journal_last_closed(slot), 1);

    netw::predict::CommandFrameRecord claimed;
    REQUIRE(
        netw::predict::CommandFrameRecord::decode(
            input_schema(),
            pool->build_command_frame(slot, input_schema(), keys, 8),
            claimed
        )
    );
    NETW_CHECK_EQ(claimed.transition_count(), 4);
    NETW_CHECK_EQ(claimed.evidence_count(), 2);

    CHECK(pool->build_command_frame(slot + 9000, input_schema(), keys, 8)
              .is_empty());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Claim] a command epoch re-keys everything "
    "the old numbering answered for, and a tick lane files each fresh command "
    "at its own label"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = driving_slot(pool);
    Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);

    Array keys;
    keys.append(StringName("button"));

    netw::predict::CommandFrameRecord frame;
    REQUIRE(netw::predict::CommandFrameRecord::open(input_schema(), frame));
    frame.set_epoch(3);
    frame.set_ack_of_acks(11);
    frame.append_transition(4, 4, true);
    frame.append_transition(5, 5, true);
    Array first;
    first.append(7);
    Array second;
    second.append(9);
    frame.append_payload(first);
    frame.append_payload(second);

    const PackedInt64Array held = pool->admit_command_frame(slot, frame, keys);
    NETW_CHECK_EQ(int(held.size()), 2);
    NETW_CHECK_EQ(pool->command_epoch_of(slot), 3);
    NETW_CHECK_EQ(pool->owner_ack_floor_of(slot), 11);
    NETW_CHECK_EQ(pool->replay_cursor_of(slot), held[0]);
    NETW_CHECK_EQ(pool->next_input_tick_of(slot), 4);
    CHECK(lane->has_input_at(4));
    CHECK(lane->has_input_at(5));

    pool->set_ack(slot, 99);
    netw::predict::CommandFrameRecord later;
    REQUIRE(netw::predict::CommandFrameRecord::open(input_schema(), later));
    later.set_epoch(4);
    later.set_ack_of_acks(2);
    later.append_transition(4, 40, true);
    Array only;
    only.append(1);
    later.append_payload(only);

    pool->admit_command_frame(slot, later, keys);
    NETW_CHECK_EQ(pool->command_epoch_of(slot), 4);
    NETW_CHECK_EQ(pool->ack_of(slot), -1);
    NETW_CHECK_EQ(pool->owner_ack_floor_of(slot), 2);

    NETW_CHECK_EQ(
        int(pool->admit_command_frame(slot + 9000, later, keys).size()),
        0
    );
}

} // namespace TestNetwPredictClaimLaws

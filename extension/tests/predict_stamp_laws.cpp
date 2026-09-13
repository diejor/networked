#include "support/netw_test.h"

#include <limits>

#include "netw/api/entity.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_field_recovery.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/predict/engine.hpp"
#include "support/carrier.h"

using namespace godot;

namespace TestNetwPredictStampLaws {

#if defined(NETW_TIER_HOSTED)

namespace {

using godot::PackedInt64Array;
using godot::Ref;
using netw::NetwPredictionEngine;
using netw_test::Carrier;

bool declared_rule(int64_t, const godot::StringName &) {
    return true;
}

bool second_rule(int64_t, const godot::StringName &) {
    return false;
}

netw::predict::AckEvidenceWire ack_record(
    int p_evidence_mask,
    int p_pre_fp,
    int p_c_hash,
    int p_e_digest,
    int p_post_fp,
    int p_topo_fp,
    int p_witness_fp,
    int p_raw_fp,
    int p_family_base
) {
    netw::predict::AckEvidenceWire record;
    record.evidence_mask = uint8_t(p_evidence_mask);
    record.pre_fp = p_pre_fp;
    record.c_hash = p_c_hash;
    record.e_digest = p_e_digest;
    record.post_fp = p_post_fp;
    record.topo_fp = p_topo_fp;
    record.witness_fp = p_witness_fp;
    record.raw_fp = p_raw_fp;
    record.pre_pose_fp = p_family_base;
    record.pre_momentum_fp = p_family_base == 0 ? 0 : p_family_base + 1;
    record.pre_controller_fp = p_family_base == 0 ? 0 : p_family_base + 2;
    record.post_pose_fp = record.pre_pose_fp;
    record.post_momentum_fp = record.pre_momentum_fp;
    record.post_controller_fp = record.pre_controller_fp;
    return record;
}

Ref<netw::NetwQuantizeScalar> coarse() {
    Ref<netw::NetwQuantizeScalar> out;
    out.instantiate();
    out->set_min_limit(-31.75);
    out->set_max_limit(31.75);
    out->set_bit_count(8);
    return out;
}

void declare(
    godot::LocalVector<netw::predict::FieldDecl> &p_out,
    const char *p_key,
    const char *p_channel,
    bool p_quantized
) {
    p_out.push_back(
        netw::field_decl(
            StringName(p_key),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(p_channel),
            0.0,
            false,
            false,
            -1.0,
            -1.0,
            false,
            p_quantized ? Ref<netw::NetwQuantize>(coarse())
                        : Ref<netw::NetwQuantize>(),
            int(godot::Variant::FLOAT)
        )
    );
}

godot::LocalVector<netw::predict::FieldDecl> every_family_populated() {
    godot::LocalVector<netw::predict::FieldDecl> out;
    declare(out, "speed", "throttle", true);
    declare(out, "throttle", "", false);
    declare(out, "latch", "", false);
    return out;
}

Carrier *carrier(double p_speed, double p_throttle) {
    Carrier *out = memnew(Carrier);
    out->define("speed", p_speed);
    out->define("throttle", p_throttle);
    out->define("latch", 7.0);
    return out;
}

int64_t stamped_slot(NetwPredictionEngine *p_pool, Carrier *p_body) {
    const int64_t slot = p_pool->open(every_family_populated());
    p_pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    p_pool->bind_owner(slot, p_body);
    return slot;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Stamp] the stamp equals the six verbs it "
    "replaces, field for field"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary raw = pool->capture_state(slot);
    const godot::Dictionary canonical = pool->canonicalize_state(slot, raw);
    const int64_t expected_pre = pool->state_fingerprint(slot, canonical);
    const godot::PackedInt32Array expected_families
        = pool->state_family_fingerprints(slot, canonical);

    const PackedInt64Array stamp = pool->open_state_stamp(slot, false);

    NETW_CHECK_EQ(stamp.size(), NetwPredictionEngine::STAMP_COLUMN_COUNT);
    NETW_CHECK_EQ(stamp[NetwPredictionEngine::STAMP_BOUND], 1);
    NETW_CHECK_EQ(stamp[NetwPredictionEngine::STAMP_PRE_FP], expected_pre);
    NETW_CHECK_EQ(
        stamp[NetwPredictionEngine::STAMP_POSE_FP],
        expected_families[0]
    );
    NETW_CHECK_EQ(
        stamp[NetwPredictionEngine::STAMP_MOMENTUM_FP],
        expected_families[1]
    );
    NETW_CHECK_EQ(
        stamp[NetwPredictionEngine::STAMP_CONTROLLER_FP],
        expected_families[2]
    );
    const bool three_apart = expected_families[0] != expected_families[1]
        && expected_families[1] != expected_families[2]
        && expected_families[0] != expected_families[2];
    CHECK(three_apart);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Stamp] the raw column is withheld unless it "
    "is asked for, and it names its own evidence"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const PackedInt64Array silent = pool->open_state_stamp(slot, false);
    NETW_CHECK_EQ(silent[NetwPredictionEngine::STAMP_RAW_FP], 0);
    NETW_CHECK_EQ(silent[NetwPredictionEngine::STAMP_EVIDENCE_MASK], 0);

    const PackedInt64Array asked = pool->open_state_stamp(slot, true);
    NETW_CHECK_EQ(
        asked[NetwPredictionEngine::STAMP_EVIDENCE_MASK],
        int(netw::predict::EVIDENCE_RAW)
    );
    const bool raw_taken = asked[NetwPredictionEngine::STAMP_RAW_FP] != 0;
    CHECK(raw_taken);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Stamp] the whole-state and family columns "
    "canonicalize inside the fingerprint, so the stamp pays for it once"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.3, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary raw = pool->capture_state(slot);
    const godot::Dictionary canonical = pool->canonicalize_state(slot, raw);
    const bool quantizer_moved_it = double(canonical[StringName("speed")])
        != double(raw[StringName("speed")]);
    CHECK(quantizer_moved_it);

    NETW_CHECK_EQ(
        pool->state_fingerprint(slot, raw),
        pool->state_fingerprint(slot, canonical)
    );
    NETW_CHECK_EQ(
        pool->state_family_fingerprints(slot, raw)[0],
        pool->state_family_fingerprints(slot, canonical)[0]
    );

    const PackedInt64Array stamp = pool->open_state_stamp(slot, false);
    NETW_CHECK_EQ(
        stamp[NetwPredictionEngine::STAMP_PRE_FP],
        pool->state_fingerprint(slot, canonical)
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Stamp] the raw column equals the causal "
    "fingerprint of the RAW state, not of the canonical one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.3, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary every_field_is_causal = pool->capture_state(slot);
    const int64_t expected
        = netw::prediction_core::raw_state_fingerprint(every_field_is_causal);

    const PackedInt64Array stamp = pool->open_state_stamp(slot, true);

    NETW_CHECK_EQ(stamp[NetwPredictionEngine::STAMP_RAW_FP], expected);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Stamp] a state that moved stamps apart, and "
    "a state that did not stamps alike"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const PackedInt64Array first = pool->open_state_stamp(slot, true);
    const PackedInt64Array again = pool->open_state_stamp(slot, true);
    NETW_CHECK_EQ(
        first[NetwPredictionEngine::STAMP_PRE_FP],
        again[NetwPredictionEngine::STAMP_PRE_FP]
    );

    body->define("speed", 9.5);
    const PackedInt64Array moved = pool->open_state_stamp(slot, true);
    const bool apart = first[NetwPredictionEngine::STAMP_PRE_FP]
        != moved[NetwPredictionEngine::STAMP_PRE_FP];
    CHECK(apart);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Stamp] an unbound slot stamps nothing and "
    "says so, rather than stamping a zero state"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    const PackedInt64Array stamp = pool->open_state_stamp(slot, true);

    NETW_CHECK_EQ(stamp.size(), NetwPredictionEngine::STAMP_COLUMN_COUNT);
    NETW_CHECK_EQ(stamp[NetwPredictionEngine::STAMP_BOUND], 0);
    NETW_CHECK_EQ(stamp[NetwPredictionEngine::STAMP_PRE_FP], 0);

    const PackedInt64Array closed = pool->open_state_stamp(-1, true);
    NETW_CHECK_EQ(closed.size(), NetwPredictionEngine::STAMP_COLUMN_COUNT);
    NETW_CHECK_EQ(closed[NetwPredictionEngine::STAMP_BOUND], 0);
}

namespace {

godot::Dictionary row_of(double p_speed, double p_throttle) {
    godot::Dictionary out;
    out[StringName("speed")] = p_speed;
    out[StringName("throttle")] = p_throttle;
    out[StringName("latch")] = 7.0;
    return out;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Transport] a delta is measured per field, "
    "over the fields the write RESTORES"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary deltas
        = pool->transport_deltas(slot, row_of(4.5, 0.25), row_of(6.5, 0.25));

    NETW_CHECK_EQ(deltas.size(), 3);
    NETW_CHECK_CLOSE(double(deltas[StringName("speed")]), 2.0, 0.000001);
    NETW_CHECK_CLOSE(double(deltas[StringName("throttle")]), 0.0, 0.000001);
    NETW_CHECK_CLOSE(double(deltas[StringName("latch")]), 0.0, 0.000001);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Transport] a write whose own size cannot be "
    "measured is not eligible, and the empty answer says so"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    godot::Dictionary partial;
    partial[StringName("speed")] = 4.5;

    CHECK(pool->transport_deltas(slot, partial, row_of(6.5, 0.25)).is_empty());
    CHECK(pool->transport_deltas(slot, row_of(4.5, 0.25), godot::Dictionary())
              .is_empty());
    CHECK(pool->transport_deltas(-1, row_of(4.5, 0.25), row_of(6.5, 0.25))
              .is_empty());

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Transport] a write that moves nothing is not "
    "a repair, whatever its eligibility says"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    CHECK_FALSE(
        NetwPredictionEngine::transport_moved(
            pool->transport_deltas(slot, row_of(4.5, 0.25), row_of(4.5, 0.25))
        )
    );
    CHECK(
        NetwPredictionEngine::transport_moved(
            pool->transport_deltas(slot, row_of(4.5, 0.25), row_of(4.5, 0.75))
        )
    );
    CHECK_FALSE(NetwPredictionEngine::transport_moved(godot::Dictionary()));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][NonPose] the pose fields are excluded, "
    "because the pose tier is judged by its own distances"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary judged = pool->non_pose_eligibility(
        slot,
        row_of(4.5, 0.25),
        row_of(4.5, 0.25),
        0.001
    );

    const godot::Dictionary fields = judged[StringName("fields")];
    CHECK_FALSE(fields.has(StringName("speed")));
    CHECK(fields.has(StringName("throttle")));
    CHECK(fields.has(StringName("latch")));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][NonPose] one field past its own tolerance "
    "makes the whole answer disagree"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary agreed = pool->non_pose_eligibility(
        slot,
        row_of(4.5, 0.25),
        row_of(4.5, 0.25),
        0.001
    );
    CHECK(bool(agreed[StringName("agrees")]));

    const godot::Dictionary forked = pool->non_pose_eligibility(
        slot,
        row_of(4.5, 0.25),
        row_of(4.5, 9.25),
        0.001
    );
    CHECK_FALSE(bool(forked[StringName("agrees")]));

    const godot::Dictionary fields = forked[StringName("fields")];
    const godot::Dictionary throttle = fields[StringName("throttle")];
    CHECK_FALSE(bool(throttle[StringName("agrees")]));
    NETW_CHECK_CLOSE(double(throttle[StringName("error")]), 9.0, 0.000001);
    NETW_CHECK_CLOSE(double(throttle[StringName("epsilon")]), 0.001, 1e-9);
    const godot::Dictionary latch = fields[StringName("latch")];
    CHECK(bool(latch[StringName("agrees")]));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][NonPose] a field either side did not carry "
    "is unbounded, so it disagrees rather than passing unmeasured"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    godot::Dictionary partial;
    partial[StringName("speed")] = 4.5;
    partial[StringName("latch")] = 7.0;

    const godot::Dictionary judged
        = pool->non_pose_eligibility(slot, partial, row_of(4.5, 0.25), 0.001);

    CHECK_FALSE(bool(judged[StringName("agrees")]));
    const godot::Dictionary fields = judged[StringName("fields")];
    const godot::Dictionary throttle = fields[StringName("throttle")];
    const bool unbounded = double(throttle[StringName("error")])
        == std::numeric_limits<double>::infinity();
    CHECK(unbounded);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][NonPose] a slot that was never opened judges "
    "no field and agrees vacuously"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;

    const godot::Dictionary judged = pool->non_pose_eligibility(
        -1,
        row_of(4.5, 0.25),
        row_of(4.5, 9.25),
        0.001
    );

    CHECK(bool(judged[StringName("agrees")]));
    const godot::Dictionary fields = judged[StringName("fields")];
    NETW_CHECK_EQ(fields.size(), 0);
}

namespace {

godot::LocalVector<netw::predict::FieldDecl> one_angle_field() {
    godot::LocalVector<netw::predict::FieldDecl> out;
    out.push_back(
        netw::field_decl(
            StringName("heading"),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            -1.0,
            -1.0,
            true,
            Ref<netw::NetwQuantize>(),
            int(godot::Variant::FLOAT)
        )
    );
    return out;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Angle] a field declared as an angle is "
    "measured the short way round, everywhere the wiring is read"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(one_angle_field());
    pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );

    CHECK(pool->is_angle(slot, pool->field_slot(slot, StringName("heading"))));

    godot::Dictionary near_zero;
    near_zero[StringName("heading")] = 0.1;
    godot::Dictionary near_tau;
    near_tau[StringName("heading")] = 6.1;

    const godot::Dictionary deltas
        = pool->transport_deltas(slot, near_zero, near_tau);
    const double wrapped = double(deltas[StringName("heading")]);
    const bool short_way = wrapped < 1.0;
    CHECK(short_way);

    const godot::Dictionary judged
        = pool->non_pose_eligibility(slot, near_zero, near_tau, 1.0);
    CHECK(bool(judged[StringName("agrees")]));
}

TEST_CASE(
    "[Networked][Predict][Angle] a field NOT declared as an angle "
    "measures the long way, so the declaration is what decides"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    CHECK_FALSE(
        pool->is_angle(slot, pool->field_slot(slot, StringName("throttle")))
    );

    godot::Dictionary near_zero = row_of(4.5, 0.1);
    godot::Dictionary near_tau = row_of(4.5, 6.1);

    const godot::Dictionary deltas
        = pool->transport_deltas(slot, near_zero, near_tau);
    const double straight = double(deltas[StringName("throttle")]);
    const bool long_way = straight > 5.9;
    CHECK(long_way);

    memdelete(body);
}

namespace {

void declare_full(
    godot::LocalVector<netw::predict::FieldDecl> &p_out,
    const char *p_key,
    int p_class,
    bool p_teleport_only,
    bool p_reconcile_only,
    double p_epsilon_override,
    bool p_quantized
) {
    p_out.push_back(
        netw::field_decl(
            StringName(p_key),
            p_class,
            StringName(),
            0.0,
            p_teleport_only,
            p_reconcile_only,
            p_epsilon_override,
            -1.0,
            false,
            p_quantized ? Ref<netw::NetwQuantize>(coarse())
                        : Ref<netw::NetwQuantize>(),
            int(godot::Variant::FLOAT)
        )
    );
}

godot::Variant stamp_carry_rule(const godot::Variant &p_value) {
    return p_value;
}

int64_t tolerance_slot(NetwPredictionEngine *p_pool) {
    godot::LocalVector<netw::predict::FieldDecl> decl;
    const int causal = int(netw::predict::PropertyClass::CAUSAL);
    const int cosmetic = int(netw::predict::PropertyClass::COSMETIC);
    declare_full(decl, "quantized", causal, false, false, -1.0, true);
    declare_full(decl, "plain", causal, false, false, -1.0, false);
    declare_full(decl, "latched", causal, true, false, 0.25, false);
    declare_full(decl, "reconciled", causal, false, true, -1.0, false);
    declare_full(decl, "cosmetic", cosmetic, false, false, -1.0, false);
    const int64_t slot = p_pool->open(decl);
    p_pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    return slot;
}

double at_field(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    const godot::PackedFloat64Array &p_column,
    const char *p_key
) {
    const int field = p_pool->field_slot(p_slot, StringName(p_key));
    return field >= 0 && field < p_column.size() ? p_column[field] : -99.0;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Tolerance] the correction column excludes "
    "the fields the vote excludes, and every other field falls back"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    const godot::PackedFloat64Array column
        = pool->correction_tolerances(slot, 0.5);

    NETW_CHECK_EQ(column.size(), 5);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "quantized"), 0.5, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "plain"), 0.5, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "latched"), 0.25, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "reconciled"), -1.0, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "cosmetic"), -1.0, 1e-9);
}

TEST_CASE(
    "[Networked][Predict][Tolerance] in domain the meter reads what "
    "the wire can resolve, so an unquantized field tolerates nothing"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    const godot::PackedFloat64Array column = pool->meter_tolerances(
        slot,
        int(netw::predict::Domain::IN_DOMAIN),
        0.5
    );

    NETW_CHECK_EQ(column.size(), 5);
    const bool quantized_tolerates
        = at_field(pool, slot, column, "quantized") > 0.0;
    CHECK(quantized_tolerates);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "plain"), 0.0, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "latched"), 0.25, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "reconciled"), -1.0, 1e-9);
}

TEST_CASE(
    "[Networked][Predict][Tolerance] out of domain the meter reads the "
    "declared tolerance instead, because the wire is not what disagreed"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    const godot::PackedFloat64Array column = pool->meter_tolerances(
        slot,
        int(netw::predict::Domain::OUT_OF_DOMAIN),
        0.5
    );

    NETW_CHECK_CLOSE(at_field(pool, slot, column, "quantized"), 0.5, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "plain"), 0.5, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "latched"), 0.25, 1e-9);
    NETW_CHECK_CLOSE(at_field(pool, slot, column, "reconciled"), -1.0, 1e-9);
}

TEST_CASE(
    "[Networked][Predict][Tolerance] the meter counts tolerances of "
    "the error, and a field the meter declares none for is not counted"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    godot::Dictionary divergence;
    divergence[StringName("quantized")] = 0.0;
    divergence[StringName("plain")] = 0.0;
    divergence[StringName("latched")] = 1.25;
    divergence[StringName("cosmetic")] = 0.0;

    NETW_CHECK_EQ(
        pool->meter_of(
            slot,
            divergence,
            int(netw::predict::Domain::OUT_OF_DOMAIN),
            0.5
        ),
        4
    );

    divergence[StringName("latched")] = 0.0;
    divergence[StringName("reconciled")] = 99.0;
    NETW_CHECK_EQ(
        pool->meter_of(
            slot,
            divergence,
            int(netw::predict::Domain::OUT_OF_DOMAIN),
            0.5
        ),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Dissipate] a no-write window is not decided "
    "where no episode is gathering evidence, and a decision it never took is "
    "not a refusal"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    const int out_of_domain = int(netw::predict::Domain::OUT_OF_DOMAIN);

    NETW_CHECK_EQ(
        pool->try_dissipate(
            slot,
            4,
            1,
            out_of_domain,
            false,
            0.5,
            false,
            StringName("body"),
            0
        ),
        int(NetwPredictionEngine::DISSIPATE_UNDECIDED)
    );
    NETW_CHECK_EQ(
        pool->episode_stats(
            slot
        )[NetwPredictionEngine::STAT_EPISODE_WRITE_COUNT],
        0
    );

    NETW_CHECK_EQ(
        pool->try_dissipate(
            slot + 9000,
            4,
            1,
            out_of_domain,
            false,
            0.5,
            false,
            StringName("body"),
            0
        ),
        int(NetwPredictionEngine::DISSIPATE_UNDECIDED)
    );
}

TEST_CASE(
    "[Networked][Predict][Dissipate] momentum and everything else are "
    "counted apart, because only one of them a no-write can answer"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    const int out_of_domain = int(netw::predict::Domain::OUT_OF_DOMAIN);

    godot::Dictionary quiet;
    quiet[StringName("plain")] = 0.0;
    quiet[StringName("latched")] = 0.0;
    const godot::Dictionary settled
        = pool->dissipate_fields(slot, quiet, out_of_domain, 0.5);
    CHECK_FALSE(bool(settled[StringName("momentum_active")]));
    CHECK_FALSE(bool(settled[StringName("other_active")]));

    godot::Dictionary momentum_only;
    momentum_only[StringName("plain")] = 0.0;
    momentum_only[StringName("latched")] = 9.0;
    const godot::Dictionary drifting
        = pool->dissipate_fields(slot, momentum_only, out_of_domain, 0.5);
    CHECK(bool(drifting[StringName("momentum_active")]));
    CHECK_FALSE(bool(drifting[StringName("other_active")]));

    godot::Dictionary elsewhere;
    elsewhere[StringName("plain")] = 9.0;
    elsewhere[StringName("latched")] = 0.0;
    const godot::Dictionary forked
        = pool->dissipate_fields(slot, elsewhere, out_of_domain, 0.5);
    CHECK_FALSE(bool(forked[StringName("momentum_active")]));
    CHECK(bool(forked[StringName("other_active")]));
}

TEST_CASE(
    "[Networked][Predict][Dissipate] a trigger-excluded field is "
    "judged only when it is also withheld, and unmeasured fields are skipped"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    const int out_of_domain = int(netw::predict::Domain::OUT_OF_DOMAIN);

    godot::Dictionary loud;
    loud[StringName("reconciled")] = 99.0;
    loud[StringName("latched")] = 9.0;
    const godot::Dictionary judged
        = pool->dissipate_fields(slot, loud, out_of_domain, 0.5);

    const godot::Dictionary fields = judged[StringName("fields")];
    CHECK_FALSE(fields.has(StringName("reconciled")));
    CHECK(fields.has(StringName("latched")));
    CHECK_FALSE(fields.has(StringName("plain")));
    CHECK_FALSE(bool(judged[StringName("other_active")]));
}

TEST_CASE(
    "[Networked][Predict][Digest] a slot with no open episode digests "
    "to nothing, so a reader cannot mistake it for one that settled"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    CHECK(pool->episode_digest(slot).is_empty());
    CHECK(pool->episode_digest(-1).is_empty());
}

TEST_CASE(
    "[Networked][Predict][Digest] the revision counts stamps, so a "
    "reader detects a change without detaching the record"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    const int64_t other = tolerance_slot(pool);

    NETW_CHECK_EQ(pool->episode_revision(slot), 0);

    pool->stamp_episode_revision(slot);
    pool->stamp_episode_revision(slot);

    NETW_CHECK_EQ(pool->episode_revision(slot), 2);
    NETW_CHECK_EQ(pool->episode_revision(other), 0);
    NETW_CHECK_EQ(pool->episode_revision(-1), 0);
}

TEST_CASE(
    "[Networked][Predict][Digest] an open episode digests its "
    "identity, its generator and the SIZE of its evidence"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    REQUIRE(
        pool->open_episode(slot, 7, int(netw::predict::Attribution::CLOSURE))
    );
    pool->stamp_episode_revision(slot);

    const godot::Dictionary digest = pool->episode_digest(slot);
    CHECK_FALSE(digest.is_empty());
    NETW_CHECK_EQ(int(digest[StringName("revision")]), 1);

    const godot::Dictionary generator = digest[StringName("generator")];
    NETW_CHECK_EQ(int(generator[StringName("transition")]), 7);
    NETW_CHECK_EQ(
        int(generator[StringName("boundary")]),
        int(netw::predict::Attribution::CLOSURE)
    );

    const godot::Dictionary evidence = digest[StringName("evidence")];
    NETW_CHECK_EQ(int(evidence[StringName("comparisons")]), 0);
    NETW_CHECK_EQ(int(evidence[StringName("writes")]), 0);
    NETW_CHECK_EQ(int(evidence[StringName("decisions")]), 0);
    CHECK(digest.has(StringName("last_operator")));
    CHECK(digest.has(StringName("disposition")));
}

namespace {

godot::Dictionary lint_of(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    bool p_has_corridor = false
) {
    return p_pool->reachability_report(
        p_slot,
        StringName("racer"),
        0.05,
        1.5,
        0,
        StringName("default"),
        0,
        6,
        p_has_corridor
    );
}

godot::PackedStringArray finding_fields(
    const godot::Dictionary &p_report,
    const char *p_code
) {
    const Array findings = p_report[StringName("findings")];
    for (int at = 0; at < findings.size(); ++at) {
        const godot::Dictionary held = findings[at];
        if (held[StringName("code")] == StringName(p_code)) {
            return held[StringName("fields")];
        }
    }
    return godot::PackedStringArray();
}

bool has_finding(const godot::Dictionary &p_report, const char *p_code) {
    const Array findings = p_report[StringName("findings")];
    for (int at = 0; at < findings.size(); ++at) {
        const godot::Dictionary held = findings[at];
        if (held[StringName("code")] == StringName(p_code)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Pose] a field is advanced through its own "
    "channel, and one with no channel restores verbatim"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    godot::Dictionary payload;
    payload[StringName("speed")] = 1.0;
    payload[StringName("throttle")] = 2.0;
    payload[StringName("latch")] = 7.0;

    const godot::Dictionary held = pool->project_state(slot, payload, 0.0);
    NETW_CHECK_CLOSE(double(held[StringName("speed")]), 1.0, 1e-9);

    const godot::Dictionary advanced = pool->project_state(slot, payload, 0.5);
    NETW_CHECK_CLOSE(double(advanced[StringName("speed")]), 2.0, 1e-9);
    NETW_CHECK_CLOSE(double(advanced[StringName("throttle")]), 2.0, 1e-9);
    NETW_CHECK_CLOSE(double(advanced[StringName("latch")]), 7.0, 1e-9);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] the pose error is measured per field "
    "over the tier, and an unmeasurable field drops out"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary errors
        = pool->pose_errors(slot, row_of(4.5, 0.25), row_of(6.5, 0.25));

    CHECK(errors.has(StringName("speed")));
    CHECK_FALSE(errors.has(StringName("latch")));
    NETW_CHECK_CLOSE(double(errors[StringName("speed")]), 2.0, 1e-9);

    godot::Dictionary partial;
    partial[StringName("throttle")] = 0.25;
    CHECK(pool->pose_errors(slot, partial, row_of(6.5, 0.25)).is_empty());

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Ack] an incomplete run hands over zeros "
    "rather than the columns it happens to carry, and names no cause"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    netw::predict::AckFrame frame;
    frame.base = 3;
    frame.records.push_back(ack_record(
        int(netw::predict::EVIDENCE_WITNESS),
        21,
        22,
        23,
        24,
        25,
        26,
        27,
        11
    ));

    CHECK(pool->admit_ack_row(slot, frame, 3, 0, true, false).transition < 0);

    pool->record_input(slot, 3, 3);
    REQUIRE(pool->replay_drive(
                    slot,
                    godot::Dictionary(),
                    3,
                    3,
                    int(netw::DriveKind::FRESH),
                    3,
                    3,
                    1.0 / 60.0,
                    1,
                    0,
                    0,
                    0,
                    0
    )
                .ran);

    const netw::predict::AckVerdict incomplete
        = pool->admit_ack_row(slot, frame, 3, 0, false, false);
    CHECK_FALSE(incomplete.evidence_complete);

    const netw::predict::AckVerdict whole
        = pool->admit_ack_row(slot, frame, 3, 0, true, false);
    CHECK(whole.evidence_complete);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Replay] a replay re-runs every unacked "
    "entry once, in ascending order, and reports that depth"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    NETW_CHECK_EQ(
        pool->replay_authored_entries(
            slot + 9000,
            0,
            godot::Dictionary(),
            godot::PackedStringArray(),
            Callable()
        ),
        0
    );

    for (int64_t at = 1; at <= 3; ++at) {
        pool->record_input(slot, at, at);
        REQUIRE(pool->replay_drive(
                        slot,
                        godot::Dictionary(),
                        at,
                        at,
                        int(netw::DriveKind::FRESH),
                        at,
                        at,
                        1.0 / 60.0,
                        1,
                        0,
                        0,
                        0,
                        0
        )
                    .ran);
    }

    const int held = pool->replay_entries(slot, 0).size();
    NETW_CHECK_EQ(held, 3);
    NETW_CHECK_EQ(
        pool->replay_authored_entries(
            slot,
            0,
            godot::Dictionary(),
            godot::PackedStringArray(),
            Callable()
        ),
        held
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Ack] a run counts one verdict per row it "
    "JUDGED, and names the first transition that disagreed"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    netw::predict::AckFrame frame;
    frame.base = 3;
    frame.records.push_back(ack_record(0, 0, 0, 0, 4242, 0, 0, 0, 0));
    frame.records.push_back(ack_record(0, 0, 0, 0, 0, 0, 0, 0, 0));

    for (int64_t at = 3; at <= 4; ++at) {
        pool->record_input(slot, at, at);
        REQUIRE(pool->replay_drive(
                        slot,
                        godot::Dictionary(),
                        at,
                        at,
                        int(netw::DriveKind::FRESH),
                        at,
                        at,
                        1.0 / 60.0,
                        1,
                        0,
                        0,
                        0,
                        0
        )
                    .ran);
    }

    const godot::PackedInt64Array counts
        = pool->admit_ack_run(slot, frame, Callable(), Callable());

    NETW_CHECK_EQ(counts[NetwPredictionEngine::ACK_RUN_OF_ACKS], 4);
    NETW_CHECK_EQ(counts[NetwPredictionEngine::ACK_RUN_SUBSTITUTED], 0);
    NETW_CHECK_EQ(counts[NetwPredictionEngine::ACK_RUN_VERIFIED], 2);
    NETW_CHECK_EQ(counts[NetwPredictionEngine::ACK_RUN_MISMATCHED], 1);
    NETW_CHECK_EQ(counts[NetwPredictionEngine::ACK_RUN_FIRST_DIVERGENT], 3);

    const godot::PackedInt64Array again
        = pool->admit_ack_run(slot, frame, Callable(), Callable());
    NETW_CHECK_EQ(again[NetwPredictionEngine::ACK_RUN_VERIFIED], 0);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Ack] a receipt separates a frame that did "
    "not decode from one this tape does not speak for"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    netw::predict::AckFrame frame;
    frame.epoch = 7;
    frame.base = 0;
    frame.records.push_back(ack_record(0, 0, 0, 0, 4242, 0, 0, 0, 0));
    const PackedByteArray payload = netw::predict::encode_ack(frame);
    REQUIRE_FALSE(payload.is_empty());

    PackedByteArray garbage;
    garbage.push_back(255);
    garbage.push_back(255);
    garbage.push_back(255);

    const PackedInt64Array undecoded = pool->receive_ack_frame(
        slot,
        garbage,
        7,
        false,
        Callable(),
        Callable()
    );
    NETW_CHECK_EQ(
        undecoded[NetwPredictionEngine::ACK_RUN_RECEIPT],
        int64_t(NetwPredictionEngine::ACK_RECEIPT_UNDECODED)
    );

    const PackedInt64Array foreign = pool->receive_ack_frame(
        slot,
        payload,
        9,
        false,
        Callable(),
        Callable()
    );
    NETW_CHECK_EQ(
        foreign[NetwPredictionEngine::ACK_RUN_RECEIPT],
        int64_t(NetwPredictionEngine::ACK_RECEIPT_FOREIGN_EPOCH)
    );
    NETW_CHECK_EQ(foreign[NetwPredictionEngine::ACK_RUN_OF_ACKS], 0);

    const PackedInt64Array admitted = pool->receive_ack_frame(
        slot,
        payload,
        7,
        false,
        Callable(),
        Callable()
    );
    NETW_CHECK_EQ(
        admitted[NetwPredictionEngine::ACK_RUN_RECEIPT],
        int64_t(NetwPredictionEngine::ACK_RECEIPT_ADMITTED)
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Ack] charging a divergence names the "
    "transition it was charged AT, which the retry keys on"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    netw::predict::AckFrame frame;
    frame.base = 3;
    frame.records.push_back(ack_record(0, 0, 0, 0, 0, 0, 0, 0, 0));

    pool->charge_divergence(
        slot,
        frame,
        5,
        0,
        true,
        int(netw::predict::Attribution::EXECUTION)
    );

    NETW_CHECK_EQ(
        pool->attribution_of(slot),
        int(netw::predict::Attribution::EXECUTION)
    );
    NETW_CHECK_EQ(pool->attributed_transition_of(slot), 5);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Witness] an operator waits only while an "
    "episode is open with an undecided operator to wait FOR"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    NETW_CHECK_EQ(
        pool->defer_operator_for_witness(
            slot,
            9,
            3,
            godot::Dictionary(),
            true,
            false
        ),
        int(NetwPredictionEngine::DEFER_REFUSED)
    );

    REQUIRE(pool->open_episode(slot, 3, 0));
    NETW_CHECK_EQ(
        pool->defer_operator_for_witness(
            slot,
            9,
            3,
            godot::Dictionary(),
            false,
            true
        ),
        int(NetwPredictionEngine::DEFER_REFUSED)
    );

    NETW_CHECK_EQ(
        pool->defer_operator_for_witness(
            slot,
            9,
            3,
            godot::Dictionary(),
            true,
            false
        ),
        int(NetwPredictionEngine::DEFER_HELD)
    );
    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), 3);
    NETW_CHECK_EQ(pool->deferred_operator_recv_tick(slot), 9);

    NETW_CHECK_EQ(
        pool->defer_operator_for_witness(
            slot,
            10,
            3,
            godot::Dictionary(),
            true,
            false
        ),
        int(NetwPredictionEngine::DEFER_HELD)
    );
    NETW_CHECK_EQ(pool->deferred_operator_recv_tick(slot), 10);

    NETW_CHECK_EQ(
        pool->defer_operator_for_witness(
            slot,
            11,
            4,
            godot::Dictionary(),
            true,
            false
        ),
        int(NetwPredictionEngine::DEFER_DISPLACED)
    );
    NETW_CHECK_EQ(pool->deferred_operator_basis(slot), -1);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Axes] a reconfigure takes every declared "
    "scalar off the handle and leaves the two RESOLVED axes alone"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);
    Ref<netw::NetwPredictionHandle> handle;
    handle.instantiate();

    CHECK_FALSE(
        pool->reconfigure_from(slot, Ref<netw::NetwPredictionHandle>())
    );
    CHECK_FALSE(pool->reconfigure_from(slot + 9000, handle));

    handle->set_schedule(
        static_cast<netw::NetwPredict::Schedule>(int(netw::Schedule::TICK))
    );
    handle->set_snap_restore(int(netw::RestoreMode::EXTRAPOLATED));
    handle->set_max_restore_ticks(9);
    handle->set_divergence_epsilon(0.75);
    handle->set_teleport_threshold(3.5);
    handle->set_collision_cooldown_ticks(11);

    CHECK(pool->reconfigure_from(slot, handle));

    NETW_CHECK_EQ(pool->role_of(slot), int(netw::Role::PREDICT));
    NETW_CHECK_EQ(pool->correction_of(slot), int(netw::CorrectionMode::SNAP));
    NETW_CHECK_EQ(pool->island_of(slot), NetwPredictionEngine::ISLAND_NONE);
    NETW_CHECK_CLOSE(pool->epsilon_of_slot(slot), 0.75, 1e-9);
    NETW_CHECK_CLOSE(pool->teleport_threshold_of(slot), 3.5, 1e-9);
    NETW_CHECK_EQ(pool->collision_cooldown_of(slot), 11);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Lane] the ack frontier is the acknowledged "
    "transition floored by the last CLOSED row, and only a consumer has one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    CHECK(pool->ack_frontier(slot, 5).is_empty());
    CHECK(pool->build_command_frame_for(slot).is_empty());

    CHECK(pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));

    CHECK(pool->ack_frontier(slot, -1).is_empty());

    const godot::PackedInt64Array frontier = pool->ack_frontier(slot, 5);
    NETW_CHECK_EQ(frontier.size(), 2);
    NETW_CHECK_EQ(frontier[0], pool->journal_last_closed(slot));
    NETW_CHECK_EQ(
        frontier[1],
        MIN(int64_t(5), pool->journal_last_closed(slot))
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Lane] the command lane's keys are the "
    "VOLATILE columns of the slot's own input set, in set order"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK(pool->input_keys(slot).is_empty());
    CHECK(pool->input_schema(slot) == nullptr);
    netw::predict::CommandFrameRecord undecodable;
    CHECK_FALSE(
        pool->decode_command_frame(slot, godot::PackedByteArray(), undecodable)
    );

    Ref<netw::NetwPropertySet> declared;
    declared.instantiate();
    const Ref<netw::NetwPropertySetColumn> volatile_field
        = netw::NetwPropertySetColumn::create(
            StringName("steer"),
            Ref<netw::NetwQuantize>(),
            false,
            int64_t(godot::Variant::FLOAT)
        );
    const Ref<netw::NetwPropertySetColumn> retained_field
        = netw::NetwPropertySetColumn::create(
            StringName("name"),
            Ref<netw::NetwQuantize>(),
            false,
            int64_t(godot::Variant::STRING)
        );
    retained_field->set_lane(int64_t(netw::NetwPropertySet::RETAINED));
    declared->columns.push_back(volatile_field);
    declared->columns.push_back(retained_field);

    Node *host = memnew(Node);
    const Ref<netw::NetwPropertySetBinding> bound
        = netw::NetwPropertySetBinding::create(declared, host);
    REQUIRE(bound.is_valid());
    pool->bind_property_sets(slot, bound, bound);

    const godot::Array keys = pool->input_keys(slot);
    NETW_CHECK_EQ(keys.size(), 1);
    CHECK(StringName(keys[0]) == StringName("steer"));

    memdelete(host);
}

TEST_CASE(
    "[Networked][Predict][Axes] the two axes are resolved from the "
    "declaration, and a closed delay displays where it would speculate"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);
    Ref<netw::NetwPredictionHandle> handle;
    handle.instantiate();

    pool->declare_axes(slot, false, true, false);
    NETW_CHECK_EQ(
        pool->resolve_axes(slot, handle),
        int(netw::NetwPredict::ROLE_PREDICT)
    );
    NETW_CHECK_EQ(
        handle->get_input_source(),
        int(netw::NetwPredict::INPUT_SOURCE_LOCAL)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(netw::NetwPredict::SIM_MODE_SPECULATIVE)
    );

    handle->set_recovery_policy(
        int(netw::NetwPredict::RECOVERY_POLICY_DELAY_CLOSED)
    );
    pool->resolve_axes(slot, handle);
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(netw::NetwPredict::SIM_MODE_DISPLAY)
    );

    pool->declare_axes(slot, true, false, false);
    pool->resolve_axes(slot, handle);
    NETW_CHECK_EQ(
        handle->get_input_source(),
        int(netw::NetwPredict::INPUT_SOURCE_RECEIVED)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(netw::NetwPredict::SIM_MODE_AUTHORITATIVE)
    );

    pool->declare_axes(slot, false, false, false);
    pool->resolve_axes(slot, handle);
    NETW_CHECK_EQ(
        handle->get_input_source(),
        int(netw::NetwPredict::INPUT_SOURCE_NONE)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(netw::NetwPredict::SIM_MODE_DISPLAY)
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Axes] an entity nobody authors commands for "
    "simulates on its authority and is dragged where something drags it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);
    Ref<netw::NetwPredictionHandle> handle;
    handle.instantiate();

    pool->declare_axes(slot, true, true, true);
    NETW_CHECK_EQ(
        pool->resolve_axes(slot, handle),
        int(netw::NetwPredict::ROLE_HOST_LOCAL)
    );
    NETW_CHECK_EQ(
        handle->get_input_source(),
        int(netw::NetwPredict::INPUT_SOURCE_NONE)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(netw::NetwPredict::SIM_MODE_AUTHORITATIVE)
    );

    pool->declare_axes(slot, false, false, true);
    NETW_CHECK_EQ(
        pool->resolve_axes(slot, handle),
        int(netw::NetwPredict::ROLE_REMOTE)
    );

    pool->note_simulated_by(slot, 77, Callable());
    NETW_CHECK_EQ(
        pool->resolve_axes(slot, handle),
        int(netw::NetwPredict::ROLE_SIMULATE)
    );
    NETW_CHECK_EQ(
        handle->get_sim_mode(),
        int(netw::NetwPredict::SIM_MODE_SPECULATIVE)
    );

    handle->set_recovery_policy(
        int(netw::NetwPredict::RECOVERY_POLICY_DELAY_CLOSED)
    );
    NETW_CHECK_EQ(
        pool->resolve_axes(slot, handle),
        int(netw::NetwPredict::ROLE_REMOTE)
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Reseed] a seed is advanced only where the "
    "declaration asked to extrapolate, and never past the restore ceiling"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);
    pool->record_input(slot, 10, 10);
    REQUIRE(pool->replay_drive(
                    slot,
                    godot::Dictionary(),
                    10,
                    10,
                    int(netw::DriveKind::FRESH),
                    10,
                    10,
                    1.0 / 60.0,
                    1,
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                    true
    )
                .ran);

    const godot::Dictionary seed = row_of(4.5, 0.25);

    const godot::Dictionary exact = pool->advanced_seed(
        slot,
        seed,
        4,
        int(netw::NetwPredict::RESTORE_MODE_EXACT),
        6,
        2.0,
        0.01
    );
    NETW_CHECK_CLOSE(double(exact[StringName("speed")]), 4.5, 1e-9);

    const godot::Dictionary advanced = pool->advanced_seed(
        slot,
        seed,
        4,
        int(netw::NetwPredict::RESTORE_MODE_EXTRAPOLATED),
        6,
        2.0,
        0.01
    );
    const bool seed_moved = double(advanced[StringName("speed")]) > 4.5;
    CHECK(seed_moved);

    const godot::Dictionary held = pool->advanced_seed(
        slot,
        seed,
        4,
        int(netw::NetwPredict::RESTORE_MODE_EXTRAPOLATED),
        0,
        2.0,
        0.01
    );
    NETW_CHECK_CLOSE(double(held[StringName("speed")]), 4.5, 1e-9);

    CHECK(pool->advanced_seed(
                  slot,
                  godot::Dictionary(),
                  4,
                  int(netw::NetwPredict::RESTORE_MODE_EXTRAPOLATED),
                  6,
                  2.0,
                  0.01
    )
              .is_empty());

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Tape] the reported transitions come off the "
    "TAPE where the owner drives and off the command lane where it consumes"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    CHECK(pool->tape_transitions(slot + 9000).is_empty());

    pool->record_input(slot, 3, 3);
    REQUIRE(pool->replay_drive(
                    slot,
                    godot::Dictionary(),
                    3,
                    7,
                    int(netw::DriveKind::FRESH),
                    3,
                    3,
                    1.0 / 60.0,
                    1,
                    0,
                    0,
                    0,
                    0
    )
                .ran);

    const godot::PackedInt64Array span = pool->tape_span(slot);
    NETW_CHECK_EQ(
        pool->tape_transitions(slot).size(),
        int(span[1] - span[0] + 1)
    );

    CHECK(pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));
    NETW_CHECK_EQ(
        pool->tape_transitions(slot).size(),
        pool->command_transitions(slot).size()
    );
    CHECK(pool->tape_transitions(slot).size() != int(span[1] - span[0] + 1));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Ledger] a comparison charges only the "
    "causal, non-excluded fields it MEASURED past their own tolerance"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    const StringName plain("plain");
    const StringName latched("latched");
    const StringName cosmetic("cosmetic");

    godot::Dictionary divergence;
    divergence[plain] = 9.0;
    divergence[latched] = 0.001;
    divergence[cosmetic] = 9.0;
    pool->note_divergence(slot, divergence);

    pool->ledger_note_comparison(slot, false, 4, 0.5);
    NETW_CHECK_EQ(pool->ledger_counts(slot, plain)[0], 0);

    pool->ledger_note_comparison(slot, true, 4, 0.5);
    NETW_CHECK_EQ(pool->ledger_counts(slot, plain)[0], 1);
    NETW_CHECK_EQ(pool->ledger_counts(slot, latched)[0], 0);
    NETW_CHECK_EQ(pool->ledger_counts(slot, cosmetic)[0], 0);
}

TEST_CASE(
    "[Networked][Predict][Ledger] the recovery ledger is seeded for "
    "every causal field and for nothing else, and an unbound set seeds none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    Node *host = memnew(Node);
    Ref<netw::NetwPropertySet> declared;
    declared.instantiate();
    const Ref<netw::NetwPropertySetBinding> bound
        = netw::NetwPropertySetBinding::create(declared, host);
    REQUIRE(bound.is_valid());

    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 0);
    pool->seed_recovery_ledger(slot);
    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 0);

    pool->bind_property_sets(slot, bound, bound);
    pool->seed_recovery_ledger(slot);

    const godot::Array seeded = pool->ledger_fields(slot);
    NETW_CHECK_EQ(seeded.size(), 4);
    CHECK(seeded.has(StringName("quantized")));
    CHECK(seeded.has(StringName("plain")));
    CHECK(seeded.has(StringName("latched")));
    CHECK(seeded.has(StringName("reconciled")));
    CHECK_FALSE(seeded.has(StringName("cosmetic")));

    pool->seed_recovery_ledger(slot + 9000);

    memdelete(host);
}

TEST_CASE(
    "[Networked][Predict][Pose] authoring one command moves BOTH "
    "input cursors, which are two fields and not a shadow"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    pool->author_input(slot, 7);

    NETW_CHECK_EQ(pool->latest_input_tick_of(slot), 7);
    NETW_CHECK_EQ(
        pool->drive_cursors(
            slot
        )[NetwPredictionEngine::CURSOR_LATEST_INPUT_TICK],
        7
    );

    CHECK(pool->author_input(slot + 9000, 9).is_empty());
    NETW_CHECK_EQ(pool->latest_input_tick_of(slot), 7);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] a recovery is planned against the "
    "basis it was asked for, and a closed slot plans none"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const netw::predict::WritePlan planned = pool->recover_for(
        slot,
        4,
        3,
        row_of(4.5, 0.25),
        row_of(9.5, 0.25),
        row_of(4.5, 0.25),
        godot::Dictionary(),
        int(netw::NetwPredict::RECOVERY_POLICY_REBASE_REPLAY),
        0.01,
        2.0,
        6,
        0,
        6,
        int(netw::predict::Domain::IN_DOMAIN),
        0,
        false
    );
    CHECK_FALSE(planned.skip);
    NETW_CHECK_EQ(planned.basis, 4);

    const netw::predict::WritePlan closed = pool->recover_for(
        slot + 9000,
        4,
        3,
        row_of(4.5, 0.25),
        row_of(9.5, 0.25),
        row_of(4.5, 0.25),
        godot::Dictionary(),
        int(netw::NetwPredict::RECOVERY_POLICY_REBASE_REPLAY),
        0.01,
        2.0,
        6,
        0,
        6,
        int(netw::predict::Domain::IN_DOMAIN),
        0,
        false
    );
    CHECK(closed.skip);
    NETW_CHECK_EQ(closed.basis, -1);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] a comparison is judged only where the "
    "pool holds the row it would be judged against"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);
    const godot::PackedFloat64Array meter
        = pool->meter_tolerances(slot, 0, 0.01);

    CHECK(
        pool->compare_state_for(
                slot,
                3,
                row_of(4.5, 0.25),
                row_of(6.5, 0.25),
                meter,
                0.01
        )
            .transition
        < 0
    );

    pool->record_input(slot, 3, 3);
    REQUIRE(pool->replay_drive(
                    slot,
                    godot::Dictionary(),
                    3,
                    3,
                    int(netw::DriveKind::FRESH),
                    3,
                    3,
                    1.0 / 60.0,
                    1,
                    0,
                    0,
                    0,
                    0
    )
                .ran);

    const netw::predict::StateVerdict unarmed = pool->compare_state_for(
        slot,
        3,
        row_of(4.5, 0.25),
        row_of(6.5, 0.25),
        meter,
        0.01
    );
    CHECK_FALSE(unarmed.corrected);

    pool->set_stream_reconstructed(slot, true);
    const netw::predict::StateVerdict judged = pool->compare_state_for(
        slot,
        3,
        row_of(4.5, 0.25),
        row_of(6.5, 0.25),
        meter,
        0.01
    );
    CHECK(judged.corrected);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] a replayed entry records the state it "
    "produced at the entry AFTER it, never at its own"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    pool->record_input(slot, 3, 3);
    const netw::predict::DriveRecord drive = pool->replay_drive(
        slot,
        godot::Dictionary(),
        3,
        3,
        int(netw::DriveKind::FRESH),
        3,
        3,
        1.0 / 60.0,
        1,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);

    const Ref<netw::NetwTimeline> entries = pool->entry_history(slot);
    REQUIRE(entries.is_valid());
    CHECK(entries->state_at(4).is_empty());

    pool->close_replayed_entry(
        slot,
        3,
        godot::Dictionary(),
        godot::PackedStringArray()
    );

    CHECK_FALSE(entries->state_at(4).is_empty());
    CHECK(entries->state_at(3).is_empty());

    CHECK(pool->close_replayed_entry(
                  slot + 9000,
                  3,
                  godot::Dictionary(),
                  godot::PackedStringArray()
    )
              .is_empty());

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] a write is measured against what the "
    "body held before it, the staged value overrides the readback, and a "
    "move too small to matter is not a move"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    const godot::Dictionary before = pool->capture_state(slot);
    CHECK(pool->write_deltas(slot, before, godot::Dictionary()).is_empty());

    godot::Dictionary staged;
    staged[StringName("speed")] = 9.5;
    const godot::Dictionary moved = pool->write_deltas(slot, before, staged);
    CHECK(moved.has(StringName("speed")));
    NETW_CHECK_CLOSE(double(moved[StringName("speed")]), 5.0, 1e-9);

    godot::Dictionary hair;
    hair[StringName("speed")] = 4.5 + 1e-9;
    CHECK(pool->write_deltas(slot, before, hair).is_empty());

    godot::Dictionary unheld;
    unheld[StringName("speed")] = 9.5;
    godot::Dictionary partial;
    partial[StringName("throttle")] = 0.25;
    CHECK(pool->write_deltas(slot, partial, unheld).is_empty());

    CHECK(pool->write_deltas(slot + 9000, before, staged).is_empty());

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] a simulated remote is compared "
    "against where the authority row would be NOW, and only where the "
    "declaration asked to extrapolate"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    godot::Dictionary payload;
    payload[StringName("speed")] = 4.5;
    payload[StringName("throttle")] = 0.25;
    payload[StringName("latch")] = 7.0;

    const godot::Dictionary held
        = pool->open_simulated_state(slot, payload, 10, 40);
    const godot::Dictionary target = held[StringName("target")];
    NETW_CHECK_CLOSE(double(target[StringName("speed")]), 4.5, 1e-9);
    NETW_CHECK_CLOSE(double(held[StringName("divergence")]), 0.0, 1e-9);
    CHECK(pool->reconciling(slot));

    pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXTRAPOLATED)
    );
    const godot::Dictionary aged
        = pool->open_simulated_state(slot, payload, 10, 40);
    const godot::Dictionary moved = aged[StringName("target")];
    const bool target_moved = double(moved[StringName("speed")]) != 4.5;
    CHECK(target_moved);
    const bool divergence_seen = double(aged[StringName("divergence")]) > 0.0;
    CHECK(divergence_seen);

    const godot::Dictionary same_tick
        = pool->open_simulated_state(slot, payload, 10, 10);
    NETW_CHECK_CLOSE(
        double(godot::Dictionary(
            same_tick[StringName("target")]
        )[StringName("speed")]),
        4.5,
        1e-9
    );

    const godot::Dictionary absent
        = pool->open_simulated_state(slot + 9000, payload, 10, 40);
    NETW_CHECK_CLOSE(double(absent[StringName("divergence")]), 0.0, 1e-9);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Pose] the tier error is measured against "
    "the EXTRAPOLATED target, so an older acknowledgement moves the target "
    "and a slot with nothing in the tier measures nothing at all"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);
    REQUIRE(pool->has_pose_fields(slot));

    godot::Dictionary payload;
    payload[StringName("speed")] = 6.5;
    payload[StringName("throttle")] = 0.25;
    payload[StringName("latch")] = 7.0;

    const godot::Dictionary held = pool->pose_errors_against(slot, payload, 0);
    CHECK(held.has(StringName("speed")));
    CHECK_FALSE(held.has(StringName("latch")));
    NETW_CHECK_CLOSE(double(held[StringName("speed")]), 2.0, 1e-9);

    const godot::Dictionary aged = pool->pose_errors_against(slot, payload, 30);
    const bool target_moved = double(aged[StringName("speed")])
        != double(held[StringName("speed")]);
    CHECK(target_moved);

    const godot::Dictionary clamped
        = pool->pose_errors_against(slot, payload, 300);
    NETW_CHECK_CLOSE(
        double(clamped[StringName("speed")]),
        double(aged[StringName("speed")]),
        1e-9
    );

    CHECK(pool->pose_errors_against(slot + 9000, payload, 0).is_empty());

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Lint] a declaration that is legal and then "
    "silently inert is named, and a healthy one is not"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    const godot::Dictionary report = lint_of(pool, slot);

    const bool named = String(report[StringName("entity")]) == String("racer");
    CHECK(named);
    CHECK(has_finding(report, "unquantized"));
    const godot::PackedStringArray bare = finding_fields(report, "unquantized");
    CHECK(bare.has(String("plain")));
    CHECK_FALSE(bare.has(String("quantized")));
    CHECK(has_finding(report, "unobserved"));
    CHECK_FALSE(has_finding(report, "inert_forward_model"));
    CHECK_FALSE(has_finding(report, "transport_without_epsilon"));
}

TEST_CASE(
    "[Networked][Predict][Lint] a carry step the tick tier refuses is named "
    "inert, and the report says which schedule would run it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);
    const StringName plain("plain");
    pool->set_carry(slot, plain, callable_mp_static(&stamp_carry_rule));

    const godot::Dictionary report = lint_of(pool, slot);

    CHECK(has_finding(report, "inert_forward_model"));
    const godot::PackedStringArray named
        = finding_fields(report, "inert_forward_model");
    CHECK(named.has(String("plain")));

    const godot::Dictionary fields = report[StringName("fields")];
    const godot::Dictionary entry = fields[plain];
    const godot::Dictionary model = entry[StringName("forward_model")];
    const bool declares_step
        = String(model[StringName("kind")]) == String("step");
    CHECK(declares_step);
    CHECK_FALSE(bool(model[StringName("live")]));
    const bool says_frame
        = String(model[StringName("why")])
              .contains(String("prediction.schedule = FRAME"));
    CHECK(says_frame);
}

TEST_CASE(
    "[Networked][Predict][Lint] a tolerance on a field nothing "
    "compares decides nothing, and the report says so"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    CHECK_FALSE(has_finding(lint_of(pool, slot), "uncompared"));

    godot::LocalVector<netw::predict::FieldDecl> decl;
    declare_full(
        decl,
        "cosmetic",
        int(netw::predict::PropertyClass::COSMETIC),
        false,
        false,
        0.25,
        false
    );
    const int64_t marked = pool->open(decl);
    pool->configure(
        marked,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );

    CHECK(has_finding(lint_of(pool, marked), "uncompared"));
}

TEST_CASE(
    "[Networked][Predict][Lint] the per-field row names the class, "
    "what triggers, and whether a forward model is LIVE"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = tolerance_slot(pool);

    const godot::Dictionary fields = lint_of(pool, slot)[StringName("fields")];
    const godot::Dictionary quantized = fields[StringName("quantized")];
    const bool causal_class
        = String(quantized[StringName("class")]) == String("CAUSAL");
    CHECK(causal_class);
    CHECK(bool(quantized[StringName("triggers")]));

    const godot::Dictionary model = quantized[StringName("forward_model")];
    const bool no_model = String(model[StringName("kind")]) == String("none");
    CHECK(no_model);
    CHECK_FALSE(bool(model[StringName("live")]));

    const godot::Dictionary reconciled = fields[StringName("reconciled")];
    CHECK_FALSE(bool(reconciled[StringName("triggers")]));

    const godot::Dictionary cosmetic = fields[StringName("cosmetic")];
    const bool cosmetic_class
        = String(cosmetic[StringName("class")]) == String("COSMETIC");
    CHECK(cosmetic_class);
}

TEST_CASE(
    "[Networked][Predict][Lint] a slot that was never opened lints "
    "nothing rather than inventing a clean report"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;

    const godot::Dictionary report = lint_of(pool, -1);
    const godot::Dictionary fields = report[StringName("fields")];
    const Array findings = report[StringName("findings")];

    NETW_CHECK_EQ(fields.size(), 0);
    NETW_CHECK_EQ(findings.size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Tolerance] a slot that was never opened has "
    "no column at all"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;

    NETW_CHECK_EQ(pool->correction_tolerances(-1, 0.5).size(), 0);
    NETW_CHECK_EQ(
        pool->meter_tolerances(-1, int(netw::predict::Domain::IN_DOMAIN), 0.5)
            .size(),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Transport] the projection reads the slot's "
    "own pose enrolment and moves the current value by authority's delta"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    godot::Dictionary predicted;
    predicted[StringName("speed")] = 4.0;
    predicted[StringName("throttle")] = 0.25;
    predicted[StringName("latch")] = 7.0;
    godot::Dictionary authority = predicted.duplicate();
    authority[StringName("speed")] = 6.0;
    authority[StringName("throttle")] = 0.75;
    godot::Dictionary current = predicted.duplicate();
    current[StringName("speed")] = 10.0;

    const godot::Dictionary out
        = pool->transport_of(slot, predicted, authority, current);
    CHECK(bool(out[StringName("valid")]));
    const godot::Dictionary restore = out[StringName("restore")];
    NETW_CHECK_EQ(restore.size(), 1);
    CHECK(restore.has(StringName("speed")));
    CHECK(double(restore[StringName("speed")]) == doctest::Approx(12.0));
    const godot::Dictionary delta = out[StringName("delta")];
    CHECK(double(delta[StringName("speed")]) == doctest::Approx(2.0));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Transport] a pose field absent from any of "
    "the three payloads refuses the whole projection"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    Carrier *body = carrier(4.5, 0.25);
    const int64_t slot = stamped_slot(pool, body);

    godot::LocalVector<netw::predict::FieldDecl> paired;
    declare(paired, "speed", "throttle", false);
    declare(paired, "throttle", "", false);
    declare(paired, "heading", "spin", false);
    declare(paired, "spin", "", false);
    pool->rewire(slot, paired);

    godot::Dictionary predicted;
    predicted[StringName("speed")] = 4.0;
    predicted[StringName("heading")] = 1.0;
    godot::Dictionary authority = predicted.duplicate();
    authority[StringName("speed")] = 6.0;
    authority[StringName("heading")] = 2.0;
    godot::Dictionary current = predicted.duplicate();
    current.erase(StringName("heading"));

    const godot::Dictionary out
        = pool->transport_of(slot, predicted, authority, current);
    CHECK_FALSE(bool(out[StringName("valid")]));
    NETW_CHECK_EQ(godot::Dictionary(out[StringName("restore")]).size(), 0);
    NETW_CHECK_EQ(godot::Dictionary(out[StringName("delta")]).size(), 0);

    const godot::Dictionary closed
        = pool->transport_of(slot + 9000, predicted, authority, current);
    CHECK_FALSE(bool(closed[StringName("valid")]));

    memdelete(body);
}

TEST_CASE(
    "[Networked][Predict][Transport] the angle declaration answers by "
    "field name, and a name the slot never declared is not one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    godot::LocalVector<netw::predict::FieldDecl> declaration;
    declaration.push_back(
        netw::field_decl(
            StringName("heading"),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            -1.0,
            -1.0,
            true
        )
    );
    declare(declaration, "speed", "", false);
    const int64_t slot = pool->open(declaration);

    CHECK(pool->is_angle_field(slot, StringName("heading")));
    CHECK_FALSE(pool->is_angle_field(slot, StringName("speed")));
    CHECK_FALSE(pool->is_angle_field(slot, StringName("nothing")));
    CHECK_FALSE(pool->is_angle_field(slot + 9000, StringName("heading")));
}

TEST_CASE(
    "[Networked][Predict][Transport] a declared tier distance enrols "
    "its field in the pose tier and answers the measurement by name"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    godot::LocalVector<netw::predict::FieldDecl> declaration;
    declaration.push_back(
        netw::field_decl(
            StringName("position"),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            -1.0,
            3.0,
            false
        )
    );
    declare(declaration, "speed", "", false);
    const int64_t slot = pool->open(declaration);

    CHECK(pool->has_pose_fields(slot));
    const godot::Dictionary distances = pool->teleport_distances(slot);
    NETW_CHECK_EQ(distances.size(), 1);
    CHECK(double(distances[StringName("position")]) == doctest::Approx(3.0));

    godot::Dictionary errors;
    errors[StringName("position")] = 2.9;
    CHECK_FALSE(pool->teleport_reached_of(slot, errors, 0.5));
    errors[StringName("position")] = 3.0;
    CHECK(pool->teleport_reached_of(slot, errors, 99.0));
}

TEST_CASE(
    "[Networked][Predict][Transport] a field with no tier distance of "
    "its own is measured against the entity-wide one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    NETW_CHECK_EQ(pool->teleport_distances(slot).size(), 0);

    godot::Dictionary errors;
    errors[StringName("speed")] = 1.0;
    CHECK_FALSE(pool->teleport_reached_of(slot, errors, 2.0));
    CHECK(pool->teleport_reached_of(slot, errors, 0.5));
    CHECK(pool->teleport_reached_of(slot + 9000, errors, 0.5));
    NETW_CHECK_EQ(pool->teleport_distances(slot + 9000).size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Wiring] the property class answers by name, "
    "and reconcile_only excludes a field the class still calls causal"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    godot::LocalVector<netw::predict::FieldDecl> declaration;
    declare(declaration, "speed", "", false);
    declaration.push_back(
        netw::field_decl(
            StringName("skid"),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            true,
            -1.0,
            -1.0,
            false
        )
    );
    declaration.push_back(
        netw::field_decl(
            StringName("paint"),
            int(netw::predict::PropertyClass::COSMETIC)
        )
    );
    const int64_t slot = pool->open(declaration);

    CHECK(pool->is_causal_field(slot, StringName("speed")));
    CHECK(pool->is_causal_field(slot, StringName("skid")));
    CHECK_FALSE(pool->is_causal_field(slot, StringName("paint")));
    CHECK_FALSE(pool->is_causal_field(slot, StringName("nothing")));

    CHECK_FALSE(pool->is_trigger_excluded_field(slot, StringName("speed")));
    CHECK(pool->is_trigger_excluded_field(slot, StringName("skid")));
    CHECK_FALSE(pool->is_trigger_excluded_field(slot, StringName("paint")));

    CHECK_FALSE(pool->is_causal_field(slot + 9000, StringName("speed")));
    CHECK_FALSE(
        pool->is_trigger_excluded_field(slot + 9000, StringName("skid"))
    );
}

TEST_CASE(
    "[Networked][Predict][Wiring] a field's own tolerance outranks the "
    "entity-wide one, and only a declared override is its own"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    godot::LocalVector<netw::predict::FieldDecl> declaration;
    declaration.push_back(
        netw::field_decl(
            StringName("position"),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            0.125,
            -1.0,
            false
        )
    );
    declare(declaration, "speed", "", false);
    const int64_t slot = pool->open(declaration);

    CHECK(
        pool->epsilon_for_field(slot, StringName("position"), 9.0)
        == doctest::Approx(0.125)
    );
    CHECK(
        pool->epsilon_for_field(slot, StringName("speed"), 9.0)
        == doctest::Approx(9.0)
    );
    CHECK(
        pool->epsilon_for_field(slot, StringName("nothing"), 9.0)
        == doctest::Approx(9.0)
    );
    CHECK(
        pool->epsilon_for_field(slot + 9000, StringName("position"), 9.0)
        == doctest::Approx(9.0)
    );
}

TEST_CASE(
    "[Networked][Predict][Wiring] the compared state keeps the causal "
    "fields alone, and a slot declaring none compares the whole payload"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    godot::LocalVector<netw::predict::FieldDecl> declaration;
    declare(declaration, "speed", "", false);
    declaration.push_back(
        netw::field_decl(
            StringName("paint"),
            int(netw::predict::PropertyClass::COSMETIC)
        )
    );
    const int64_t slot = pool->open(declaration);

    godot::Dictionary payload;
    payload[StringName("speed")] = 4.0;
    payload[StringName("paint")] = 2.0;
    payload[StringName("unknown")] = 1.0;

    const godot::Dictionary compared = pool->compared_state_of(slot, payload);
    NETW_CHECK_EQ(compared.size(), 1);
    CHECK(compared.has(StringName("speed")));

    godot::LocalVector<netw::predict::FieldDecl> cosmetic_only;
    cosmetic_only.push_back(
        netw::field_decl(
            StringName("paint"),
            int(netw::predict::PropertyClass::COSMETIC)
        )
    );
    const int64_t bare = pool->open(cosmetic_only);
    NETW_CHECK_EQ(pool->compared_state_of(bare, payload).size(), 3);
    NETW_CHECK_EQ(pool->compared_state_of(bare + 9000, payload).size(), 3);
}

TEST_CASE(
    "[Networked][Predict][Wiring] the projection map names the channel "
    "each field restores along, and a field with none is absent"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    const godot::Dictionary map = pool->projection_map(slot);
    NETW_CHECK_EQ(map.size(), 1);
    CHECK(StringName(map[StringName("speed")]) == StringName("throttle"));
    CHECK_FALSE(map.has(StringName("throttle")));
    CHECK_FALSE(map.has(StringName("latch")));
    NETW_CHECK_EQ(pool->projection_map(slot + 9000).size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Wiring] the seam snapshot carries the six "
    "declared tables and the three entity-wide defaults the slot was "
    "configured with"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    godot::LocalVector<netw::predict::FieldDecl> declaration;
    declaration.push_back(
        netw::field_decl(
            StringName("heading"),
            int(netw::predict::PropertyClass::CAUSAL),
            StringName(),
            0.5,
            true,
            false,
            0.125,
            3.0,
            true
        )
    );
    declaration.push_back(
        netw::field_decl(
            StringName("paint"),
            int(netw::predict::PropertyClass::COSMETIC)
        )
    );
    const int64_t slot = pool->open(declaration);
    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE,
        false,
        false,
        false,
        0.01,
        2.0,
        -4
    ));

    const godot::Dictionary out = pool->wiring_snapshot(slot);
    const StringName heading("heading");

    CHECK(
        double(godot::Dictionary(out[StringName("epsilon_overrides")])[heading])
        == doctest::Approx(0.125)
    );
    CHECK(godot::Dictionary(out[StringName("angle_fields")]).has(heading));
    CHECK(godot::Dictionary(out[StringName("withheld")]).has(heading));
    CHECK(godot::Dictionary(out[StringName("converge_rules")]).has(heading));
    CHECK(
        godot::Dictionary(out[StringName("teleport_thresholds")]).has(heading)
    );
    CHECK(
        godot::Dictionary(out[StringName("vote_excludes")])
            .has(StringName("paint"))
    );
    CHECK_FALSE(
        godot::Dictionary(out[StringName("vote_excludes")]).has(heading)
    );

    CHECK(double(out[StringName("epsilon")]) == doctest::Approx(0.01));
    CHECK(
        double(out[StringName("teleport_threshold")]) == doctest::Approx(2.0)
    );
    NETW_CHECK_EQ(int(out[StringName("max_restore_ticks")]), 6);

    CHECK(pool->epsilon_of_slot(slot) == doctest::Approx(0.01));
    CHECK(pool->teleport_threshold_of(slot) == doctest::Approx(2.0));
    NETW_CHECK_EQ(pool->collision_cooldown_of(slot), 0);

    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        NetwPredictionEngine::ISLAND_NONE,
        false,
        false,
        false,
        -0.5,
        -1.0,
        5
    ));
    CHECK(pool->epsilon_of_slot(slot) == doctest::Approx(0.0));
    CHECK(pool->teleport_threshold_of(slot) == doctest::Approx(0.0));
    NETW_CHECK_EQ(pool->collision_cooldown_of(slot), 5);

    const godot::Dictionary closed = pool->wiring_snapshot(slot + 9000);
    NETW_CHECK_EQ(
        godot::Dictionary(closed[StringName("angle_fields")]).size(),
        0
    );
    CHECK(double(closed[StringName("epsilon")]) == doctest::Approx(0.0));
    CHECK(pool->epsilon_of_slot(slot + 9000) == doctest::Approx(0.0));
    NETW_CHECK_EQ(pool->collision_cooldown_of(slot + 9000), 0);
}

TEST_CASE(
    "[Networked][Predict][Timing] one pass's reading carries every "
    "value it was built with, and defaults a frame it was not given one for"
) {
    const netw::NetwPredictTiming full
        = netw::NetwPredictTiming::of(7, 0.25, 1.0 / 60.0, 41, 2, false);
    NETW_CHECK_EQ(full.get_tick(), 7);
    CHECK(full.get_delta() == doctest::Approx(0.25));
    CHECK(full.get_ticktime() == doctest::Approx(1.0 / 60.0));
    NETW_CHECK_EQ(full.get_frame(), 41);
    NETW_CHECK_EQ(full.get_quantum(), 2);
    CHECK_FALSE(full.get_simulating());

    const netw::NetwPredictTiming bare
        = netw::NetwPredictTiming::of(0, 0.0, 0.0);
    NETW_CHECK_EQ(bare.get_frame(), 0);
    NETW_CHECK_EQ(bare.get_quantum(), 1);
    CHECK(bare.get_simulating());
}

TEST_CASE(
    "[Networked][Predict][Vocabulary] the joint floor is the oldest "
    "basis any member still needs, and one it cannot replay across heals"
) {
    godot::Dictionary bases;
    bases[StringName("a")] = 40;
    bases[StringName("b")] = 34;
    bases[StringName("c")] = -1;
    godot::Dictionary relays;
    relays[StringName("a")] = 37;

    const godot::Dictionary settled
        = netw::NetwPredict::joint_floor(bases, relays, 39, 30, 50);
    NETW_CHECK_EQ(int64_t(settled[StringName("floor")]), 34);
    CHECK_FALSE(bool(settled[StringName("heal")]));

    const godot::Dictionary healed
        = netw::NetwPredict::joint_floor(bases, relays, 39, 36, 50);
    NETW_CHECK_EQ(int64_t(healed[StringName("floor")]), 50);
    CHECK(bool(healed[StringName("heal")]));

    const godot::Dictionary epoch_bound = netw::NetwPredict::joint_floor(
        godot::Dictionary(),
        relays,
        12,
        0,
        50
    );
    NETW_CHECK_EQ(int64_t(epoch_bound[StringName("floor")]), 12);

    const godot::Dictionary nothing = netw::NetwPredict::joint_floor(
        godot::Dictionary(),
        godot::Dictionary(),
        -1,
        0,
        50
    );
    NETW_CHECK_EQ(int64_t(nothing[StringName("floor")]), 50);
    CHECK_FALSE(bool(nothing[StringName("heal")]));
}

TEST_CASE(
    "[Networked][Predict][Vocabulary] a matrix cell is ranked by the "
    "strongest provenance that is true of it"
) {
    NETW_CHECK_EQ(
        netw::NetwPredict::joint_cell(true, true, true),
        int(netw::NetwPredict::CELL_PROVENANCE_AUTHORED)
    );
    NETW_CHECK_EQ(
        netw::NetwPredict::joint_cell(false, true, true),
        int(netw::NetwPredict::CELL_PROVENANCE_RELAYED)
    );
    NETW_CHECK_EQ(
        netw::NetwPredict::joint_cell(false, false, true),
        int(netw::NetwPredict::CELL_PROVENANCE_SUBSTITUTED)
    );
    NETW_CHECK_EQ(
        netw::NetwPredict::joint_cell(false, false, false),
        int(netw::NetwPredict::CELL_PROVENANCE_COAST)
    );
}

TEST_CASE(
    "[Networked][Predict][Vocabulary] a name a member does not have "
    "reads back as its value rather than as another member's name"
) {
    CHECK(
        netw::NetwPredict::schedule_name(
            static_cast<netw::NetwPredict::Schedule>(1)
        )
        == godot::String("FRAME")
    );
    CHECK(
        netw::NetwPredict::drive_kind_name(
            static_cast<netw::NetwPredict::DriveKind>(7)
        )
        == godot::String("SUBSTITUTED")
    );
    CHECK(
        netw::NetwPredict::verdict_reason_name(
            static_cast<netw::NetwPredict::VerdictReason>(10)
        )
        == godot::String("DECLINED")
    );
    CHECK(
        netw::NetwPredict::episode_state_name(
            static_cast<netw::NetwPredict::EpisodeState>(2)
        )
        == godot::String("FALLBACK")
    );
    CHECK(
        netw::NetwPredict::operator_outcome_name(4) == godot::String("WITHHELD")
    );

    CHECK(
        netw::NetwPredict::schedule_name(
            static_cast<netw::NetwPredict::Schedule>(99)
        )
        == godot::String("99")
    );
    CHECK(
        netw::NetwPredict::drive_kind_name(
            static_cast<netw::NetwPredict::DriveKind>(-1)
        )
        == godot::String("-1")
    );
}

TEST_CASE(
    "[Networked][Predict][Report] a comparison's own report is the "
    "slot's, and a closed slot reports the absence rather than a value"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    NETW_CHECK_EQ(pool->verdict_reason_of(slot), 0);
    NETW_CHECK_EQ(pool->attribution_of(slot), 0);
    NETW_CHECK_EQ(pool->attributed_transition_of(slot), -1);
    NETW_CHECK_EQ(pool->compare_staleness_of(slot), -1);
    CHECK_FALSE(pool->reconciling(slot));

    pool->note_verdict_reason(slot, 9);
    pool->note_attribution(slot, 4, 77);
    pool->note_compare_staleness(slot, 3);
    pool->note_reconciling(slot, true);

    NETW_CHECK_EQ(pool->verdict_reason_of(slot), 9);
    NETW_CHECK_EQ(pool->attribution_of(slot), 4);
    NETW_CHECK_EQ(pool->attributed_transition_of(slot), 77);
    NETW_CHECK_EQ(pool->compare_staleness_of(slot), 3);
    CHECK(pool->reconciling(slot));

    NETW_CHECK_EQ(pool->verdict_reason_of(slot + 9000), 0);
    NETW_CHECK_EQ(pool->attributed_transition_of(slot + 9000), -1);
    NETW_CHECK_EQ(pool->compare_staleness_of(slot + 9000), -1);
    CHECK_FALSE(pool->reconciling(slot + 9000));

    pool->note_verdict_reason(slot + 9000, 9);
    pool->note_attribution(slot + 9000, 4, 77);
    NETW_CHECK_EQ(pool->verdict_reason_of(slot + 9000), 0);
}

TEST_CASE(
    "[Networked][Predict][Report] the attribution and the transition "
    "it describes move together, so neither can describe the other's row"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    pool->note_attribution(slot, 6, 12);
    NETW_CHECK_EQ(pool->attribution_of(slot), 6);
    NETW_CHECK_EQ(pool->attributed_transition_of(slot), 12);

    pool->note_attribution(slot, 0, -1);
    NETW_CHECK_EQ(pool->attribution_of(slot), 0);
    NETW_CHECK_EQ(pool->attributed_transition_of(slot), -1);
}

TEST_CASE(
    "[Networked][Predict][Report] the per-field readings are keyed by "
    "the slot's own field table, and a name it never declared is dropped"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const StringName speed("speed");
    const StringName latch("latch");

    NETW_CHECK_EQ(pool->divergence_report(slot).size(), 0);
    CHECK_FALSE(pool->has_divergence(slot, speed));
    CHECK(pool->divergence_of(slot, speed, 7.5) == doctest::Approx(7.5));

    godot::Dictionary readings;
    readings[speed] = 0.5;
    readings[StringName("nothing")] = 9.0;
    pool->note_divergence(slot, readings);

    const godot::Dictionary out = pool->divergence_report(slot);
    NETW_CHECK_EQ(out.size(), 1);
    CHECK(double(out[speed]) == doctest::Approx(0.5));
    CHECK(pool->has_divergence(slot, speed));
    CHECK(pool->divergence_of(slot, speed, 7.5) == doctest::Approx(0.5));
    CHECK_FALSE(pool->has_divergence(slot, StringName("nothing")));
    CHECK_FALSE(pool->has_divergence(slot, latch));

    pool->clear_divergence(slot);
    NETW_CHECK_EQ(pool->divergence_report(slot).size(), 0);
    CHECK_FALSE(pool->has_divergence(slot, speed));
}

TEST_CASE(
    "[Networked][Predict][Report] a reading of zero is a reading, and "
    "the absent answer is the caller's fallback rather than zero"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const StringName speed("speed");

    godot::Dictionary readings;
    readings[speed] = 0.0;
    pool->note_divergence(slot, readings);

    CHECK(pool->has_divergence(slot, speed));
    CHECK(pool->divergence_of(slot, speed, 7.5) == doctest::Approx(0.0));
    NETW_CHECK_EQ(pool->divergence_report(slot).size(), 1);

    CHECK(
        pool->divergence_of(slot, StringName("latch"), 7.5)
        == doctest::Approx(7.5)
    );
}

TEST_CASE(
    "[Networked][Predict][Report] the tier error and the divergence "
    "are two readings, and a rewire empties both"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const StringName speed("speed");

    godot::Dictionary diverged;
    diverged[speed] = 0.5;
    godot::Dictionary tiered;
    tiered[speed] = 4.0;
    pool->note_divergence(slot, diverged);
    pool->note_tier_errors(slot, tiered);

    CHECK(double(pool->divergence_report(slot)[speed]) == doctest::Approx(0.5));
    CHECK(double(pool->tier_error_report(slot)[speed]) == doctest::Approx(4.0));

    pool->clear_tier_errors(slot);
    NETW_CHECK_EQ(pool->tier_error_report(slot).size(), 0);
    NETW_CHECK_EQ(pool->divergence_report(slot).size(), 1);

    pool->rewire(slot, every_family_populated());
    NETW_CHECK_EQ(pool->divergence_report(slot).size(), 0);
    NETW_CHECK_EQ(pool->tier_error_report(slot).size(), 0);

    NETW_CHECK_EQ(pool->divergence_report(slot + 9000).size(), 0);
    CHECK_FALSE(pool->has_divergence(slot + 9000, speed));
}

TEST_CASE(
    "[Networked][Predict][Report] the promoting subjects are kept in "
    "instance order, so the predictor a member runs holds still"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const Callable first = callable_mp_static(&declared_rule);
    const Callable second = callable_mp_static(&second_rule);

    NETW_CHECK_EQ(pool->simulation_subject_count(slot), 0);
    CHECK_FALSE(pool->first_simulation_predictor(slot).is_valid());

    CHECK(pool->note_simulated_by(slot, 90, second));
    CHECK(pool->note_simulated_by(slot, 10, first));
    NETW_CHECK_EQ(pool->simulation_subject_count(slot), 2);
    CHECK(pool->first_simulation_predictor(slot) == first);

    CHECK_FALSE(pool->note_simulated_by(slot, 10, first));
    CHECK(pool->note_simulated_by(slot, 10, second));
    CHECK(pool->first_simulation_predictor(slot) == second);

    CHECK(pool->clear_simulated_by(slot, 10));
    CHECK_FALSE(pool->clear_simulated_by(slot, 10));
    NETW_CHECK_EQ(pool->simulation_subject_count(slot), 1);
    CHECK(pool->first_simulation_predictor(slot) == second);

    pool->clear_simulation_subjects(slot);
    NETW_CHECK_EQ(pool->simulation_subject_count(slot), 0);

    CHECK_FALSE(pool->note_simulated_by(slot + 9000, 1, first));
    NETW_CHECK_EQ(pool->simulation_subject_count(slot + 9000), 0);
}

TEST_CASE(
    "[Networked][Predict][Report] a subject that declared no predictor "
    "is still a promoter, and is skipped when one is asked for"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const Callable declared = callable_mp_static(&declared_rule);

    CHECK(pool->note_simulated_by(slot, 5, Callable()));
    NETW_CHECK_EQ(pool->simulation_subject_count(slot), 1);
    CHECK_FALSE(pool->first_simulation_predictor(slot).is_valid());

    CHECK(pool->note_simulated_by(slot, 9, declared));
    NETW_CHECK_EQ(pool->simulation_subject_count(slot), 2);
    CHECK(pool->first_simulation_predictor(slot) == declared);
}

TEST_CASE(
    "[Networked][Predict][Report] the breach source answers 'default' "
    "until something names itself, and a rewire does not un-name it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK(pool->breach_source_of(slot) == StringName("default"));
    CHECK(pool->breach_source_of(slot + 9000) == StringName("default"));

    pool->note_breach_source(slot, StringName("code"));
    CHECK(pool->breach_source_of(slot) == StringName("code"));

    pool->rewire(slot, every_family_populated());
    CHECK(pool->breach_source_of(slot) == StringName("code"));
}

TEST_CASE(
    "[Networked][Predict][Ledger] a field enters the ledger when "
    "something charges it, and a row of zeros is still a row"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const StringName speed("speed");
    const StringName latch("latch");

    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 0);

    pool->ledger_seed(slot, speed);
    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 1);
    NETW_CHECK_EQ(pool->ledger_counts(slot, speed)[0], 0);

    pool->ledger_bump_triggered(slot, latch);
    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 2);
    NETW_CHECK_EQ(pool->ledger_counts(slot, latch)[0], 1);

    pool->ledger_seed(slot, StringName("nothing"));
    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 2);
    NETW_CHECK_EQ(pool->ledger_counts(slot, StringName("nothing")).size(), 6);
    NETW_CHECK_EQ(pool->ledger_counts(slot, StringName("nothing"))[0], 0);
}

TEST_CASE(
    "[Networked][Predict][Ledger] the three charged counters and the "
    "three the carry track answers read as one row"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const StringName speed("speed");

    pool->ledger_bump_triggered(slot, speed);
    pool->ledger_bump_triggered(slot, speed);
    pool->ledger_bump_repaired(slot, speed);
    pool->ledger_bump_contracted(slot, speed);

    const godot::PackedInt64Array counts = pool->ledger_counts(slot, speed);
    NETW_CHECK_EQ(counts.size(), 6);
    NETW_CHECK_EQ(counts[0], 2);
    NETW_CHECK_EQ(counts[1], 1);
    NETW_CHECK_EQ(counts[2], 1);
    NETW_CHECK_EQ(counts[3], 0);
    NETW_CHECK_EQ(counts[4], 0);
    NETW_CHECK_EQ(counts[5], 0);

    const godot::Dictionary rows = pool->field_recovery(slot);
    NETW_CHECK_EQ(rows.size(), 1);
    const Ref<netw::NetwPredictFieldRecovery> row = rows[speed];
    REQUIRE(row.is_valid());
    NETW_CHECK_EQ(row->get_triggered(), 2);
    NETW_CHECK_EQ(row->get_repaired(), 1);
    NETW_CHECK_EQ(row->get_contracted(), 1);
    NETW_CHECK_EQ(row->get_carried(), 0);
    CHECK(row->get_field() == speed);

    pool->ledger_bump_triggered(slot, speed);
    NETW_CHECK_EQ(row->get_triggered(), 3);
}

TEST_CASE(
    "[Networked][Predict][Ledger] a rewire keeps the counts a field "
    "already accumulated, because they are the entity's history"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    const StringName speed("speed");

    pool->ledger_bump_repaired(slot, speed);
    pool->ledger_bump_repaired(slot, speed);
    NETW_CHECK_EQ(pool->ledger_counts(slot, speed)[1], 2);

    pool->rewire(slot, every_family_populated());
    NETW_CHECK_EQ(pool->ledger_counts(slot, speed)[1], 2);
    NETW_CHECK_EQ(pool->ledger_fields(slot).size(), 1);

    NETW_CHECK_EQ(pool->field_recovery(slot + 9000).size(), 0);
    NETW_CHECK_EQ(pool->ledger_fields(slot + 9000).size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Cursor] the drive cursor is the slot's, and "
    "a closed slot answers the default rather than the last slot's reading"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK(pool->tick_delta_of(slot) == doctest::Approx(1.0 / 60.0));
    NETW_CHECK_EQ(pool->frame_index_of(slot), 0);
    NETW_CHECK_EQ(pool->declared_quantum_of(slot), 1);
    NETW_CHECK_EQ(pool->latest_input_tick_of(slot), -1);
    NETW_CHECK_EQ(pool->last_driven_input_tick_of(slot), -1);
    NETW_CHECK_EQ(pool->last_frame_transition_tick_of(slot), -1);
    NETW_CHECK_EQ(pool->last_recorded_input_tick_of(slot), -1);
    CHECK_FALSE(pool->raw_fingerprints_of(slot));

    pool->set_tick_delta(slot, 1.0 / 30.0);
    pool->set_frame_index(slot, 12);
    pool->set_declared_quantum(slot, 3);
    pool->set_latest_input_tick(slot, 40);
    pool->set_last_driven_input_tick(slot, 39);
    pool->set_last_frame_transition_tick(slot, 38);
    pool->set_last_recorded_input_tick(slot, 37);
    pool->set_raw_fingerprints(slot, true);

    CHECK(pool->tick_delta_of(slot) == doctest::Approx(1.0 / 30.0));
    NETW_CHECK_EQ(pool->frame_index_of(slot), 12);
    NETW_CHECK_EQ(pool->declared_quantum_of(slot), 3);
    NETW_CHECK_EQ(pool->latest_input_tick_of(slot), 40);
    NETW_CHECK_EQ(pool->last_driven_input_tick_of(slot), 39);
    NETW_CHECK_EQ(pool->last_frame_transition_tick_of(slot), 38);
    NETW_CHECK_EQ(pool->last_recorded_input_tick_of(slot), 37);
    CHECK(pool->raw_fingerprints_of(slot));

    CHECK(pool->tick_delta_of(slot + 9000) == doctest::Approx(1.0 / 60.0));
    NETW_CHECK_EQ(pool->declared_quantum_of(slot + 9000), 1);
    NETW_CHECK_EQ(pool->latest_input_tick_of(slot + 9000), -1);
    CHECK_FALSE(pool->raw_fingerprints_of(slot + 9000));

    pool->set_frame_index(slot + 9000, 99);
    NETW_CHECK_EQ(pool->frame_index_of(slot), 12);
}

TEST_CASE(
    "[Networked][Predict][Cursor] a rewire keeps the cursor, because "
    "where a drive got to is not a fact about the field table"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    pool->set_latest_input_tick(slot, 77);
    pool->set_declared_quantum(slot, 4);

    pool->rewire(slot, every_family_populated());
    NETW_CHECK_EQ(pool->latest_input_tick_of(slot), 77);
    NETW_CHECK_EQ(pool->declared_quantum_of(slot), 4);
}

TEST_CASE(
    "[Networked][Predict][Cursor] the lane cursor is the slot's, and "
    "its defaults say nothing has happened rather than that something did"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    NETW_CHECK_EQ(pool->tape_epoch_of(slot), 0);
    NETW_CHECK_EQ(pool->next_tape_entry_index_of(slot), 0);
    NETW_CHECK_EQ(pool->last_driven_entry_index_of(slot), -1);
    NETW_CHECK_EQ(pool->replay_cursor_of(slot), -1);
    NETW_CHECK_EQ(pool->ack_of(slot), -1);
    NETW_CHECK_EQ(pool->ack_of_acks_of(slot), -1);
    NETW_CHECK_EQ(pool->owner_ack_floor_of(slot), -1);
    NETW_CHECK_EQ(pool->arrivals_this_frame_of(slot), 0);
    CHECK_FALSE(pool->ack_advanced_of(slot));
    CHECK_FALSE(pool->last_replayed_fresh_of(slot));

    CHECK(pool->ack_domain_confirmed_of(slot));

    pool->set_ack(slot, 12);
    pool->set_ack_advanced(slot, true);
    pool->set_ack_domain_confirmed(slot, false);
    pool->set_replay_cursor(slot, 9);
    pool->set_arrivals_this_frame(slot, 3);

    NETW_CHECK_EQ(pool->ack_of(slot), 12);
    CHECK(pool->ack_advanced_of(slot));
    CHECK_FALSE(pool->ack_domain_confirmed_of(slot));
    NETW_CHECK_EQ(pool->replay_cursor_of(slot), 9);
    NETW_CHECK_EQ(pool->arrivals_this_frame_of(slot), 3);

    NETW_CHECK_EQ(pool->ack_of(slot + 9000), -1);
    CHECK(pool->ack_domain_confirmed_of(slot + 9000));
    pool->set_ack(slot + 9000, 40);
    NETW_CHECK_EQ(pool->ack_of(slot), 12);
}

TEST_CASE(
    "[Networked][Predict][Cursor] the acknowledgement is confirmed "
    "until something says otherwise, which no other default does"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK(pool->ack_domain_confirmed_of(slot));
    CHECK_FALSE(pool->ack_advanced_of(slot));
    CHECK_FALSE(pool->last_replayed_fresh_of(slot));

    pool->rewire(slot, every_family_populated());
    CHECK(pool->ack_domain_confirmed_of(slot));
}

TEST_CASE(
    "[Networked][Predict][Latch] a report-once latch starts unfired "
    "and stays fired, which is what makes it report once"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK_FALSE(pool->invalid_witness_reported_of(slot));
    CHECK_FALSE(pool->invalid_command_predictor_reported_of(slot));
    CHECK_FALSE(pool->joint_refusal_reported_of(slot));
    CHECK_FALSE(pool->island_gap_reported_of(slot));

    pool->set_invalid_witness_reported(slot, true);
    CHECK(pool->invalid_witness_reported_of(slot));
    pool->set_invalid_witness_reported(slot, true);
    CHECK(pool->invalid_witness_reported_of(slot));

    pool->rewire(slot, every_family_populated());
    CHECK(pool->invalid_witness_reported_of(slot));

    CHECK_FALSE(pool->invalid_witness_reported_of(slot + 9000));
}

TEST_CASE(
    "[Networked][Predict][Latch] the joint window opens at -1, which "
    "is the reading that says no group has claimed this entity"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    NETW_CHECK_EQ(pool->joint_basis_of(slot), -1);
    NETW_CHECK_EQ(pool->joint_relay_floor_of(slot), -1);
    NETW_CHECK_EQ(pool->joint_epoch_floor_of(slot), -1);
    NETW_CHECK_EQ(pool->tenure_begin_of(slot), -1);
    NETW_CHECK_EQ(pool->tenure_end_of(slot), -1);

    pool->set_joint_basis(slot, 0);
    NETW_CHECK_EQ(pool->joint_basis_of(slot), 0);

    pool->set_tenure_begin(slot, 4);
    pool->set_tenure_end(slot, 9);
    NETW_CHECK_EQ(pool->tenure_begin_of(slot), 4);
    NETW_CHECK_EQ(pool->tenure_end_of(slot), 9);

    NETW_CHECK_EQ(pool->joint_basis_of(slot + 9000), -1);
    NETW_CHECK_EQ(pool->tenure_end_of(slot + 9000), -1);
}

TEST_CASE(
    "[Networked][Predict][Latch] the stream is unreconstructed and "
    "the entity unregistered until each is declared otherwise"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK_FALSE(pool->stream_reconstructed_of(slot));
    CHECK_FALSE(pool->registered_of(slot));
    CHECK_FALSE(pool->fallback_latched_of(slot));
    CHECK_FALSE(pool->last_correction_teleported_of(slot));
    NETW_CHECK_EQ(pool->validated_class_hash_of(slot), 0);

    pool->set_stream_reconstructed(slot, true);
    pool->set_registered(slot, true);
    pool->set_validated_class_hash(slot, 8123);
    CHECK(pool->stream_reconstructed_of(slot));
    CHECK(pool->registered_of(slot));
    NETW_CHECK_EQ(pool->validated_class_hash_of(slot), 8123);

    CHECK_FALSE(pool->stream_reconstructed_of(slot + 9000));
    NETW_CHECK_EQ(pool->validated_class_hash_of(slot + 9000), 0);
}

TEST_CASE(
    "[Networked][Predict][Axes] the resolved role and correction are "
    "the slot's own config, written by one act and read by everything"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::FRAME),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));

    NETW_CHECK_EQ(pool->role_of(slot), int(netw::Role::CONSUME));
    NETW_CHECK_EQ(pool->correction_of(slot), int(netw::CorrectionMode::SNAP));

    pool->set_role(slot, int(netw::Role::PREDICT));
    pool->set_correction(slot, int(netw::CorrectionMode::REPLAY));
    NETW_CHECK_EQ(pool->role_of(slot), int(netw::Role::PREDICT));
    NETW_CHECK_EQ(pool->correction_of(slot), int(netw::CorrectionMode::REPLAY));

    NETW_CHECK_EQ(
        pool->correction_of(slot + 9000),
        int(netw::CorrectionMode::REPLAY)
    );
    pool->set_correction(slot + 9000, int(netw::CorrectionMode::SNAP));
    NETW_CHECK_EQ(pool->correction_of(slot), int(netw::CorrectionMode::REPLAY));
}

TEST_CASE(
    "[Networked][Predict][Roster] the four engine rosters are the "
    "slot's, they are sets, and they do not leak into one another"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    Ref<netw::NetwEntity> one;
    one.instantiate();
    Ref<netw::NetwEntity> two;
    two.instantiate();

    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_SIMULATED),
        0
    );

    CHECK(pool->roster_add(slot, NetwPredictionEngine::ROSTER_SIMULATED, one));
    CHECK_FALSE(
        pool->roster_add(slot, NetwPredictionEngine::ROSTER_SIMULATED, one)
    );
    CHECK(pool->roster_add(slot, NetwPredictionEngine::ROSTER_SIMULATED, two));
    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_SIMULATED),
        2
    );
    CHECK(pool->roster_has(slot, NetwPredictionEngine::ROSTER_SIMULATED, one));
    CHECK_FALSE(pool->roster_has(
        slot,
        NetwPredictionEngine::ROSTER_JOINT_LINGERING,
        one
    ));
    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_JOINT_LINGERING),
        0
    );

    CHECK(
        pool->roster_erase(slot, NetwPredictionEngine::ROSTER_SIMULATED, one)
    );
    CHECK_FALSE(
        pool->roster_erase(slot, NetwPredictionEngine::ROSTER_SIMULATED, one)
    );
    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_SIMULATED),
        1
    );

    pool->roster_clear(slot, NetwPredictionEngine::ROSTER_SIMULATED);
    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_SIMULATED),
        0
    );

    CHECK_FALSE(
        pool->roster_add(slot, NetwPredictionEngine::ROSTER_SIMULATED, nullptr)
    );
    CHECK_FALSE(pool->roster_add(slot, 99, one));
    CHECK_FALSE(pool->roster_add(
        slot + 9000,
        NetwPredictionEngine::ROSTER_SIMULATED,
        one
    ));
}

TEST_CASE(
    "[Networked][Predict][Roster] an assignment replaces the roster "
    "whole, and reading it drops the members that have since been freed"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());
    Ref<netw::NetwEntity> kept;
    kept.instantiate();

    godot::TypedArray<netw::NetwEntity> members;
    members.push_back(kept);
    {
        Ref<netw::NetwEntity> transient;
        transient.instantiate();
        members.push_back(transient);
        pool->roster_assign(
            slot,
            NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
            members
        );
        NETW_CHECK_EQ(
            pool->roster_count(
                slot,
                NetwPredictionEngine::ROSTER_ISLAND_MEMBERS
            ),
            2
        );
        members.remove_at(1);
    }

    NETW_CHECK_EQ(
        pool->roster_list(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS)
            .size(),
        1
    );
    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS),
        1
    );

    pool->roster_assign(
        slot,
        NetwPredictionEngine::ROSTER_ISLAND_MEMBERS,
        godot::TypedArray<netw::NetwEntity>()
    );
    NETW_CHECK_EQ(
        pool->roster_count(slot, NetwPredictionEngine::ROSTER_ISLAND_MEMBERS),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Rows] the four payload rows are the slot's, "
    "and each is a separate row rather than one the others alias"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    NETW_CHECK_EQ(pool->frame_input_of(slot).size(), 0);
    NETW_CHECK_EQ(pool->stall_input_of(slot).size(), 0);
    NETW_CHECK_EQ(pool->last_input_of(slot).size(), 0);
    NETW_CHECK_EQ(pool->open_topology_of(slot).size(), 0);

    godot::Dictionary driven;
    driven[StringName("throttle")] = 1.0;
    pool->set_frame_input(slot, driven);

    NETW_CHECK_EQ(pool->frame_input_of(slot).size(), 1);
    NETW_CHECK_EQ(pool->stall_input_of(slot).size(), 0);
    NETW_CHECK_EQ(pool->last_input_of(slot).size(), 0);
    NETW_CHECK_EQ(pool->open_topology_of(slot).size(), 0);

    godot::Dictionary facts;
    facts[StringName("floor")] = true;
    pool->set_open_topology(slot, facts);
    NETW_CHECK_EQ(pool->open_topology_of(slot).size(), 1);
    CHECK(pool->open_topology_of(slot).has(StringName("floor")));
    NETW_CHECK_EQ(pool->frame_input_of(slot).size(), 1);
    CHECK(pool->frame_input_of(slot).has(StringName("throttle")));
    CHECK_FALSE(pool->frame_input_of(slot).has(StringName("floor")));

    pool->set_frame_input(slot, godot::Dictionary());
    NETW_CHECK_EQ(pool->frame_input_of(slot).size(), 0);
    NETW_CHECK_EQ(pool->open_topology_of(slot).size(), 1);

    NETW_CHECK_EQ(pool->frame_input_of(slot + 9000).size(), 0);
    pool->set_frame_input(slot + 9000, driven);
    NETW_CHECK_EQ(pool->frame_input_of(slot).size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Rows] the timeline a slot is bound to is "
    "readable back from it, so nothing needs a second reference"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK(pool->timeline_of(slot).is_null());
    CHECK(pool->timeline_of(slot + 9000).is_null());

    const Ref<netw::NetwTimeline> lane = netw::NetwTimeline::create(64);
    pool->bind_timeline(slot, lane);
    CHECK(pool->timeline_of(slot) == lane);

    pool->bind_timeline(slot, Ref<netw::NetwTimeline>());
    CHECK(pool->timeline_of(slot).is_null());
}

TEST_CASE(
    "[Networked][Predict][Rows] the two property set bindings are the "
    "slot's, and they are bound as one act because a rewire replaces both"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK(pool->state_binding_of(slot).is_null());
    CHECK(pool->input_binding_of(slot).is_null());
    CHECK(pool->state_binding_of(slot + 9000).is_null());

    Ref<netw::NetwPropertySet> declared;
    declared.instantiate();
    const Ref<netw::NetwPropertySetBinding> state
        = netw::NetwPropertySetBinding::create(declared, nullptr);
    const Ref<netw::NetwPropertySetBinding> input
        = netw::NetwPropertySetBinding::create(declared, nullptr);
    REQUIRE(state.is_valid());
    REQUIRE(input.is_valid());
    CHECK(state != input);

    pool->bind_property_sets(slot, state, input);
    CHECK(pool->state_binding_of(slot) == state);
    CHECK(pool->input_binding_of(slot) == input);

    pool->bind_property_sets(
        slot,
        Ref<netw::NetwPropertySetBinding>(),
        Ref<netw::NetwPropertySetBinding>()
    );
    CHECK(pool->state_binding_of(slot).is_null());
    CHECK(pool->input_binding_of(slot).is_null());
}

TEST_CASE(
    "[Networked][Predict][Axes] the two declared axes are the slot's, "
    "and a slot nothing declared for holds neither"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open(every_family_populated());

    CHECK_FALSE(pool->declared_authority_of(slot));
    CHECK_FALSE(pool->declared_controlled_locally_of(slot));

    pool->declare_axes(slot, true, false, false);
    CHECK(pool->declared_authority_of(slot));
    CHECK_FALSE(pool->declared_controlled_locally_of(slot));

    pool->declare_axes(slot, false, true, false);
    CHECK_FALSE(pool->declared_authority_of(slot));
    CHECK(pool->declared_controlled_locally_of(slot));

    CHECK_FALSE(pool->declared_authority_of(slot + 9000));
    pool->declare_axes(slot + 9000, true, true, false);
    CHECK_FALSE(pool->declared_authority_of(slot + 9000));
    CHECK_FALSE(pool->declared_authority_of(slot));
}

#endif

} // namespace TestNetwPredictStampLaws

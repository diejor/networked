#include "support/netw_test.h"

#include <cmath>

#include "netw/predict/compare.hpp"

using namespace godot;

namespace TestNetwPredictCompareLaws {

using namespace netw;
using namespace netw::predict;

Wiring wiring() {
    LocalVector<FieldDecl> fields;
    FieldDecl x;
    x.key = StringName("x");
    x.property_class = int(PropertyClass::CAUSAL);
    fields.push_back(x);
    FieldDecl derived;
    derived.key = StringName("derived");
    derived.property_class = int(PropertyClass::DERIVED);
    fields.push_back(derived);
    return compile(fields);
}

Journal journal_with(int64_t p_transition, int32_t p_post_fp) {
    Journal journal;
    JournalOpen row;
    row.pre_fp = 10;
    row.c_hash = 20;
    row.evidence_mask = EVIDENCE_WITNESS;
    row.pre_families.pose = 1;
    journal.open(p_transition, row);
    FamilyFingerprints post;
    post.pose = 2;
    journal.close(p_transition, p_post_fp, post);
    return journal;
}

StateRow state(double p_x, double p_derived) {
    StateRow out;
    out.resize(2);
    out.set(0, p_x);
    out.set(1, p_derived);
    return out;
}

LocalVector<double> tolerances(double p_x, double p_derived = -1.0) {
    LocalVector<double> out;
    out.resize(2);
    out[0] = p_x;
    out[1] = p_derived;
    return out;
}

StateVerdict judged(
    const Wiring &p_wiring,
    Journal &p_journal,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    double p_tolerance,
    bool p_reconstructed = true
) {
    StateVerdict out;
    compare_state(
        p_wiring,
        p_journal,
        8,
        4,
        p_predicted,
        p_authority,
        tolerances(p_tolerance),
        tolerances(p_tolerance),
        p_tolerance,
        p_reconstructed,
        out
    );
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] a derived field is absent from the "
    "causal field errors the vote reads and present in the unfiltered ones "
    "the published per-field divergence is built from"
) {
    const Wiring declared = wiring();
    Journal unequal = journal_with(4, 99);
    unequal.mark_ack(4, false);
    const StateVerdict verdict = judged(
        declared,
        unequal,
        state(1.0, 10.0),
        state(5.0, 1000.0),
        1000.0
    );

    NETW_CHECK_EQ(int(verdict.field_errors.size()), 2);
    NETW_CHECK_EQ(int(verdict.all_field_errors.size()), 2);
    NETW_CHECK_CLOSE(verdict.field_errors[0], 4.0, 0.0001);
    NETW_CHECK_CLOSE(verdict.all_field_errors[0], 4.0, 0.0001);
    NETW_CHECK_CLOSE(verdict.field_errors[1], -1.0, 0.0001);
    NETW_CHECK_CLOSE(verdict.all_field_errors[1], 990.0, 0.0001);
    NETW_CHECK_CLOSE(verdict.divergence, 4.0, 0.0001);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] a field the authority row omits is "
    "absent from both the causal and the unfiltered field errors"
) {
    const Wiring declared = wiring();
    Journal unequal = journal_with(4, 99);
    unequal.mark_ack(4, false);
    StateRow partial;
    partial.resize(2);
    partial.set(0, 5.0);
    const StateVerdict verdict
        = judged(declared, unequal, state(1.0, 10.0), partial, 1000.0);

    NETW_CHECK_CLOSE(verdict.all_field_errors[0], 4.0, 0.0001);
    NETW_CHECK_CLOSE(verdict.all_field_errors[1], -1.0, 0.0001);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] exactness outranks tolerance"
) {
    const Wiring declared = wiring();
    Journal unequal = journal_with(4, 99);
    unequal.mark_ack(4, false);
    StateVerdict verdict = judged(
        declared,
        unequal,
        state(1.0, 10.0),
        state(5.0, 1000.0),
        1000.0
    );
    CHECK(verdict.compared);
    CHECK(verdict.corrected);
    CHECK(verdict.settled);
    NETW_CHECK_EQ(int(verdict.exact), int(ExactVerdict::UNEQUAL));
    NETW_CHECK_CLOSE(verdict.divergence, 4.0, 0.0001);

    Journal equal = journal_with(4, 99);
    equal.mark_ack(4, true);
    verdict
        = judged(declared, equal, state(1.0, 10.0), state(5.0, 1000.0), 0.001);
    CHECK_FALSE(verdict.corrected);
    NETW_CHECK_EQ(verdict.meter, 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] unclaimed exactness spends the "
    "declared tolerance"
) {
    const Wiring declared = wiring();
    Journal journal = journal_with(4, 99);
    StateVerdict inside
        = judged(declared, journal, state(1.0, 10.0), state(1.01, 1000.0), 0.1);
    CHECK_FALSE(inside.corrected);
    CHECK_FALSE(inside.settled);

    journal.mark_domain(4, netw::predict::Domain::OUT_OF_DOMAIN);
    StateVerdict outside
        = judged(declared, journal, state(1.0, 10.0), state(1.2, 1000.0), 0.1);
    CHECK(outside.corrected);
    CHECK(outside.settled);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] only causal fields judge a state"
) {
    const Wiring declared = wiring();
    Journal journal = journal_with(4, 99);
    StateVerdict verdict
        = judged(declared, journal, state(1.0, 10.0), state(1.0, 1000.0), 0.0);
    CHECK_FALSE(verdict.corrected);
    NETW_CHECK_CLOSE(verdict.divergence, 0.0, 0.0001);
    CHECK(verdict.field_errors[1] < 0.0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] reconstruction gates comparison"
) {
    const Wiring declared = wiring();
    Journal journal = journal_with(4, 99);
    StateVerdict verdict = judged(
        declared,
        journal,
        state(1.0, 10.0),
        state(9.0, 10.0),
        0.0,
        false
    );
    CHECK_FALSE(verdict.compared);
    CHECK_FALSE(verdict.corrected);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] ack evidence names the first "
    "unequal antecedent"
) {
    Journal journal = journal_with(4, 99);
    EvidenceRow peer;
    peer.complete = true;
    peer.pre_fp = 11;
    peer.c_hash = 20;
    peer.post_fp = 100;
    peer.evidence_mask = EVIDENCE_WITNESS;
    peer.pre_families.pose = 9;
    const AckVerdict verdict = admit_ack(journal, 4, peer, false);

    CHECK(verdict.compared);
    NETW_CHECK_EQ(int(verdict.exact), int(ExactVerdict::UNEQUAL));
    NETW_CHECK_EQ(
        int(verdict.attribution),
        int(netw::predict::Attribution::PRE_STATE)
    );
    NETW_CHECK_EQ(int(verdict.differing_family), int(DifferingFamily::POSE));
    NETW_CHECK_EQ(
        int(journal.attribution_of(4)),
        int(netw::predict::Attribution::PRE_STATE)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] two rotations a rounding apart "
    "read as no difference, never as NaN"
) {
    const Quaternion unit;
    const Quaternion rounded(0.0f, 0.0f, 0.0f, 1.0000001f);
    const double quaternion = value_error(unit, rounded, false);
    const double basis = value_error(Basis(unit), Basis(rounded), false);
    CHECK_FALSE(std::isnan(quaternion));
    CHECK_FALSE(std::isnan(basis));
    NETW_CHECK_EQ(quaternion, 0.0);
    NETW_CHECK_EQ(basis, 0.0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] a quantized rotation tolerates no "
    "less than the comparison can tell apart"
) {
    Ref<NetwQuantizeQuaternion> quantizer;
    quantizer.instantiate();
    quantizer->set_bit_count(16);
    LocalVector<FieldDecl> fields;
    FieldDecl rotation;
    rotation.key = StringName("quaternion");
    rotation.quantizer = quantizer;
    rotation.type = int(Variant::QUATERNION);
    fields.push_back(rotation);
    Wiring rotated = compile(fields);
    tolerate_quantizer_steps(rotated);

    const Quaternion unit;
    const real_t below = std::nextafter(real_t(1.0), real_t(0.0));
    const Quaternion rounded(0.0f, 0.0f, 0.0f, below);
    const double smallest = value_error(unit, rounded, false);
    NETW_CHECK_GT(smallest, 0.0);
    NETW_CHECK_GE(rotated.epsilon[0], smallest);
    NETW_CHECK_GE(
        rotated.epsilon[0],
        3.0 * quantizer->max_error(Variant::QUATERNION)
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] incomplete evidence names no cause"
) {
    Journal journal = journal_with(4, 99);
    EvidenceRow peer;
    peer.post_fp = 100;
    const AckVerdict verdict = admit_ack(journal, 4, peer, false);

    CHECK(verdict.compared);
    CHECK_FALSE(verdict.evidence_complete);
    NETW_CHECK_EQ(
        int(verdict.attribution),
        int(netw::predict::Attribution::UNKNOWN)
    );
}

} // namespace TestNetwPredictCompareLaws

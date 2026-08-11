#include "support/netw_test.h"

#include <cmath>

#include "netw/predict/compare.hpp"

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

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] exactness outranks tolerance"
) {
    const Wiring declared = wiring();
    Journal unequal = journal_with(4, 99);
    unequal.mark_ack(4, false);
    StateVerdict verdict = compare_state(
        declared,
        unequal,
        8,
        4,
        state(1.0, 10.0),
        state(5.0, 1000.0),
        tolerances(1000.0),
        tolerances(1000.0),
        1000.0,
        true
    );
    CHECK(verdict.compared);
    CHECK(verdict.corrected);
    CHECK(verdict.settled);
    NETW_CHECK_EQ(int(verdict.exact), int(ExactVerdict::UNEQUAL));
    NETW_CHECK_CLOSE(verdict.divergence, 4.0, 0.0001);

    Journal equal = journal_with(4, 99);
    equal.mark_ack(4, true);
    verdict = compare_state(
        declared,
        equal,
        8,
        4,
        state(1.0, 10.0),
        state(5.0, 1000.0),
        tolerances(0.001),
        tolerances(0.001),
        0.001,
        true
    );
    CHECK_FALSE(verdict.corrected);
    NETW_CHECK_EQ(verdict.meter, 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] unclaimed exactness spends the "
    "declared tolerance"
) {
    const Wiring declared = wiring();
    Journal journal = journal_with(4, 99);
    StateVerdict inside = compare_state(
        declared,
        journal,
        8,
        4,
        state(1.0, 10.0),
        state(1.01, 1000.0),
        tolerances(0.1),
        tolerances(0.1),
        0.1,
        true
    );
    CHECK_FALSE(inside.corrected);
    CHECK_FALSE(inside.settled);

    journal.mark_domain(4, netw::predict::Domain::OUT_OF_DOMAIN);
    StateVerdict outside = compare_state(
        declared,
        journal,
        8,
        4,
        state(1.0, 10.0),
        state(1.2, 1000.0),
        tolerances(0.1),
        tolerances(0.1),
        0.1,
        true
    );
    CHECK(outside.corrected);
    CHECK(outside.settled);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] only causal fields judge a state"
) {
    const Wiring declared = wiring();
    Journal journal = journal_with(4, 99);
    StateVerdict verdict = compare_state(
        declared,
        journal,
        8,
        4,
        state(1.0, 10.0),
        state(1.0, 1000.0),
        tolerances(0.0),
        tolerances(0.0),
        0.0,
        true
    );
    CHECK_FALSE(verdict.corrected);
    NETW_CHECK_CLOSE(verdict.divergence, 0.0, 0.0001);
    CHECK(verdict.field_errors[1] < 0.0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] reconstruction gates comparison"
) {
    const Wiring declared = wiring();
    Journal journal = journal_with(4, 99);
    StateVerdict verdict = compare_state(
        declared,
        journal,
        8,
        4,
        state(1.0, 10.0),
        state(9.0, 10.0),
        tolerances(0.0),
        tolerances(0.0),
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

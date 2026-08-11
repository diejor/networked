#pragma once

/* The receive-side comparison for one predicted transition.
 *
 * State is indexed by the declaration's field table. Missing values have a
 * presence column, so absence never shares a representation with `null`.
 * Comparison produces one record and mutates no body. The shell may act on the
 * verdict later, while a shell-less law reads the same record directly.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/prediction_core.hpp"

namespace netw {

using namespace godot;

namespace predict {

constexpr int METER_SATURATED = 0x7FFFFFFF;

struct StateRow {
    LocalVector<Variant> values;
    LocalVector<uint8_t> present;

    StateRow() = default;
    StateRow(const StateRow &p_other);
    StateRow &operator=(const StateRow &p_other);

    void resize(int p_count);
    void set(int p_field, const Variant &p_value);
    bool has(int p_field) const;
    bool any() const;
};

struct EvidenceRow {
    int32_t pre_fp = 0;
    int32_t c_hash = 0;
    int32_t e_digest = 0;
    int32_t post_fp = 0;
    FamilyFingerprints pre_families;
    FamilyFingerprints post_families;
    int32_t topo_fp = 0;
    int32_t witness_fp = 0;
    int32_t raw_fp = 0;
    uint8_t evidence_mask = 0;
    bool complete = false;
};

struct AckVerdict {
    int64_t transition = -1;
    ExactVerdict exact = ExactVerdict::UNJUDGED;
    Attribution attribution = Attribution::UNKNOWN;
    DifferingFamily differing_family = DifferingFamily::NONE;
    bool compared = false;
    bool evidence_complete = false;
};

struct StateVerdict {
    int64_t recv_tick = -1;
    int64_t transition = -1;
    ExactVerdict exact = ExactVerdict::UNJUDGED;
    Domain domain = Domain::OUT_OF_DOMAIN;
    Attribution attribution = Attribution::UNKNOWN;
    LocalVector<double> field_errors;
    double divergence = 0.0;
    int meter = 0;
    bool compared = false;
    bool corrected = false;
    bool settled = false;
};

struct CompareStats {
    int comparisons_ran = 0;
    int comparisons_skipped = 0;
    int fp_verified = 0;
    int fp_mismatches = 0;
    int64_t first_divergent_transition = -1;
};

double value_error(const Variant &p_left, const Variant &p_right, bool p_angle);

Attribution attribute(
    bool p_pre_equal,
    bool p_command_equal,
    bool p_environment_equal,
    bool p_topology_equal,
    bool p_raw_equal,
    bool p_witness_equal,
    int p_local_evidence,
    int p_peer_evidence,
    bool p_evidence_complete
);

AckVerdict admit_ack(
    Journal &p_journal,
    int64_t p_transition,
    const EvidenceRow &p_peer,
    bool p_substituted
);

StateVerdict compare_state(
    const Wiring &p_wiring,
    Journal &p_journal,
    int64_t p_recv_tick,
    int64_t p_transition,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const LocalVector<double> &p_correction_tolerances,
    const LocalVector<double> &p_meter_tolerances,
    double p_fallback_epsilon,
    bool p_stream_reconstructed
);

} // namespace predict

} // namespace netw

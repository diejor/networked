#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/prediction_core.hpp"
#include "netw/wire/code_row.hpp"

namespace netw::predict {

#if defined(NETW_TESTS)
int64_t script_dictionaries_built();
void note_script_dictionary();
#define NETW_NOTE_SCRIPT_DICTIONARY() ::netw::predict::note_script_dictionary()
#else
#define NETW_NOTE_SCRIPT_DICTIONARY() ((void)0)
#endif

constexpr int METER_SATURATED = 0x7FFFFFFF;

struct StateRow {
    godot::LocalVector<godot::Variant> values;
    godot::LocalVector<uint8_t> present;

    StateRow() = default;
    StateRow(const StateRow &p_other);
    StateRow &operator=(const StateRow &p_other);
    StateRow(StateRow &&p_other) = default;
    StateRow &operator=(StateRow &&p_other) = default;

    void resize(int p_count);
    void set(int p_field, const godot::Variant &p_value);
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
    godot::LocalVector<double> field_errors;
    godot::LocalVector<double> all_field_errors;
    double divergence = 0.0;
    int meter = 0;
    bool compared = false;
    bool corrected = false;
    bool settled = false;
};

struct Judged {
    double divergence = 0.0;
    int meter = 0;
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

double value_error(
    const godot::Variant &p_left,
    const godot::Variant &p_right,
    bool p_angle
);

double divergence_by_field(
    const godot::Dictionary &p_predicted,
    const godot::Dictionary &p_authoritative,
    godot::Dictionary p_out,
    const godot::Dictionary &p_angles
);

void read_payload(
    const Wiring &p_wiring,
    const godot::Dictionary &p_payload,
    StateRow &r_row
);

godot::Dictionary payload_of(const Wiring &p_wiring, const StateRow &p_row);
godot::Dictionary payload_of(const FieldCodec &p_codec, const StateRow &p_row);
void read_payload(
    const FieldCodec &p_codec,
    const godot::Dictionary &p_payload,
    StateRow &r_row
);
void zero_state_row(const FieldCodec &p_codec, StateRow &r_row);

void canonicalize_row(
    const FieldCodec &p_codec,
    StateRow &r_row,
    wire::CodeRow &r_scratch,
    godot::PackedInt64Array &r_codes
);

int measure(
    const godot::LocalVector<double> &p_errors,
    const godot::LocalVector<double> &p_tolerances
);

double judge_rows(
    const Wiring &p_wiring,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const godot::LocalVector<double> &p_correction_tolerances,
    godot::LocalVector<double> &r_errors,
    bool &r_diverged
);

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

void compare_state(
    const Wiring &p_wiring,
    Journal &p_journal,
    int64_t p_recv_tick,
    int64_t p_transition,
    const StateRow &p_predicted,
    const StateRow &p_authority,
    const godot::LocalVector<double> &p_correction_tolerances,
    const godot::LocalVector<double> &p_meter_tolerances,
    double p_fallback_epsilon,
    bool p_stream_reconstructed,
    StateVerdict &r_out
);

} // namespace netw::predict

#pragma once

#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/frames.hpp"

namespace netw::predict {

using netw::table::SchemaRecord;

class CommandFrameRecord {
    CommandFrame frame;
    SchemaRecord schema;
    wire::WirePlan plan;

public:
    static bool open(
        const SchemaRecord &p_schema,
        CommandFrameRecord &r_record
    );

    static bool decode(
        const SchemaRecord &p_schema,
        const godot::PackedByteArray &p_bytes,
        CommandFrameRecord &r_record
    );

    const CommandFrame &wire() const {
        return frame;
    }

    void set_epoch(int p_epoch);
    int epoch() const;
    void set_ack_of_acks(int64_t p_transition);
    int64_t ack_of_acks() const;

    bool append_transition(int64_t p_index, int64_t p_label, bool p_fresh);
    bool append_values(const godot::LocalVector<godot::Variant> &p_values);
    bool append_codes(const uint64_t *p_words);

    const wire::WirePlan &row_plan() const {
        return plan;
    }
    bool append_evidence(
        int p_evidence_mask,
        int p_pre_fp,
        int p_post_fp,
        int p_e_digest,
        int p_topo_fp,
        int p_witness_fp,
        const godot::PackedInt32Array &p_pre_family_fps,
        const godot::PackedInt32Array &p_post_family_fps,
        int p_raw_fp
    );

    int transition_count() const {
        return int(frame.transitions.size());
    }

    const TransitionWire &transition_at(int p_index) const {
        return frame.transitions[uint32_t(p_index)];
    }

    int payload_count() const {
        return int(frame.payloads.size());
    }

    bool read_payload(
        int p_index,
        const godot::LocalVector<int> &p_fields,
        StateRow &r_row
    ) const;

    int evidence_count() const {
        return int(frame.evidence.size());
    }

    const CommandEvidenceWire &evidence_at(int p_index) const {
        return frame.evidence[uint32_t(p_index)];
    }

    godot::PackedByteArray to_bytes() const;
};

} // namespace netw::predict

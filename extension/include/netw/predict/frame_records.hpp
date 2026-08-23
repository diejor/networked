#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/predict/frames.hpp"
#include "netw/api/schema_core.hpp"

namespace netw {

class NetwPredictCommandFrame : public godot::RefCounted {
    GDCLASS(NetwPredictCommandFrame, godot::RefCounted)

    predict::CommandFrame frame;
    godot::Ref<SchemaRecord> schema;
    wire::WirePlan plan;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictCommandFrame> create(
        const godot::Ref<SchemaRecord> &p_schema
    );

    static godot::Ref<NetwPredictCommandFrame> from_bytes(
        const godot::Ref<SchemaRecord> &p_schema,
        const godot::PackedByteArray &p_bytes
    );

    void set_epoch(int p_epoch);
    int epoch() const;
    void set_ack_of_acks(int64_t p_transition);
    int64_t ack_of_acks() const;

    bool append_transition(int64_t p_index, int64_t p_label, bool p_fresh);
    bool append_payload(const godot::Array &p_values);
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

    int transition_count() const;
    godot::PackedInt64Array indices() const;
    godot::PackedInt64Array labels() const;
    godot::PackedByteArray fresh_flags() const;

    int payload_count() const;
    godot::Array payload_at(int p_index) const;

    int evidence_count() const;
    godot::PackedByteArray evidence_masks() const;
    godot::PackedInt32Array pre_fps() const;
    godot::PackedInt32Array post_fps() const;
    godot::PackedInt32Array e_digests() const;
    godot::PackedInt32Array topo_fps() const;
    godot::PackedInt32Array witness_fps() const;
    godot::PackedInt32Array raw_fps() const;
    godot::PackedInt32Array pre_family_fps() const;
    godot::PackedInt32Array post_family_fps() const;

    godot::PackedByteArray to_bytes() const;
};

class NetwPredictAckFrame : public godot::RefCounted {
    GDCLASS(NetwPredictAckFrame, godot::RefCounted)

    predict::AckFrame frame;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictAckFrame> create();
    static godot::Ref<NetwPredictAckFrame> from_bytes(
        const godot::PackedByteArray &p_bytes
    );

    void set_epoch(int p_epoch);
    int epoch() const;
    void set_base(int64_t p_transition);
    int64_t base() const;

    bool append_record(
        int p_evidence_mask,
        int p_pre_fp,
        int p_c_hash,
        int p_e_digest,
        int p_post_fp,
        int p_topo_fp,
        int p_witness_fp,
        const godot::PackedInt32Array &p_pre_family_fps,
        const godot::PackedInt32Array &p_post_family_fps,
        int p_raw_fp,
        int p_flags,
        int p_witness_class
    );

    int record_count() const;
    godot::PackedByteArray evidence_masks() const;
    godot::PackedInt32Array pre_fps() const;
    godot::PackedInt32Array c_hashes() const;
    godot::PackedInt32Array e_digests() const;
    godot::PackedInt32Array post_fps() const;
    godot::PackedInt32Array topo_fps() const;
    godot::PackedInt32Array witness_fps() const;
    godot::PackedInt32Array raw_fps() const;
    godot::PackedInt32Array pre_family_fps() const;
    godot::PackedInt32Array post_family_fps() const;
    godot::PackedByteArray flags() const;
    godot::PackedByteArray witness_class_bits() const;

    godot::PackedByteArray to_bytes() const;

    enum Flag {
        FLAG_SUBSTITUTED = predict::ACK_SUBSTITUTED,
        FLAG_SUPERSEDED = predict::ACK_SUPERSEDED,
    };
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredictAckFrame::Flag);

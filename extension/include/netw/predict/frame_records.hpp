#pragma once

/* The two prediction frames as records a caller fills and reads by column.
 *
 * Encoding and decoding are the same object seen from either end: a sender
 * appends rows and asks for bytes, a receiver hands over bytes and reads the
 * columns back. One spelling per direction is what keeps the two from drifting
 * the way two hand-written codecs did.
 *
 * The evidence columns stay packed rather than becoming a record per
 * transition, because every reader of them is already walking an index and a
 * per-transition object would allocate once per acknowledged transition per
 * frame.
 */

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/predict/frames.hpp"
#include "netw/table/schema_core.hpp"

namespace netw {

using namespace godot;

/* One owner-to-authority command window, planned against the input schema.
 *
 * The schema is fixed at construction because a payload row is only meaningful
 * beside the plan it was gathered under, and a frame that could be re-planned
 * mid-fill would hold rows of two shapes.
 */
class NetwPredictCommandFrame : public RefCounted {
    GDCLASS(NetwPredictCommandFrame, RefCounted)

    predict::CommandFrame frame;
    Ref<SchemaRecord> schema;
    wire::WirePlan plan;

protected:
    static void _bind_methods();

public:
    // An empty frame planned against `p_schema`, or null when the schema
    // declares no fixed-width row.
    static Ref<NetwPredictCommandFrame> create(
        const Ref<SchemaRecord> &p_schema
    );

    // The frame `p_bytes` states, or null when the bytes are not a complete
    // frame of this plan. A caller counts the refusal and drops the datagram,
    // because a partial window has no honest reading.
    static Ref<NetwPredictCommandFrame> from_bytes(
        const Ref<SchemaRecord> &p_schema,
        const PackedByteArray &p_bytes
    );

    void set_epoch(int p_epoch);
    int epoch() const;
    void set_ack_of_acks(int64_t p_transition);
    int64_t ack_of_acks() const;

    bool append_transition(int64_t p_index, int64_t p_label, bool p_fresh);
    bool append_payload(const Array &p_values);
    bool append_evidence(
        int p_evidence_mask,
        int p_pre_fp,
        int p_post_fp,
        int p_e_digest,
        int p_topo_fp,
        int p_witness_fp,
        const PackedInt32Array &p_pre_family_fps,
        const PackedInt32Array &p_post_family_fps,
        int p_raw_fp
    );

    int transition_count() const;
    PackedInt64Array indices() const;
    PackedInt64Array labels() const;
    PackedByteArray fresh_flags() const;

    int payload_count() const;
    Array payload_at(int p_index) const;

    int evidence_count() const;
    PackedByteArray evidence_masks() const;
    PackedInt32Array pre_fps() const;
    PackedInt32Array post_fps() const;
    PackedInt32Array e_digests() const;
    PackedInt32Array topo_fps() const;
    PackedInt32Array witness_fps() const;
    PackedInt32Array raw_fps() const;
    PackedInt32Array pre_family_fps() const;
    PackedInt32Array post_family_fps() const;

    PackedByteArray to_bytes() const;
};

/* One authority-to-owner acknowledgement prefix.
 *
 * The witness class rides the same wire byte as the flags and is read and
 * written here as its own column, so no caller repeats the shift.
 */
class NetwPredictAckFrame : public RefCounted {
    GDCLASS(NetwPredictAckFrame, RefCounted)

    predict::AckFrame frame;

protected:
    static void _bind_methods();

public:
    static Ref<NetwPredictAckFrame> create();
    static Ref<NetwPredictAckFrame> from_bytes(const PackedByteArray &p_bytes);

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
        const PackedInt32Array &p_pre_family_fps,
        const PackedInt32Array &p_post_family_fps,
        int p_raw_fp,
        int p_flags,
        int p_witness_class
    );

    int record_count() const;
    PackedByteArray evidence_masks() const;
    PackedInt32Array pre_fps() const;
    PackedInt32Array c_hashes() const;
    PackedInt32Array e_digests() const;
    PackedInt32Array post_fps() const;
    PackedInt32Array topo_fps() const;
    PackedInt32Array witness_fps() const;
    PackedInt32Array raw_fps() const;
    PackedInt32Array pre_family_fps() const;
    PackedInt32Array post_family_fps() const;
    PackedByteArray flags() const;
    PackedByteArray witness_class_bits() const;

    PackedByteArray to_bytes() const;

    enum Flag {
        FLAG_SUBSTITUTED = predict::ACK_SUBSTITUTED,
        FLAG_SUPERSEDED = predict::ACK_SUPERSEDED,
    };
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredictAckFrame::Flag);

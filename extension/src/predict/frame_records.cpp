#include "netw/predict/frame_records.hpp"

#include "godot/class_db.hpp"
#include "netw/wire/value_row.hpp"

namespace netw {

namespace {

const int FAMILY_COUNT = 3;

void append_families(
    PackedInt32Array &r_out,
    int32_t p_pose,
    int32_t p_momentum,
    int32_t p_controller
) {
    r_out.push_back(p_pose);
    r_out.push_back(p_momentum);
    r_out.push_back(p_controller);
}

} // namespace

Ref<NetwPredictCommandFrame> NetwPredictCommandFrame::create(
    const Ref<SchemaRecord> &p_schema
) {
    const wire::WirePlan plan = wire::WirePlan::compile(p_schema);
    if (!plan.valid()) {
        return Ref<NetwPredictCommandFrame>();
    }
    Ref<NetwPredictCommandFrame> made;
    made.instantiate();
    made->schema = p_schema;
    made->plan = plan;
    return made;
}

Ref<NetwPredictCommandFrame> NetwPredictCommandFrame::from_bytes(
    const Ref<SchemaRecord> &p_schema,
    const PackedByteArray &p_bytes
) {
    Ref<NetwPredictCommandFrame> made = create(p_schema);
    if (made.is_null()
        || !predict::decode_command(p_bytes, made->plan, made->frame)) {
        return Ref<NetwPredictCommandFrame>();
    }
    return made;
}

void NetwPredictCommandFrame::set_epoch(int p_epoch) {
    frame.epoch = uint8_t(p_epoch & 0xff);
}

int NetwPredictCommandFrame::epoch() const {
    return int(frame.epoch);
}

void NetwPredictCommandFrame::set_ack_of_acks(int64_t p_transition) {
    frame.ack_of_acks = p_transition;
}

int64_t NetwPredictCommandFrame::ack_of_acks() const {
    return frame.ack_of_acks;
}

bool NetwPredictCommandFrame::append_transition(
    int64_t p_index,
    int64_t p_label,
    bool p_fresh
) {
    if (frame.transitions.size() >= predict::COMMAND_TRANSITION_MAX) {
        return false;
    }
    // The window is one contiguous run, so a gap would make the base and the
    // count disagree about which transition every later row names.
    if (!frame.transitions.is_empty()
        && p_index != frame.transitions[frame.transitions.size() - 1].index + 1) {
        return false;
    }
    predict::TransitionWire row;
    row.index = p_index;
    row.label = p_label;
    row.fresh = p_fresh;
    frame.transitions.push_back(row);
    return true;
}

bool NetwPredictCommandFrame::append_payload(const Array &p_values) {
    wire::CodeRow row;
    if (!wire::encode_scalar_row(schema, p_values, row)) {
        return false;
    }
    frame.payloads.push_back(row);
    return true;
}

bool NetwPredictCommandFrame::append_evidence(
    int p_evidence_mask,
    int p_pre_fp,
    int p_post_fp,
    int p_e_digest,
    int p_topo_fp,
    int p_witness_fp,
    const PackedInt32Array &p_pre_family_fps,
    const PackedInt32Array &p_post_family_fps,
    int p_raw_fp
) {
    if (p_pre_family_fps.size() != FAMILY_COUNT
        || p_post_family_fps.size() != FAMILY_COUNT) {
        return false;
    }
    predict::CommandEvidenceWire record;
    record.evidence_mask = uint8_t(p_evidence_mask & 0xff);
    record.pre_fp = int32_t(p_pre_fp);
    record.post_fp = int32_t(p_post_fp);
    record.e_digest = int32_t(p_e_digest);
    record.topo_fp = int32_t(p_topo_fp);
    record.witness_fp = int32_t(p_witness_fp);
    record.pre_pose_fp = p_pre_family_fps[0];
    record.pre_momentum_fp = p_pre_family_fps[1];
    record.pre_controller_fp = p_pre_family_fps[2];
    record.post_pose_fp = p_post_family_fps[0];
    record.post_momentum_fp = p_post_family_fps[1];
    record.post_controller_fp = p_post_family_fps[2];
    record.raw_fp = int32_t(p_raw_fp);
    frame.evidence.push_back(record);
    return true;
}

int NetwPredictCommandFrame::transition_count() const {
    return int(frame.transitions.size());
}

PackedInt64Array NetwPredictCommandFrame::indices() const {
    PackedInt64Array out;
    for (uint32_t at = 0; at < frame.transitions.size(); ++at) {
        out.push_back(frame.transitions[at].index);
    }
    return out;
}

PackedInt64Array NetwPredictCommandFrame::labels() const {
    PackedInt64Array out;
    for (uint32_t at = 0; at < frame.transitions.size(); ++at) {
        out.push_back(frame.transitions[at].label);
    }
    return out;
}

PackedByteArray NetwPredictCommandFrame::fresh_flags() const {
    PackedByteArray out;
    for (uint32_t at = 0; at < frame.transitions.size(); ++at) {
        out.push_back(frame.transitions[at].fresh ? 1 : 0);
    }
    return out;
}

int NetwPredictCommandFrame::payload_count() const {
    return int(frame.payloads.size());
}

Array NetwPredictCommandFrame::payload_at(int p_index) const {
    Array out;
    if (p_index < 0 || p_index >= int(frame.payloads.size())) {
        return out;
    }
    wire::decode_scalar_row(schema, frame.payloads[uint32_t(p_index)], out);
    return out;
}

int NetwPredictCommandFrame::evidence_count() const {
    return int(frame.evidence.size());
}

PackedByteArray NetwPredictCommandFrame::evidence_masks() const {
    PackedByteArray out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].evidence_mask);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::pre_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].pre_fp);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::post_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].post_fp);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::e_digests() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].e_digest);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::topo_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].topo_fp);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::witness_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].witness_fp);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::raw_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        out.push_back(frame.evidence[at].raw_fp);
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::pre_family_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        const predict::CommandEvidenceWire &record = frame.evidence[at];
        append_families(
            out,
            record.pre_pose_fp,
            record.pre_momentum_fp,
            record.pre_controller_fp
        );
    }
    return out;
}

PackedInt32Array NetwPredictCommandFrame::post_family_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.evidence.size(); ++at) {
        const predict::CommandEvidenceWire &record = frame.evidence[at];
        append_families(
            out,
            record.post_pose_fp,
            record.post_momentum_fp,
            record.post_controller_fp
        );
    }
    return out;
}

PackedByteArray NetwPredictCommandFrame::to_bytes() const {
    return predict::encode_command(frame, plan);
}

void NetwPredictCommandFrame::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictCommandFrame",
        D_METHOD("create", "schema"),
        &NetwPredictCommandFrame::create
    );
    ClassDB::bind_static_method(
        "NetwPredictCommandFrame",
        D_METHOD("from_bytes", "schema", "bytes"),
        &NetwPredictCommandFrame::from_bytes
    );
    ClassDB::bind_method(
        D_METHOD("set_epoch", "epoch"),
        &NetwPredictCommandFrame::set_epoch
    );
    ClassDB::bind_method(D_METHOD("epoch"), &NetwPredictCommandFrame::epoch);
    ClassDB::bind_method(
        D_METHOD("set_ack_of_acks", "transition"),
        &NetwPredictCommandFrame::set_ack_of_acks
    );
    ClassDB::bind_method(
        D_METHOD("ack_of_acks"),
        &NetwPredictCommandFrame::ack_of_acks
    );
    ClassDB::bind_method(
        D_METHOD("append_transition", "index", "label", "fresh"),
        &NetwPredictCommandFrame::append_transition
    );
    ClassDB::bind_method(
        D_METHOD("append_payload", "values"),
        &NetwPredictCommandFrame::append_payload
    );
    ClassDB::bind_method(
        D_METHOD(
            "append_evidence",
            "evidence_mask",
            "pre_fp",
            "post_fp",
            "e_digest",
            "topo_fp",
            "witness_fp",
            "pre_family_fps",
            "post_family_fps",
            "raw_fp"
        ),
        &NetwPredictCommandFrame::append_evidence
    );
    ClassDB::bind_method(
        D_METHOD("transition_count"),
        &NetwPredictCommandFrame::transition_count
    );
    ClassDB::bind_method(
        D_METHOD("indices"),
        &NetwPredictCommandFrame::indices
    );
    ClassDB::bind_method(D_METHOD("labels"), &NetwPredictCommandFrame::labels);
    ClassDB::bind_method(
        D_METHOD("fresh_flags"),
        &NetwPredictCommandFrame::fresh_flags
    );
    ClassDB::bind_method(
        D_METHOD("payload_count"),
        &NetwPredictCommandFrame::payload_count
    );
    ClassDB::bind_method(
        D_METHOD("payload_at", "index"),
        &NetwPredictCommandFrame::payload_at
    );
    ClassDB::bind_method(
        D_METHOD("evidence_count"),
        &NetwPredictCommandFrame::evidence_count
    );
    ClassDB::bind_method(
        D_METHOD("evidence_masks"),
        &NetwPredictCommandFrame::evidence_masks
    );
    ClassDB::bind_method(
        D_METHOD("pre_fps"),
        &NetwPredictCommandFrame::pre_fps
    );
    ClassDB::bind_method(
        D_METHOD("post_fps"),
        &NetwPredictCommandFrame::post_fps
    );
    ClassDB::bind_method(
        D_METHOD("e_digests"),
        &NetwPredictCommandFrame::e_digests
    );
    ClassDB::bind_method(
        D_METHOD("topo_fps"),
        &NetwPredictCommandFrame::topo_fps
    );
    ClassDB::bind_method(
        D_METHOD("witness_fps"),
        &NetwPredictCommandFrame::witness_fps
    );
    ClassDB::bind_method(
        D_METHOD("raw_fps"),
        &NetwPredictCommandFrame::raw_fps
    );
    ClassDB::bind_method(
        D_METHOD("pre_family_fps"),
        &NetwPredictCommandFrame::pre_family_fps
    );
    ClassDB::bind_method(
        D_METHOD("post_family_fps"),
        &NetwPredictCommandFrame::post_family_fps
    );
    ClassDB::bind_method(
        D_METHOD("to_bytes"),
        &NetwPredictCommandFrame::to_bytes
    );
}

Ref<NetwPredictAckFrame> NetwPredictAckFrame::create() {
    Ref<NetwPredictAckFrame> made;
    made.instantiate();
    return made;
}

Ref<NetwPredictAckFrame> NetwPredictAckFrame::from_bytes(
    const PackedByteArray &p_bytes
) {
    Ref<NetwPredictAckFrame> made = create();
    if (!predict::decode_ack(p_bytes, made->frame)) {
        return Ref<NetwPredictAckFrame>();
    }
    return made;
}

void NetwPredictAckFrame::set_epoch(int p_epoch) {
    frame.epoch = uint8_t(p_epoch & 0xff);
}

int NetwPredictAckFrame::epoch() const {
    return int(frame.epoch);
}

void NetwPredictAckFrame::set_base(int64_t p_transition) {
    frame.base = p_transition < 0 ? 0 : uint64_t(p_transition);
}

int64_t NetwPredictAckFrame::base() const {
    return int64_t(frame.base);
}

bool NetwPredictAckFrame::append_record(
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
) {
    if (p_pre_family_fps.size() != FAMILY_COUNT
        || p_post_family_fps.size() != FAMILY_COUNT) {
        return false;
    }
    predict::AckEvidenceWire record;
    record.evidence_mask = uint8_t(p_evidence_mask & 0xff);
    record.pre_fp = int32_t(p_pre_fp);
    record.c_hash = int32_t(p_c_hash);
    record.e_digest = int32_t(p_e_digest);
    record.post_fp = int32_t(p_post_fp);
    record.topo_fp = int32_t(p_topo_fp);
    record.witness_fp = int32_t(p_witness_fp);
    record.pre_pose_fp = p_pre_family_fps[0];
    record.pre_momentum_fp = p_pre_family_fps[1];
    record.pre_controller_fp = p_pre_family_fps[2];
    record.post_pose_fp = p_post_family_fps[0];
    record.post_momentum_fp = p_post_family_fps[1];
    record.post_controller_fp = p_post_family_fps[2];
    record.raw_fp = int32_t(p_raw_fp);
    record.flags = uint8_t(
        (p_flags & ~predict::ACK_WITNESS_MASK)
        | ((p_witness_class << predict::ACK_WITNESS_SHIFT)
           & predict::ACK_WITNESS_MASK)
    );
    frame.records.push_back(record);
    return true;
}

int NetwPredictAckFrame::record_count() const {
    return int(frame.records.size());
}

PackedByteArray NetwPredictAckFrame::evidence_masks() const {
    PackedByteArray out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].evidence_mask);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::pre_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].pre_fp);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::c_hashes() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].c_hash);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::e_digests() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].e_digest);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::post_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].post_fp);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::topo_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].topo_fp);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::witness_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].witness_fp);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::raw_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].raw_fp);
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::pre_family_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        const predict::AckEvidenceWire &record = frame.records[at];
        append_families(
            out,
            record.pre_pose_fp,
            record.pre_momentum_fp,
            record.pre_controller_fp
        );
    }
    return out;
}

PackedInt32Array NetwPredictAckFrame::post_family_fps() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        const predict::AckEvidenceWire &record = frame.records[at];
        append_families(
            out,
            record.post_pose_fp,
            record.post_momentum_fp,
            record.post_controller_fp
        );
    }
    return out;
}

PackedByteArray NetwPredictAckFrame::flags() const {
    PackedByteArray out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(frame.records[at].flags & ~predict::ACK_WITNESS_MASK);
    }
    return out;
}

PackedByteArray NetwPredictAckFrame::witness_class_bits() const {
    PackedByteArray out;
    for (uint32_t at = 0; at < frame.records.size(); ++at) {
        out.push_back(
            (frame.records[at].flags & predict::ACK_WITNESS_MASK)
            >> predict::ACK_WITNESS_SHIFT
        );
    }
    return out;
}

PackedByteArray NetwPredictAckFrame::to_bytes() const {
    return predict::encode_ack(frame);
}

void NetwPredictAckFrame::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictAckFrame",
        D_METHOD("create"),
        &NetwPredictAckFrame::create
    );
    ClassDB::bind_static_method(
        "NetwPredictAckFrame",
        D_METHOD("from_bytes", "bytes"),
        &NetwPredictAckFrame::from_bytes
    );
    ClassDB::bind_method(
        D_METHOD("set_epoch", "epoch"),
        &NetwPredictAckFrame::set_epoch
    );
    ClassDB::bind_method(D_METHOD("epoch"), &NetwPredictAckFrame::epoch);
    ClassDB::bind_method(
        D_METHOD("set_base", "transition"),
        &NetwPredictAckFrame::set_base
    );
    ClassDB::bind_method(D_METHOD("base"), &NetwPredictAckFrame::base);
    ClassDB::bind_method(
        D_METHOD(
            "append_record",
            "evidence_mask",
            "pre_fp",
            "c_hash",
            "e_digest",
            "post_fp",
            "topo_fp",
            "witness_fp",
            "pre_family_fps",
            "post_family_fps",
            "raw_fp",
            "flags",
            "witness_class"
        ),
        &NetwPredictAckFrame::append_record
    );
    ClassDB::bind_method(
        D_METHOD("record_count"),
        &NetwPredictAckFrame::record_count
    );
    ClassDB::bind_method(
        D_METHOD("evidence_masks"),
        &NetwPredictAckFrame::evidence_masks
    );
    ClassDB::bind_method(D_METHOD("pre_fps"), &NetwPredictAckFrame::pre_fps);
    ClassDB::bind_method(D_METHOD("c_hashes"), &NetwPredictAckFrame::c_hashes);
    ClassDB::bind_method(
        D_METHOD("e_digests"),
        &NetwPredictAckFrame::e_digests
    );
    ClassDB::bind_method(D_METHOD("post_fps"), &NetwPredictAckFrame::post_fps);
    ClassDB::bind_method(D_METHOD("topo_fps"), &NetwPredictAckFrame::topo_fps);
    ClassDB::bind_method(
        D_METHOD("witness_fps"),
        &NetwPredictAckFrame::witness_fps
    );
    ClassDB::bind_method(D_METHOD("raw_fps"), &NetwPredictAckFrame::raw_fps);
    ClassDB::bind_method(
        D_METHOD("pre_family_fps"),
        &NetwPredictAckFrame::pre_family_fps
    );
    ClassDB::bind_method(
        D_METHOD("post_family_fps"),
        &NetwPredictAckFrame::post_family_fps
    );
    ClassDB::bind_method(D_METHOD("flags"), &NetwPredictAckFrame::flags);
    ClassDB::bind_method(
        D_METHOD("witness_class_bits"),
        &NetwPredictAckFrame::witness_class_bits
    );
    ClassDB::bind_method(D_METHOD("to_bytes"), &NetwPredictAckFrame::to_bytes);

    BIND_ENUM_CONSTANT(FLAG_SUBSTITUTED);
    BIND_ENUM_CONSTANT(FLAG_SUPERSEDED);
}

} // namespace netw

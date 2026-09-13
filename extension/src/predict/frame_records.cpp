#include "netw/predict/frame_records.hpp"

#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::predict {

namespace {

const int FAMILY_COUNT = 3;

} // namespace

bool CommandFrameRecord::open(
    const SchemaRecord &p_schema,
    CommandFrameRecord &r_record
) {
    const wire::WirePlan plan = wire::WirePlan::compile(p_schema);
    if (!plan.valid()) {
        return false;
    }
    r_record.frame = CommandFrame();
    r_record.schema = p_schema;
    r_record.plan = plan;
    return true;
}

bool CommandFrameRecord::decode(
    const SchemaRecord &p_schema,
    const PackedByteArray &p_bytes,
    CommandFrameRecord &r_record
) {
    CommandFrameRecord staged;
    if (!open(p_schema, staged)
        || !decode_command(p_bytes, staged.plan, staged.frame)) {
        return false;
    }
    r_record = staged;
    return true;
}

void CommandFrameRecord::set_epoch(int p_epoch) {
    frame.epoch = uint8_t(p_epoch & 0xff);
}

int CommandFrameRecord::epoch() const {
    return int(frame.epoch);
}

void CommandFrameRecord::set_ack_of_acks(int64_t p_transition) {
    frame.ack_of_acks = p_transition;
}

int64_t CommandFrameRecord::ack_of_acks() const {
    return frame.ack_of_acks;
}

bool CommandFrameRecord::append_transition(
    int64_t p_index,
    int64_t p_label,
    bool p_fresh
) {
    if (frame.transitions.size() >= COMMAND_TRANSITION_MAX) {
        return false;
    }

    if (!frame.transitions.is_empty()
        && p_index
            != frame.transitions[frame.transitions.size() - 1].index + 1) {
        return false;
    }
    TransitionWire transition;
    transition.index = p_index;
    transition.label = p_label;
    transition.fresh = p_fresh;
    frame.transitions.push_back(transition);
    return true;
}

bool CommandFrameRecord::append_payload(const Array &p_values) {
    wire::CodeRow row;
    if (!wire::encode_scalar_row(schema, p_values, row)) {
        return false;
    }
    frame.payloads.push_back(row);
    return true;
}

bool CommandFrameRecord::append_evidence(
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
    CommandEvidenceWire record;
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

Array CommandFrameRecord::payload_at(int p_index) const {
    Array out;
    if (p_index < 0 || p_index >= int(frame.payloads.size())) {
        return out;
    }
    wire::decode_scalar_row(schema, frame.payloads[uint32_t(p_index)], out);
    return out;
}

PackedByteArray CommandFrameRecord::to_bytes() const {
    return encode_command(frame, plan);
}

} // namespace netw::predict

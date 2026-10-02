#include "netw/predict/frame_records.hpp"

#include <algorithm>

#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::predict {

namespace {

const int FAMILY_COUNT = 3;

void blank(StateRow &r_row) {
    for (uint32_t field = 0; field < r_row.values.size(); ++field) {
        r_row.values[field] = Variant();
        r_row.present[field] = 0;
    }
}

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

bool CommandFrameRecord::append_values(const LocalVector<Variant> &p_values) {
    if (!wire::is_scalar_schema(schema, plan)
        || int(p_values.size()) != schema.column_count()) {
        return false;
    }
    wire::CodeRow row = wire::CodeRow::for_plan(plan);
    for (int at = 0; at < schema.column_count(); ++at) {
        if (!wire::encode_scalar_column(
                *schema.at(at),
                plan.column(uint32_t(at)),
                uint32_t(at),
                p_values[uint32_t(at)],
                row
            )) {
            return false;
        }
    }
    frame.payloads.push_back(row);
    return true;
}

bool CommandFrameRecord::append_codes(const uint64_t *p_words) {
    wire::CodeRow row = wire::CodeRow::for_plan(plan);
    if (!row.assign(plan, p_words, nullptr)) {
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

bool CommandFrameRecord::read_payload(
    int p_index,
    const LocalVector<int> &p_fields,
    StateRow &r_row
) const {
    blank(r_row);
    if (p_index < 0 || p_index >= int(frame.payloads.size())
        || !wire::is_scalar_schema(schema, plan)) {
        return false;
    }
    const wire::CodeRow &payload = frame.payloads[uint32_t(p_index)];
    if (!payload.valid_for(plan)) {
        return false;
    }
    const int columns
        = std::min(schema.column_count(), int(p_fields.size()));
    PackedInt64Array codes;
    for (int at = 0; at < schema.column_count(); ++at) {
        Variant value;
        if (!wire::decode_scalar_column(
                *schema.at(at),
                plan.column(uint32_t(at)),
                uint32_t(at),
                payload,
                codes,
                value
            )) {
            blank(r_row);
            return false;
        }
        const int field = at < columns ? p_fields[uint32_t(at)] : -1;
        if (field >= 0 && field < int(r_row.values.size())) {
            r_row.set(field, value);
        }
    }
    return true;
}

PackedByteArray CommandFrameRecord::to_bytes() const {
    return encode_command(frame, plan);
}

} // namespace netw::predict

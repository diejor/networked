#include "netw/predict/frames.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/predict/journal.hpp"
#include "netw/profile.hpp"
#include "netw/wire/stream.hpp"

using namespace godot;

namespace netw::predict {

namespace {

using wire::MeasureStream;
using wire::ReadStream;
using wire::WriteStream;

struct CommandHeader {
    int64_t ack_of_acks = -1;
    uint8_t epoch = 0;
    uint64_t base = 0;
    uint8_t count = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&CommandHeader::ack_of_acks>(
            "ack_of_acks",
            netw::wire::svarint(5)
        ),
        netw::wire::field<&CommandHeader::epoch>("epoch", netw::wire::bits(8)),
        netw::wire::field<&CommandHeader::base>("base", netw::wire::varuint(5)),
        netw::wire::field<&CommandHeader::count>("count", netw::wire::bits(8))
    );
};

struct AckHeader {
    uint8_t epoch = 0;
    uint64_t base = 0;
    uint8_t count = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&AckHeader::epoch>("epoch", netw::wire::bits(8)),
        netw::wire::field<&AckHeader::base>("base", netw::wire::varuint(5)),
        netw::wire::field<&AckHeader::count>("count", netw::wire::bits(8))
    );
};

constexpr auto command_evidence_prefix = netw::wire::describe(
    netw::wire::field<&CommandEvidenceWire::evidence_mask>(
        "evidence_mask",
        netw::wire::bits(8)
    ),
    netw::wire::field<&CommandEvidenceWire::pre_fp>(
        "pre_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::post_fp>(
        "post_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::e_digest>(
        "e_digest",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::topo_fp>(
        "topo_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::witness_fp>(
        "witness_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::pre_pose_fp>(
        "pre_pose_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::pre_momentum_fp>(
        "pre_momentum_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::pre_controller_fp>(
        "pre_controller_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::post_pose_fp>(
        "post_pose_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::post_momentum_fp>(
        "post_momentum_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&CommandEvidenceWire::post_controller_fp>(
        "post_controller_fp",
        netw::wire::bits(32)
    )
);

constexpr auto ack_evidence_prefix = netw::wire::describe(
    netw::wire::field<&AckEvidenceWire::evidence_mask>(
        "evidence_mask",
        netw::wire::bits(8)
    ),
    netw::wire::field<&AckEvidenceWire::pre_fp>("pre_fp", netw::wire::bits(32)),
    netw::wire::field<&AckEvidenceWire::c_hash>("c_hash", netw::wire::bits(32)),
    netw::wire::field<&AckEvidenceWire::e_digest>(
        "e_digest",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::post_fp>(
        "post_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::topo_fp>(
        "topo_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::witness_fp>(
        "witness_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::pre_pose_fp>(
        "pre_pose_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::pre_momentum_fp>(
        "pre_momentum_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::pre_controller_fp>(
        "pre_controller_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::post_pose_fp>(
        "post_pose_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::post_momentum_fp>(
        "post_momentum_fp",
        netw::wire::bits(32)
    ),
    netw::wire::field<&AckEvidenceWire::post_controller_fp>(
        "post_controller_fp",
        netw::wire::bits(32)
    )
);

constexpr auto command_raw = netw::wire::describe(
    netw::wire::field<&CommandEvidenceWire::raw_fp>(
        "raw_fp",
        netw::wire::bits(32)
    )
);

constexpr auto ack_raw = netw::wire::describe(
    netw::wire::field<&AckEvidenceWire::raw_fp>("raw_fp", netw::wire::bits(32))
);

constexpr auto ack_suffix = netw::wire::describe(
    netw::wire::field<&AckEvidenceWire::flags>("flags", netw::wire::bits(8))
);

uint64_t zigzag(int64_t p_value) {
    return (uint64_t(p_value) << 1) ^ uint64_t(p_value >> 63);
}

int64_t unzigzag(uint64_t p_value) {
    return int64_t((p_value >> 1) ^ uint64_t(-int64_t(p_value & 1)));
}

template <class Stream>
bool serialize_row(
    Stream &p_stream,
    const wire::WirePlan &p_plan,
    wire::CodeRow &p_row
) {
    for (uint32_t column = 0; column < p_plan.column_count(); ++column) {
        const wire::ColumnPlan &slot = p_plan.column(column);
        for (int element = 0; element < slot.stride; ++element) {
            const int64_t base = slot.offset + int64_t(element) * slot.width;
            for (int consumed = 0; consumed < slot.width;) {
                const int width
                    = slot.width - consumed < 64 ? slot.width - consumed : 64;
                uint64_t code = Stream::is_reading
                    ? 0
                    : p_row.read_bits(base + consumed, width);
                if (!p_stream.bits(code, width)) {
                    return false;
                }
                if constexpr (Stream::is_reading) {
                    if (!p_row.write_bits(base + consumed, width, code)) {
                        return false;
                    }
                }
                consumed += width;
            }
        }
    }
    return true;
}

template <class Stream>
bool serialize_command_evidence(
    Stream &p_stream,
    CommandEvidenceWire &p_record
) {
    if (!command_evidence_prefix.run(p_stream, p_record)) {
        return false;
    }
    if ((p_record.evidence_mask & EVIDENCE_RAW) != 0
        && !command_raw.run(p_stream, p_record)) {
        return false;
    }
    return true;
}

template <class Stream>
bool serialize_ack_evidence(Stream &p_stream, AckEvidenceWire &p_record) {
    if (!ack_evidence_prefix.run(p_stream, p_record)) {
        return false;
    }
    if ((p_record.evidence_mask & EVIDENCE_RAW) != 0
        && !ack_raw.run(p_stream, p_record)) {
        return false;
    }
    return ack_suffix.run(p_stream, p_record);
}

bool valid_command_shape(
    const CommandFrame &p_frame,
    const wire::WirePlan &p_plan
) {
    if (!p_plan.valid() || p_frame.transitions.size() > COMMAND_TRANSITION_MAX
        || p_frame.evidence.size() > p_frame.transitions.size()) {
        return false;
    }
    uint32_t fresh = 0;
    for (uint32_t at = 0; at < p_frame.transitions.size(); ++at) {
        const TransitionWire &transition = p_frame.transitions[at];
        if (at > 0
            && transition.index != p_frame.transitions[at - 1].index + 1) {
            return false;
        }
        fresh += transition.fresh ? 1U : 0U;
    }
    if (fresh != p_frame.payloads.size()) {
        return false;
    }
    for (uint32_t at = 0; at < p_frame.payloads.size(); ++at) {
        if (!p_frame.payloads[at].valid_for(p_plan)) {
            return false;
        }
    }
    return true;
}

template <class Stream>
bool serialize_command(
    Stream &p_stream,
    CommandFrame &p_frame,
    const wire::WirePlan &p_plan
) {
    CommandHeader header;
    if constexpr (!Stream::is_reading) {
        header.ack_of_acks = p_frame.ack_of_acks;
        header.epoch = p_frame.epoch;
        header.base = p_frame.transitions.is_empty()
            ? 0
            : uint64_t(p_frame.transitions[0].index);
        header.count = uint8_t(p_frame.transitions.size());
    }
    if (!CommandHeader::wire.run(p_stream, header)) {
        return false;
    }
    if constexpr (Stream::is_reading) {
        p_frame.ack_of_acks = header.ack_of_acks;
        p_frame.epoch = header.epoch;
    }

    int64_t previous_label = 0;
    for (uint32_t at = 0; at < header.count; ++at) {
        TransitionWire transition;
        uint64_t tagged = 0;
        if constexpr (!Stream::is_reading) {
            transition = p_frame.transitions[at];
            tagged = zigzag(transition.label - previous_label) << 1;
            tagged |= transition.fresh ? 1U : 0U;
        }
        if (!p_stream.varuint(tagged, 5)) {
            return false;
        }
        if constexpr (Stream::is_reading) {
            transition.index = int64_t(header.base) + int64_t(at);
            transition.label = previous_label + unzigzag(tagged >> 1);
            transition.fresh = (tagged & 1U) != 0;
            p_frame.transitions.push_back(transition);
        }
        previous_label = transition.label;
    }

    uint32_t payload_at = 0;
    for (uint32_t at = 0; at < header.count; ++at) {
        const TransitionWire &transition = p_frame.transitions[at];
        if (!transition.fresh) {
            continue;
        }
        wire::CodeRow row = Stream::is_reading ? wire::CodeRow::for_plan(p_plan)
                                               : p_frame.payloads[payload_at];
        if (!serialize_row(p_stream, p_plan, row)) {
            return false;
        }
        if constexpr (Stream::is_reading) {
            p_frame.payloads.push_back(row);
        }
        payload_at += 1;
    }
    if (!p_stream.align_verify()) {
        return false;
    }

    uint64_t evidence_count = Stream::is_reading ? 0 : p_frame.evidence.size();
    if (!p_stream.bits(evidence_count, 8) || evidence_count > header.count) {
        return false;
    }
    for (uint32_t at = 0; at < evidence_count; ++at) {
        CommandEvidenceWire record;
        if constexpr (!Stream::is_reading) {
            record = p_frame.evidence[at];
        }
        if (!serialize_command_evidence(p_stream, record)) {
            return false;
        }
        if constexpr (Stream::is_reading) {
            p_frame.evidence.push_back(record);
        }
    }
    return p_stream.align_verify();
}

template <class Stream>
bool serialize_ack(Stream &p_stream, AckFrame &p_frame) {
    AckHeader header;
    if constexpr (!Stream::is_reading) {
        header.epoch = p_frame.epoch;
        header.base = p_frame.base;
        header.count = uint8_t(
            p_frame.records.size() < ACK_RECORD_MAX ? p_frame.records.size()
                                                    : ACK_RECORD_MAX
        );
    }
    if (!AckHeader::wire.run(p_stream, header)) {
        return false;
    }
    if constexpr (Stream::is_reading) {
        if (header.count > ACK_RECORD_MAX) {
            return false;
        }
        p_frame.epoch = header.epoch;
        p_frame.base = header.base;
    }
    for (uint32_t at = 0; at < header.count; ++at) {
        AckEvidenceWire record;
        if constexpr (!Stream::is_reading) {
            record = p_frame.records[at];
        }
        if (!serialize_ack_evidence(p_stream, record)) {
            return false;
        }
        if constexpr (Stream::is_reading) {
            p_frame.records.push_back(record);
        }
    }
    return p_stream.align_verify();
}

bool reader_consumed(ReadStream &p_reader) {
    return p_reader.ok() && p_reader.bits_remaining() == 0;
}

} // namespace

Dictionary spec_records() {
    Dictionary out;
    out["CommandHeader"] = CommandHeader::wire.spec_dump();
    out["AckHeader"] = AckHeader::wire.spec_dump();
    out["CommandEvidence"] = command_evidence_prefix.spec_dump();
    out["AckEvidence"] = ack_evidence_prefix.spec_dump();
    return out;
}

PackedByteArray encode_command(
    const CommandFrame &p_frame,
    const wire::WirePlan &p_input_plan
) {
    NETW_ZONE_NC("Predict wire command encode", colors::PREDICTION);
    NETW_ERR_COND_V(
        !valid_command_shape(p_frame, p_input_plan),
        PackedByteArray(),
        sys::PREDICTION,
        "Command frame shape does not match its input plan."
    );
    CommandFrame staged = p_frame;
    WriteStream writer;
    NETW_ERR_COND_V(
        !serialize_command(writer, staged, p_input_plan),
        PackedByteArray(),
        sys::PREDICTION,
        "Command frame exceeds its declared wire bounds."
    );
    NETW_TRACE(
        sys::PREDICTION,
        "command transitions=%d payloads=%d bytes=%d",
        int(p_frame.transitions.size()),
        int(p_frame.payloads.size()),
        writer.to_bytes().size()
    );
    return writer.to_bytes();
}

bool decode_command(
    const PackedByteArray &p_bytes,
    const wire::WirePlan &p_input_plan,
    CommandFrame &r_frame
) {
    NETW_ZONE_NC("Predict wire command decode", colors::PREDICTION);
    if (p_bytes.is_empty() || !p_input_plan.valid()) {
        return false;
    }
    CommandFrame staged;
    ReadStream reader(p_bytes);
    if (!serialize_command(reader, staged, p_input_plan)
        || !reader_consumed(reader)) {
        return false;
    }
    r_frame = staged;
    return true;
}

PackedByteArray encode_ack(const AckFrame &p_frame) {
    NETW_ZONE_NC("Predict wire ack encode", colors::PREDICTION);
    AckFrame staged = p_frame;
    WriteStream writer;
    NETW_ERR_COND_V(
        !serialize_ack(writer, staged),
        PackedByteArray(),
        sys::PREDICTION,
        "Acknowledgement frame exceeds its declared wire bounds."
    );
    NETW_ASSERT(
        writer.to_bytes().size() <= ACK_FRAME_BYTES_MAX,
        sys::PREDICTION,
        "Acknowledgement prefix exceeded its MTU budget."
    );
    NETW_TRACE(
        sys::PREDICTION,
        "ack records=%d bytes=%d",
        int(staged.records.size()),
        writer.to_bytes().size()
    );
    return writer.to_bytes();
}

bool decode_ack(const PackedByteArray &p_bytes, AckFrame &r_frame) {
    NETW_ZONE_NC("Predict wire ack decode", colors::PREDICTION);
    if (p_bytes.is_empty()) {
        return false;
    }
    AckFrame staged;
    ReadStream reader(p_bytes);
    if (!serialize_ack(reader, staged) || !reader_consumed(reader)) {
        return false;
    }
    r_frame = staged;
    return true;
}

PackedByteArray encode_relay_request(bool p_subscribed) {
    NETW_ZONE_NC("Predict wire relay request encode", colors::PREDICTION);
    WriteStream writer;
    bool staged = p_subscribed;
    if (!writer.bool1(staged) || !writer.align_verify()) {
        return PackedByteArray();
    }
    return writer.to_bytes();
}

bool decode_relay_request(const PackedByteArray &p_bytes, bool &r_subscribed) {
    NETW_ZONE_NC("Predict wire relay request decode", colors::PREDICTION);
    if (p_bytes.is_empty()) {
        return false;
    }
    ReadStream reader(p_bytes);
    bool staged = false;
    if (!reader.bool1(staged) || !reader.align_verify()
        || !reader_consumed(reader)) {
        return false;
    }
    r_subscribed = staged;
    return true;
}

bool is_lane(uint8_t p_channel) {
    return p_channel == CHANNEL_COMMAND || p_channel == CHANNEL_ACK
        || p_channel == CHANNEL_RELAY || p_channel == CHANNEL_RELAY_REQUEST;
}

godot::Error admit_frame(
    const wire::WireRegistry &p_registry,
    uint8_t p_channel,
    const FrameOrigin &p_origin,
    bool p_payload_empty,
    godot::Error p_route_verdict
) {
    NETW_ZONE_NC("Predict frame admission", colors::PREDICTION);
    if (p_route_verdict != godot::Error::OK) {
        return p_route_verdict;
    }
    if (p_payload_empty) {
        return godot::Error::ERR_INVALID_DATA;
    }
    const wire::ChannelDecl *decl
        = is_lane(p_channel) ? p_registry.find_channel(p_channel) : nullptr;
    if (decl == nullptr) {
        return godot::Error::ERR_INVALID_DATA;
    }
    switch (decl->direction) {
        case wire::Direction::OWNER_TO_SERVER:
            if (!p_origin.receiver_is_server
                || p_origin.sender != p_origin.controller) {
                return godot::Error::ERR_UNAUTHORIZED;
            }
            break;
        case wire::Direction::CLIENT_TO_SERVER:
            if (!p_origin.receiver_is_server) {
                return godot::Error::ERR_UNAUTHORIZED;
            }
            break;
        case wire::Direction::SERVER_TO_CLIENT:
        case wire::Direction::SERVER_TO_OWNER:
            if (p_origin.sender != SERVER_PEER) {
                return godot::Error::ERR_UNAUTHORIZED;
            }
            break;
        case wire::Direction::EITHER:
            break;
    }
    return godot::Error::OK;
}

} // namespace netw::predict

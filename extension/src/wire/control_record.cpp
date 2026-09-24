#include "netw/wire/control_record.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/stream.hpp"

namespace netw::wire {

using namespace godot;

namespace {

bool family_is_declared(uint64_t p_family) {
    return p_family <= uint64_t(STREAM_FAMILY_CEILING);
}

PackedByteArray finish(WriteStream &p_stream) {
    if (!p_stream.align_verify() || !p_stream.ok()) {
        return PackedByteArray();
    }
    return p_stream.to_bytes();
}

PackedByteArray write_open(const ControlRecord &p_record) {
    ControlOpen staged;
    staged.request = p_record.request;
    staged.route = uint64_t(p_record.route);
    staged.ordinal = uint64_t(p_record.ordinal);
    staged.family = uint64_t(p_record.family);
    staged.epoch = p_record.epoch;
    staged.tenure = p_record.tenure;
    staged.anchor = p_record.anchor;
    staged.schema = uint64_t(p_record.schema);
    WriteStream stream;
    if (!ControlOpen::wire.run(stream, staged)) {
        return PackedByteArray();
    }
    return finish(stream);
}

PackedByteArray write_accept(const ControlRecord &p_record) {
    ControlAcceptHead head;
    head.count = int64_t(p_record.receipts.size());
    WriteStream stream;
    if (!ControlAcceptHead::wire.run(stream, head)) {
        return PackedByteArray();
    }
    for (uint32_t at = 0; at < p_record.receipts.size(); ++at) {
        ControlAcceptEntry entry = p_record.receipts[at];
        if (!ControlAcceptEntry::wire.run(stream, entry)) {
            return PackedByteArray();
        }
    }
    return finish(stream);
}

bool read_open(ReadStream &p_stream, ControlRecord &r_record) {
    ControlOpen staged;
    if (!ControlOpen::wire.run(p_stream, staged)
        || !family_is_declared(staged.family) || staged.ordinal > 255) {
        return false;
    }
    r_record.request = staged.request;
    r_record.route = int64_t(staged.route);
    r_record.ordinal = uint8_t(staged.ordinal);
    r_record.family = StreamFamily(uint8_t(staged.family));
    r_record.epoch = staged.epoch;
    r_record.tenure = staged.tenure;
    r_record.anchor = staged.anchor;
    r_record.schema = uint32_t(staged.schema);
    return true;
}

bool read_accept(ReadStream &p_stream, ControlRecord &r_record) {
    ControlAcceptHead head;
    if (!ControlAcceptHead::wire.run(p_stream, head)) {
        return false;
    }
    godot::LocalVector<ControlAcceptEntry> staged;
    staged.reserve(uint32_t(head.count));
    for (int64_t at = 0; at < head.count; ++at) {
        ControlAcceptEntry entry;
        if (!ControlAcceptEntry::wire.run(p_stream, entry)
            || entry.revision == 0 || entry.token == 0) {
            return false;
        }
        staged.push_back(entry);
    }
    r_record.receipts = staged;
    return true;
}

} // namespace

PackedByteArray write_control_record(const ControlRecord &p_record) {
    NETW_ZONE_NC("Control record write", colors::WIRE);
    switch (p_record.tag) {
        case ControlTag::OPEN:
            return write_open(p_record);
        case ControlTag::READY: {
            ControlReady staged;
            staged.request = p_record.request;
            staged.token = p_record.token;
            WriteStream stream;
            if (!ControlReady::wire.run(stream, staged)) {
                return PackedByteArray();
            }
            return finish(stream);
        }
        case ControlTag::ACCEPT:
            return write_accept(p_record);
        case ControlTag::RESET: {
            ControlReset staged;
            staged.request = p_record.request;
            staged.token = p_record.token;
            WriteStream stream;
            if (!ControlReset::wire.run(stream, staged)) {
                return PackedByteArray();
            }
            return finish(stream);
        }
        case ControlTag::CLOSE: {
            ControlClose staged;
            staged.token = p_record.token;
            WriteStream stream;
            if (!ControlClose::wire.run(stream, staged)) {
                return PackedByteArray();
            }
            return finish(stream);
        }
    }
    return PackedByteArray();
}

bool read_control_record(
    const PackedByteArray &p_bytes,
    ControlRecord &r_record
) {
    NETW_ZONE_NC("Control record read", colors::WIRE);
    ReadStream stream(p_bytes);
    uint64_t tag = 0;
    if (!stream.bits(tag, 8)) {
        return false;
    }
    stream.seat(p_bytes);
    ControlRecord staged;
    staged.tag = ControlTag(uint8_t(tag));
    bool carried = false;
    switch (staged.tag) {
        case ControlTag::OPEN:
            carried = read_open(stream, staged);
            break;
        case ControlTag::READY: {
            ControlReady body;
            carried = ControlReady::wire.run(stream, body);
            staged.request = body.request;
            staged.token = body.token;
            break;
        }
        case ControlTag::ACCEPT:
            carried = read_accept(stream, staged);
            break;
        case ControlTag::RESET: {
            ControlReset body;
            carried = ControlReset::wire.run(stream, body);
            staged.request = body.request;
            staged.token = body.token;
            break;
        }
        case ControlTag::CLOSE: {
            ControlClose body;
            carried = ControlClose::wire.run(stream, body);
            staged.token = body.token;
            break;
        }
    }
    if (!carried) {
        NETW_DEBUG(
            sys::WIRE,
            "A control frame carrying tag %d decoded no record.",
            int(tag)
        );
    }
    if (!carried || !stream.align_verify() || !stream.ok()
        || stream.bits_remaining() != 0) {
        return false;
    }
    r_record.tag = staged.tag;
    r_record.request = staged.request;
    r_record.token = staged.token;
    r_record.route = staged.route;
    r_record.ordinal = staged.ordinal;
    r_record.family = staged.family;
    r_record.epoch = staged.epoch;
    r_record.tenure = staged.tenure;
    r_record.anchor = staged.anchor;
    r_record.schema = staged.schema;
    r_record.receipts = staged.receipts;
    return true;
}

Dictionary control_spec_records() {
    Dictionary out;
    out["RowControlOpen"] = ControlOpen::wire.spec_dump();
    out["RowControlReady"] = ControlReady::wire.spec_dump();
    out["RowControlReset"] = ControlReset::wire.spec_dump();
    out["RowControlClose"] = ControlClose::wire.spec_dump();
    out["RowControlAcceptHead"] = ControlAcceptHead::wire.spec_dump();
    out["RowControlAcceptEntry"] = ControlAcceptEntry::wire.spec_dump();
    return out;
}

} // namespace netw::wire

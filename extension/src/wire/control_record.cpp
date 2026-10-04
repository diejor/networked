#include "netw/wire/control_record.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/stream.hpp"

namespace netw::wire {

using namespace godot;

namespace {

int64_t step_from(uint64_t &r_cursor, uint64_t p_value) {
    const int64_t step = int64_t(p_value - r_cursor);
    r_cursor = p_value;
    return step;
}

uint64_t step_to(uint64_t &r_cursor, int64_t p_step) {
    r_cursor += uint64_t(p_step);
    return r_cursor;
}

ControlOpenBasis basis_of(const ControlRecord &p_record) {
    ControlOpenBasis held;
    held.epoch = p_record.epoch;
    held.tenure = p_record.tenure;
    held.anchor = p_record.anchor;
    held.anchor_author = p_record.anchor_author;
    held.schema = p_record.schema;
    return held;
}

bool omits_any(const ControlOpenHead &p_head) {
    return !(
        p_head.has_epoch && p_head.has_tenure && p_head.has_anchor
        && p_head.has_anchor_author && p_head.has_schema
    );
}

template <class Stream>
bool write_open(
    Stream &p_stream,
    const ControlRecord &p_record,
    ControlCursor &r_cursor,
    const ControlBasis &p_basis
) {
    const ControlOpenBasis *held
        = p_basis.find(ControlBasis::key_of(p_record.route, p_record.ordinal));
    ControlOpenHead head;
    head.request_step = step_from(r_cursor.opened, p_record.request);
    head.route = uint64_t(p_record.route);
    head.family = uint64_t(p_record.family);
    head.has_ordinal = p_record.ordinal != 0;
    head.has_epoch = held == nullptr || held->epoch != p_record.epoch;
    head.has_tenure = held == nullptr || held->tenure != p_record.tenure;
    head.has_anchor = held == nullptr || held->anchor != p_record.anchor;
    head.has_anchor_author
        = held == nullptr || held->anchor_author != p_record.anchor_author;
    head.has_schema = held == nullptr || held->schema != p_record.schema;
    if (!ControlOpenHead::wire.run(p_stream, head)) {
        return false;
    }
    uint64_t ordinal = p_record.ordinal;
    uint64_t epoch = p_record.epoch;
    uint64_t tenure = p_record.tenure;
    uint64_t anchor = p_record.anchor;
    uint64_t anchor_author = p_record.anchor_author;
    uint64_t schema = p_record.schema;
    uint64_t check = basis_of(p_record).check();
    return (!head.has_ordinal || p_stream.bits(ordinal, 8))
        && (!head.has_epoch || p_stream.varuint(epoch, 3))
        && (!head.has_tenure || p_stream.varuint(tenure, 5))
        && (!head.has_anchor || p_stream.varuint(anchor, 5))
        && (!head.has_anchor_author || p_stream.varuint(anchor_author, 5))
        && (!head.has_schema || p_stream.bits(schema, 32))
        && (omits_any(head) ? p_stream.bits(check, 8) : true)
        && p_stream.align_verify();
}

template <class Stream>
bool write_accept(Stream &p_stream, const ControlRecord &p_record) {
    ControlAcceptHead head;
    head.count = int64_t(p_record.receipts.size());
    if (!ControlAcceptHead::wire.run(p_stream, head)) {
        return false;
    }
    uint64_t cursor = 0;
    for (uint32_t at = 0; at < p_record.receipts.size(); ++at) {
        ControlAcceptStep entry;
        entry.token_step = step_from(cursor, p_record.receipts[at].token);
        entry.revision = p_record.receipts[at].revision;
        if (!ControlAcceptStep::wire.run(p_stream, entry)) {
            return false;
        }
    }
    return true;
}

template <class Stream>
bool write_record(
    Stream &p_stream,
    const ControlRecord &p_record,
    ControlCursor &r_cursor,
    const ControlBasis &p_basis
) {
    switch (p_record.tag) {
        case ControlTag::OPEN:
            return write_open(p_stream, p_record, r_cursor, p_basis);
        case ControlTag::READY: {
            ControlReady staged;
            staged.request_step
                = step_from(r_cursor.answered, p_record.request);
            staged.token_step = step_from(r_cursor.minted, p_record.token);
            return ControlReady::wire.run(p_stream, staged);
        }
        case ControlTag::ACCEPT:
            return write_accept(p_stream, p_record);
        case ControlTag::RESET: {
            ControlReset staged;
            staged.request_step
                = step_from(r_cursor.answered, p_record.request);
            staged.token = p_record.token;
            return ControlReset::wire.run(p_stream, staged);
        }
        case ControlTag::CLOSE: {
            ControlClose staged;
            staged.token_step = step_from(r_cursor.closed, p_record.token);
            return ControlClose::wire.run(p_stream, staged);
        }
    }
    return false;
}

const ControlOpenBasis *open_basis_of(
    uint64_t p_key,
    const ControlBasis &p_basis,
    const LocalVector<ControlOpenBasis> &p_staged,
    const LocalVector<uint64_t> &p_staged_keys
) {
    for (uint32_t at = p_staged_keys.size(); at > 0; --at) {
        if (p_staged_keys[at - 1] == p_key) {
            return &p_staged[at - 1];
        }
    }
    return p_basis.find(p_key);
}

struct FrameReading {
    ControlCursor cursor;
    LocalVector<ControlOpenBasis> opens;
    LocalVector<uint64_t> keys;
};

bool read_open(
    ReadStream &p_stream,
    const ControlBasis &p_basis,
    FrameReading &r_frame,
    ControlRecord &r_record
) {
    ControlOpenHead head;
    if (!ControlOpenHead::wire.run(p_stream, head)
        || head.family > uint64_t(STREAM_FAMILY_CEILING)) {
        return false;
    }
    uint64_t ordinal = 0;
    if (head.has_ordinal && (!p_stream.bits(ordinal, 8) || ordinal == 0)) {
        return false;
    }
    r_record.request = step_to(r_frame.cursor.opened, head.request_step);
    r_record.route = int64_t(head.route);
    r_record.ordinal = uint8_t(ordinal);
    r_record.family = StreamFamily(uint8_t(head.family));
    const uint64_t key = ControlBasis::key_of(r_record.route, r_record.ordinal);
    const ControlOpenBasis *held
        = open_basis_of(key, p_basis, r_frame.opens, r_frame.keys);
    ControlOpenBasis resolved;
    if (held != nullptr) {
        resolved = *held;
    }
    uint64_t schema = resolved.schema;
    uint64_t check = 0;
    if ((head.has_epoch && !p_stream.varuint(resolved.epoch, 3))
        || (head.has_tenure && !p_stream.varuint(resolved.tenure, 5))
        || (head.has_anchor && !p_stream.varuint(resolved.anchor, 5))
        || (head.has_anchor_author
            && !p_stream.varuint(resolved.anchor_author, 5))
        || (head.has_schema && !p_stream.bits(schema, 32))
        || (omits_any(head) && !p_stream.bits(check, 8))
        || !p_stream.align_verify()) {
        return false;
    }
    resolved.schema = uint32_t(schema);
    r_record.resolved = !omits_any(head)
        || (held != nullptr && resolved.check() == uint8_t(check));
    if (!r_record.resolved) {
        return true;
    }
    r_record.epoch = resolved.epoch;
    r_record.tenure = resolved.tenure;
    r_record.anchor = resolved.anchor;
    r_record.anchor_author = resolved.anchor_author;
    r_record.schema = resolved.schema;
    r_frame.opens.push_back(resolved);
    r_frame.keys.push_back(key);
    return true;
}

bool read_accept(ReadStream &p_stream, ControlRecord &r_record) {
    ControlAcceptHead head;
    if (!ControlAcceptHead::wire.run(p_stream, head)) {
        return false;
    }
    r_record.receipts.reserve(uint32_t(head.count));
    uint64_t cursor = 0;
    for (int64_t at = 0; at < head.count; ++at) {
        ControlAcceptStep step;
        if (!ControlAcceptStep::wire.run(p_stream, step)) {
            return false;
        }
        ControlAcceptEntry entry;
        entry.token = step_to(cursor, step.token_step);
        entry.revision = step.revision;
        if (entry.revision == 0 || entry.token == 0) {
            return false;
        }
        r_record.receipts.push_back(entry);
    }
    return true;
}

bool read_record(
    ReadStream &p_stream,
    const ControlBasis &p_basis,
    FrameReading &r_frame,
    ControlRecord &r_record
) {
    ReadStream peek = p_stream;
    uint64_t tag = 0;
    if (!peek.bits(tag, 8)) {
        return false;
    }
    r_record.tag = ControlTag(uint8_t(tag));
    switch (r_record.tag) {
        case ControlTag::OPEN:
            return read_open(p_stream, p_basis, r_frame, r_record);
        case ControlTag::READY: {
            ControlReady body;
            if (!ControlReady::wire.run(p_stream, body)) {
                return false;
            }
            r_record.request
                = step_to(r_frame.cursor.answered, body.request_step);
            r_record.token = step_to(r_frame.cursor.minted, body.token_step);
            return true;
        }
        case ControlTag::ACCEPT:
            return read_accept(p_stream, r_record);
        case ControlTag::RESET: {
            ControlReset body;
            if (!ControlReset::wire.run(p_stream, body)) {
                return false;
            }
            r_record.request
                = step_to(r_frame.cursor.answered, body.request_step);
            r_record.token = body.token;
            return true;
        }
        case ControlTag::CLOSE: {
            ControlClose body;
            if (!ControlClose::wire.run(p_stream, body)) {
                return false;
            }
            r_record.token = step_to(r_frame.cursor.closed, body.token_step);
            return true;
        }
    }
    NETW_DEBUG(
        sys::WIRE,
        "A control frame carrying tag %d decoded no record.",
        int(tag)
    );
    return false;
}

} // namespace

uint8_t ControlOpenBasis::check() const {
    uint64_t mixed = 0xCBF29CE484222325ULL;
    const uint64_t parts[] = {
        epoch,
        tenure,
        anchor,
        anchor_author,
        uint64_t(schema),
    };
    for (uint64_t part : parts) {
        mixed = (mixed ^ part) * 0x100000001B3ULL;
    }
    mixed ^= mixed >> 32;
    mixed ^= mixed >> 16;
    mixed ^= mixed >> 8;
    return uint8_t(mixed);
}

void ControlBasis::note(const ControlRecord &p_record) {
    if (p_record.tag != ControlTag::OPEN || !p_record.resolved) {
        return;
    }
    const uint64_t key = key_of(p_record.route, p_record.ordinal);
    const ControlOpenBasis held = basis_of(p_record);
    ControlOpenBasis *existing = opens.getptr(key);
    if (existing != nullptr) {
        *existing = held;
        return;
    }
    if (arrivals.size() < CONTROL_BASIS_CAPACITY) {
        arrivals.push_back(key);
    } else {
        opens.erase(arrivals[oldest]);
        arrivals[oldest] = key;
        oldest = (oldest + 1) % CONTROL_BASIS_CAPACITY;
    }
    opens.insert(key, held);
}

void ControlBasis::clear() {
    opens.clear();
    arrivals.clear();
    oldest = 0;
}

int64_t ControlFrameWriter::cost(const ControlRecord &p_record) const {
    MeasureStream measured;
    ControlCursor scratch = cursor;
    if (!write_record(measured, p_record, scratch, basis)) {
        return 0;
    }
    return measured.byte_length();
}

bool ControlFrameWriter::append(const ControlRecord &p_record) {
    NETW_ZONE_NC("Control record write", colors::WIRE);
    WriteStream piece;
    ControlCursor advanced = cursor;
    if (!write_record(piece, p_record, advanced, basis) || !piece.align_verify()
        || !piece.ok()) {
        return false;
    }
    if (!stream.raw_span(piece.data(), int64_t(piece.byte_count()))) {
        healthy = false;
        return false;
    }
    cursor = advanced;
    basis.note(p_record);
    records += 1;
    return true;
}

PackedByteArray ControlFrameWriter::bytes() const {
    if (!healthy || records == 0) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool read_control_frame(
    const PackedByteArray &p_bytes,
    ControlBasis &p_basis,
    LocalVector<ControlRecord> &r_records
) {
    NETW_ZONE_NC("Control frame read", colors::WIRE);
    if (p_bytes.is_empty()) {
        return false;
    }
    ReadStream stream(p_bytes);
    FrameReading frame;
    LocalVector<ControlRecord> staged;
    while (stream.bits_remaining() > 0) {
        ControlRecord record;
        if (!read_record(stream, p_basis, frame, record)
            || !stream.align_verify() || !stream.ok()) {
            return false;
        }
        staged.push_back(record);
    }
    for (uint32_t at = 0; at < staged.size(); ++at) {
        p_basis.note(staged[at]);
    }
    r_records = staged;
    return true;
}

PackedByteArray write_control_record(const ControlRecord &p_record) {
    ControlBasis fresh;
    ControlFrameWriter writer(fresh);
    writer.append(p_record);
    return writer.bytes();
}

bool read_control_record(
    const PackedByteArray &p_bytes,
    ControlRecord &r_record
) {
    ControlBasis fresh;
    LocalVector<ControlRecord> records;
    if (!read_control_frame(p_bytes, fresh, records) || records.size() != 1) {
        return false;
    }
    r_record.tag = records[0].tag;
    r_record.request = records[0].request;
    r_record.token = records[0].token;
    r_record.route = records[0].route;
    r_record.ordinal = records[0].ordinal;
    r_record.family = records[0].family;
    r_record.epoch = records[0].epoch;
    r_record.tenure = records[0].tenure;
    r_record.anchor = records[0].anchor;
    r_record.anchor_author = records[0].anchor_author;
    r_record.schema = records[0].schema;
    r_record.resolved = records[0].resolved;
    r_record.receipts = records[0].receipts;
    return true;
}

Dictionary control_spec_records() {
    Dictionary out;
    out["RowControlOpenHead"] = ControlOpenHead::wire.spec_dump();
    out["RowControlReady"] = ControlReady::wire.spec_dump();
    out["RowControlReset"] = ControlReset::wire.spec_dump();
    out["RowControlClose"] = ControlClose::wire.spec_dump();
    out["RowControlAcceptHead"] = ControlAcceptHead::wire.spec_dump();
    out["RowControlAcceptStep"] = ControlAcceptStep::wire.spec_dump();
    return out;
}

} // namespace netw::wire

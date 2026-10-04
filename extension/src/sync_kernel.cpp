#include "netw/sync_kernel.hpp"

#include "netw/call_args.hpp"

namespace netw::sync_kernel {

using namespace godot;

namespace {

template <class Head>
PackedByteArray row_write(const Head &p_head, const Array &p_values) {
    wire::WriteStream stream;
    Head staged = p_head;
    if (!Head::wire.run(stream, staged)
        || !call_args::values_write(stream, p_values, Array(), Array())
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

template <class Head>
bool row_read(const PackedByteArray &p_payload, Head &r_head, Array &r_values) {
    wire::ReadStream stream(p_payload);
    return Head::wire.run(stream, r_head)
        && call_args::values_read(stream, Array(), Array(), r_values)
        && stream.align_verify() && stream.bits_remaining() == 0;
}

} // namespace

PackedByteArray encode_volatile(int64_t p_ordinal, const Array &p_values) {
    VolatileHead head;
    head.ordinal = uint64_t(p_ordinal);
    head.flags = uint64_t(RESERVED);
    return row_write(head, p_values);
}

bool decode_volatile(
    const PackedByteArray &p_payload,
    int64_t p_columns,
    DecodedRow &r_row
) {
    r_row = DecodedRow();
    VolatileHead head;
    Array values;
    if (p_columns <= 0 || !row_read(p_payload, head, values)
        || head.flags != uint64_t(RESERVED) || values.size() != p_columns) {
        return false;
    }
    r_row.ordinal = int64_t(head.ordinal);
    r_row.mask
        = p_columns >= 64 ? ~uint64_t(0) : (uint64_t(1) << p_columns) - 1;
    r_row.values = values;
    return true;
}

PackedByteArray encode_retained(
    int64_t p_ordinal,
    int64_t p_mask,
    const Array &p_values
) {
    RetainedHead head;
    head.ordinal = uint64_t(p_ordinal);
    head.mask = uint64_t(p_mask);
    return row_write(head, p_values);
}

bool decode_retained(
    const PackedByteArray &p_payload,
    int64_t p_columns,
    DecodedRow &r_row
) {
    r_row = DecodedRow();
    RetainedHead head;
    Array values;
    if (!row_read(p_payload, head, values)) {
        return false;
    }
    uint64_t kept = 0;
    int64_t selected = 0;
    for (int64_t i = 0; i < p_columns && i < 64; ++i) {
        const uint64_t bit = uint64_t(1) << i;
        if ((head.mask & bit) != 0) {
            kept |= bit;
            selected += 1;
        }
    }
    if (selected == 0 || values.size() != selected) {
        return false;
    }
    r_row.ordinal = int64_t(head.ordinal);
    r_row.mask = kept;
    r_row.values = values;
    return true;
}

Dictionary frame_spec_records() {
    Dictionary out;
    out["SyncVolatileHead"] = VolatileHead::wire.spec_dump();
    out["SyncRetainedHead"] = RetainedHead::wire.spec_dump();
    return out;
}

} // namespace netw::sync_kernel

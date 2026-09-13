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

StagedWrites decode_volatile(
    const PackedByteArray &p_payload,
    const Array &p_keys
) {
    VolatileHead head;
    Array values;
    if (!row_read(p_payload, head, values) || head.flags != uint64_t(RESERVED)
        || values.size() != p_keys.size()) {
        return StagedWrites();
    }
    StagedWrites staged;
    staged.ordinal = int64_t(head.ordinal);
    staged.keys = p_keys.duplicate();
    staged.values = values;
    for (int64_t i = 0; i < p_keys.size(); ++i) {
        staged.row[p_keys[i]] = values[i];
    }
    return staged;
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

StagedWrites decode_retained(
    const PackedByteArray &p_payload,
    const Array &p_keys
) {
    RetainedHead head;
    Array values;
    if (!row_read(p_payload, head, values)) {
        return StagedWrites();
    }
    Array selected;
    for (int64_t i = 0; i < p_keys.size(); ++i) {
        if (head.mask & (uint64_t(1) << i)) {
            selected.push_back(p_keys[i]);
        }
    }
    if (values.size() != selected.size()) {
        return StagedWrites();
    }
    StagedWrites staged;
    staged.ordinal = int64_t(head.ordinal);
    staged.keys = selected;
    staged.values = values;
    for (int64_t i = 0; i < selected.size(); ++i) {
        staged.row[selected[i]] = values[i];
    }
    return staged;
}

Dictionary frame_spec_records() {
    Dictionary out;
    out["SyncVolatileHead"] = VolatileHead::wire.spec_dump();
    out["SyncRetainedHead"] = RetainedHead::wire.spec_dump();
    return out;
}

} // namespace netw::sync_kernel

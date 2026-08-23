#include "netw/api/sync_kernel.hpp"

#include "godot/class_db.hpp"
#include "netw/api/bit_buffer.hpp"
#include "netw/api/codec.hpp"

namespace netw {

using namespace godot;

PackedByteArray NetwSyncKernel::encode_volatile(
    int64_t p_ordinal,
    const Array &p_values
) {
    Ref<NetwBitBufferWriter> writer;
    writer.instantiate();
    NetwCodec::put_varint(writer, p_ordinal);
    writer->put_aligned_u8(RESERVED);
    NetwCodec::write_values(writer, p_values, Array(), Array());
    return writer->to_bytes();
}

Ref<NetwStagedWrites> NetwSyncKernel::decode_volatile(
    const PackedByteArray &p_payload,
    const Array &p_keys
) {
    Ref<NetwStagedWrites> staged;
    const Ref<NetwBitBufferReader> reader
        = NetwBitBufferReader::create(p_payload);
    const int64_t ordinal = NetwCodec::get_safe_varint(reader);
    if (reader->get_aligned_u8() != RESERVED) {
        return staged;
    }
    const Array values = NetwCodec::read_values(reader, Array(), Array());
    if (values.size() != p_keys.size()) {
        return staged;
    }
    staged.instantiate();
    staged->ordinal = ordinal;
    staged->keys = p_keys.duplicate();
    staged->values = values;
    for (int64_t i = 0; i < p_keys.size(); ++i) {
        staged->row[p_keys[i]] = values[i];
    }
    return staged;
}

PackedByteArray NetwSyncKernel::encode_retained(
    int64_t p_ordinal,
    int64_t p_mask,
    const Array &p_values
) {
    Ref<NetwBitBufferWriter> writer;
    writer.instantiate();
    NetwCodec::put_varint(writer, p_ordinal);
    NetwCodec::put_varint(writer, p_mask);
    NetwCodec::write_values(writer, p_values, Array(), Array());
    return writer->to_bytes();
}

Ref<NetwStagedWrites> NetwSyncKernel::decode_retained(
    const PackedByteArray &p_payload,
    const Array &p_keys
) {
    Ref<NetwStagedWrites> staged;
    const Ref<NetwBitBufferReader> reader
        = NetwBitBufferReader::create(p_payload);
    const int64_t ordinal = NetwCodec::get_safe_varint(reader);
    const int64_t mask = NetwCodec::get_safe_varint(reader);
    Array selected;
    for (int64_t i = 0; i < p_keys.size(); ++i) {
        if (mask & (int64_t(1) << i)) {
            selected.push_back(p_keys[i]);
        }
    }
    const Array values = NetwCodec::read_values(reader, Array(), Array());
    if (values.size() != selected.size()) {
        return staged;
    }
    staged.instantiate();
    staged->ordinal = ordinal;
    staged->keys = selected;
    staged->values = values;
    for (int64_t i = 0; i < selected.size(); ++i) {
        staged->row[selected[i]] = values[i];
    }
    return staged;
}

void NetwSyncKernel::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwSyncKernel",
        D_METHOD("encode_volatile", "ordinal", "values"),
        &NetwSyncKernel::encode_volatile
    );
    ClassDB::bind_static_method(
        "NetwSyncKernel",
        D_METHOD("decode_volatile", "payload", "keys"),
        &NetwSyncKernel::decode_volatile
    );
    ClassDB::bind_static_method(
        "NetwSyncKernel",
        D_METHOD("encode_retained", "ordinal", "mask", "values"),
        &NetwSyncKernel::encode_retained
    );
    ClassDB::bind_static_method(
        "NetwSyncKernel",
        D_METHOD("decode_retained", "payload", "keys"),
        &NetwSyncKernel::decode_retained
    );
    ClassDB::bind_integer_constant(
        "NetwSyncKernel",
        StringName(),
        "RESERVED",
        RESERVED
    );
}

} // namespace netw

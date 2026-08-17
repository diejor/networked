#include "netw/codec.hpp"

#include "netw/node_ref.hpp"

#include <cstdint>
#include <cstring>

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

void put_u64(const Ref<NetwBitBufferWriter> &writer, uint64_t value) {
    PackedByteArray bytes;
    bytes.resize(8);
    for (int index = 0; index < 8; ++index) {
        bytes.set(index, uint8_t((value >> (index * 8)) & 0xff));
    }
    writer->put_aligned_bytes(bytes);
}

uint64_t get_u64(const Ref<NetwBitBufferReader> &reader) {
    const PackedByteArray bytes = reader->get_aligned_bytes(8);
    uint64_t value = 0;
    for (int index = 0; index < bytes.size(); ++index) {
        value |= uint64_t(bytes[index]) << (index * 8);
    }
    return value;
}

void put_f32(const Ref<NetwBitBufferWriter> &writer, double value) {
    const float narrowed = float(value);
    uint32_t bits = 0;
    std::memcpy(&bits, &narrowed, sizeof(bits));
    writer->put_aligned_u32(bits);
}

double get_f32(const Ref<NetwBitBufferReader> &reader) {
    const uint32_t bits = uint32_t(reader->get_aligned_u32());
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

} // namespace

void NetwCodec::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD(
            "encode_snapshot",
            "tick",
            "ack",
            "payload",
            "keys",
            "quantizers"
        ),
        &NetwCodec::encode_snapshot
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("decode_snapshot", "bytes", "keys", "quantizers", "types"),
        &NetwCodec::decode_snapshot
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("encode_window", "samples", "keys", "quantizers"),
        &NetwCodec::encode_window
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("decode_window", "bytes", "keys", "quantizers", "types"),
        &NetwCodec::decode_window
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("encode_payload", "writer", "payload", "keys", "quantizers"),
        &NetwCodec::encode_payload
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("decode_payload", "reader", "keys", "quantizers", "types"),
        &NetwCodec::decode_payload
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("encode_value", "writer", "value", "quantizer"),
        &NetwCodec::encode_value
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("decode_value", "reader", "type", "quantizer"),
        &NetwCodec::decode_value
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("write_values", "writer", "values", "quantizers", "types"),
        &NetwCodec::write_values
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("read_values", "reader", "quantizers", "types"),
        &NetwCodec::read_values
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("put_varint", "writer", "value"),
        &NetwCodec::put_varint
    );
    ClassDB::bind_static_method(
        "NetwCodec",
        D_METHOD("get_safe_varint", "reader"),
        &NetwCodec::get_safe_varint
    );
    BIND_ENUM_CONSTANT(T_FALLBACK);
    BIND_ENUM_CONSTANT(T_BOOL);
    BIND_ENUM_CONSTANT(T_INT);
    BIND_ENUM_CONSTANT(T_VECTOR2);
    BIND_ENUM_CONSTANT(T_FLOAT);
    BIND_ENUM_CONSTANT(T_VECTOR3);
}

namespace {

// A node reference is its own kind on the wire. 0 is a raw value, 1 is a
// quantized one, 2 is this.
const uint8_t KIND_RAW = 0;
const uint8_t KIND_QUANTIZED = 1;
const uint8_t KIND_NODE_REF = 2;

void write_node_ref(
    const Ref<NetwBitBufferWriter> &p_writer,
    const Ref<NetwNodeRef> &p_ref
) {
    NetwCodec::put_varint(p_writer, p_ref->route);
    p_writer->put_aligned_u8(uint8_t(p_ref->comp));
    if (p_ref->comp == 255) {
        const PackedByteArray bytes = p_ref->path.to_utf8_buffer();
        p_writer->put_aligned_u32(uint32_t(bytes.size()));
        p_writer->put_aligned_bytes(bytes);
    }
}

Ref<NetwNodeRef> read_node_ref(const Ref<NetwBitBufferReader> &p_reader) {
    const int64_t route = NetwCodec::get_safe_varint(p_reader);
    const int64_t comp = p_reader->get_aligned_u8();
    String path;
    if (comp == 255) {
        const uint32_t length = p_reader->get_aligned_u32();
        path = gd::utf8_string(p_reader->get_aligned_bytes(int64_t(length)));
    }
    return NetwNodeRef::create(route, comp, path);
}

} // namespace

void NetwCodec::write_values(
    const Ref<NetwBitBufferWriter> &writer,
    const Array &values,
    const Array &quantizers,
    const Array &types
) {
    writer->put_aligned_u8(uint8_t(values.size()));
    for (int64_t i = 0; i < values.size(); ++i) {
        const Variant value = values[i];
        const Ref<NetwNodeRef> ref = value;
        if (ref.is_valid()) {
            writer->put_aligned_u8(KIND_NODE_REF);
            write_node_ref(writer, ref);
            continue;
        }
        const Ref<NetwQuantize> quantizer = quantizer_at(quantizers, int(i));
        const int declared = type_at(types, int(i));
        const bool quantized = quantizer.is_valid()
            && quantizer->supports_type(declared)
            && quantizer->supports_type(int(value.get_type()));
        if (quantized) {
            writer->put_aligned_u8(KIND_QUANTIZED);
            encode_value(writer, value, quantizer);
        } else {
            writer->put_aligned_u8(KIND_RAW);
            encode_value(writer, value, Ref<NetwQuantize>());
        }
    }
}

Array NetwCodec::read_values(
    const Ref<NetwBitBufferReader> &reader,
    const Array &quantizers,
    const Array &types
) {
    const int64_t count = reader->get_aligned_u8();
    Array out;
    for (int64_t i = 0; i < count; ++i) {
        const uint8_t kind = reader->get_aligned_u8();
        if (kind == KIND_NODE_REF) {
            out.push_back(read_node_ref(reader));
        } else if (kind == KIND_QUANTIZED) {
            out.push_back(decode_value(
                reader,
                type_at(types, int(i)),
                quantizer_at(quantizers, int(i))
            ));
        } else {
            out.push_back(
                decode_value(reader, Variant::NIL, Ref<NetwQuantize>())
            );
        }
    }
    return out;
}

Ref<NetwQuantize> NetwCodec::quantizer_at(const Array &quantizers, int index) {
    if (index >= quantizers.size()
        || quantizers[index].get_type() == Variant::NIL) {
        return Ref<NetwQuantize>();
    }
    return quantizers[index];
}

int NetwCodec::type_at(const Array &types, int index) {
    return index < types.size() ? int(types[index]) : Variant::NIL;
}

int NetwCodec::raw_type_of(const Variant &value) {
    switch (value.get_type()) {
        case Variant::BOOL:
            return T_BOOL;
        case Variant::INT:
            return T_INT;
        case Variant::VECTOR2:
            return T_VECTOR2;
        case Variant::FLOAT:
            return T_FLOAT;
        case Variant::VECTOR3:
            return T_VECTOR3;
        default:
            return T_FALLBACK;
    }
}

void NetwCodec::put_varint(
    const Ref<NetwBitBufferWriter> &writer,
    int64_t value
) {
    for (int index = 0; index < 5; ++index) {
        const int64_t byte = value & 0x7f;
        value >>= 7;
        if (value > 0) {
            writer->put_aligned_u8(byte | 0x80);
        } else {
            writer->put_aligned_u8(byte);
            break;
        }
    }
}

int64_t NetwCodec::get_safe_varint(const Ref<NetwBitBufferReader> &reader) {
    int64_t value = 0;
    for (int index = 0; index < 5; ++index) {
        const int64_t byte = reader->get_aligned_u8();
        value |= (byte & 0x7f) << (index * 7);
        if ((byte & 0x80) == 0) {
            return value;
        }
    }
    NETW_ERROR(sys::CODEC, "NetwCodec: Varint overflow/corrupt packet.");
    return -1;
}

int64_t NetwCodec::encode_zigzag(int64_t value) {
    return value >= 0 ? value << 1 : ((-value << 1) - 1);
}

int64_t NetwCodec::decode_zigzag(int64_t value) {
    return (value & 1) == 0 ? value >> 1 : -((value >> 1) + 1);
}

void NetwCodec::put_svarint(
    const Ref<NetwBitBufferWriter> &writer,
    int64_t value
) {
    put_varint(writer, encode_zigzag(value));
}

void NetwCodec::encode_value(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value,
    const Ref<NetwQuantize> &quantizer
) {
    if (quantizer.is_valid()) {
        quantizer->write(writer, value);
        return;
    }
    const int type = raw_type_of(value);
    writer->put_aligned_u8(type);
    switch (type) {
        case T_BOOL:
            writer->put_aligned_u8(bool(value) ? 1 : 0);
            break;
        case T_INT:
            put_u64(writer, uint64_t(int64_t(value)));
            break;
        case T_VECTOR2: {
            const Vector2 vector = value;
            put_f32(writer, vector.x);
            put_f32(writer, vector.y);
            break;
        }
        case T_FLOAT:
            put_f32(writer, double(value));
            break;
        case T_VECTOR3: {
            const Vector3 vector = value;
            put_f32(writer, vector.x);
            put_f32(writer, vector.y);
            put_f32(writer, vector.z);
            break;
        }
        default: {
            const PackedByteArray bytes = gd::var_to_bytes(value);
            writer->put_aligned_u32(bytes.size());
            writer->put_aligned_bytes(bytes);
            break;
        }
    }
}

Variant NetwCodec::decode_value(
    const Ref<NetwBitBufferReader> &reader,
    int type,
    const Ref<NetwQuantize> &quantizer
) {
    if (quantizer.is_valid()) {
        return quantizer->read(reader, type);
    }
    const int encoded_type = int(reader->get_aligned_u8());
    switch (encoded_type) {
        case T_BOOL:
            return reader->get_aligned_u8() != 0;
        case T_INT:
            return int64_t(get_u64(reader));
        case T_VECTOR2: {
            const double x = get_f32(reader);
            const double y = get_f32(reader);
            return Vector2(x, y);
        }
        case T_FLOAT:
            return get_f32(reader);
        case T_VECTOR3: {
            const double x = get_f32(reader);
            const double y = get_f32(reader);
            const double z = get_f32(reader);
            return Vector3(x, y, z);
        }
        default: {
            const int count = int(reader->get_aligned_u32());
            return gd::bytes_to_var(reader->get_aligned_bytes(count));
        }
    }
}

PackedByteArray NetwCodec::encode_snapshot(
    int64_t tick,
    int64_t ack,
    const Dictionary &payload,
    const Array &keys,
    const Array &quantizers
) {
    NETW_ZONE_NC("NetwCodec encode snapshot", colors::CODEC);
    Ref<NetwBitBufferWriter> writer;
    writer.instantiate();
    put_svarint(writer, tick);
    put_svarint(writer, ack);
    encode_payload(writer, payload, keys, quantizers);
    const PackedByteArray bytes = writer->to_bytes();
    NETW_ZONE_VALUE(bytes.size());
    NETW_PLOT(profile::names::CODEC_BYTES, bytes.size());
    NETW_TRACE(sys::CODEC, "encoded snapshot bytes=%d", bytes.size());
    return bytes;
}

Dictionary NetwCodec::decode_snapshot(
    const PackedByteArray &bytes,
    const Array &keys,
    const Array &quantizers,
    const Array &types
) {
    NETW_ZONE_NC("NetwCodec decode snapshot", colors::CODEC);
    NETW_ZONE_VALUE(bytes.size());
    if (bytes.is_empty()) {
        return Dictionary();
    }
    Ref<NetwBitBufferReader> reader = NetwBitBufferReader::create(bytes);
    const int64_t tick_value = get_safe_varint(reader);
    if (tick_value < 0) {
        return Dictionary();
    }
    const int64_t ack_value = get_safe_varint(reader);
    if (ack_value < 0) {
        return Dictionary();
    }
    Dictionary frame;
    frame[StringName("tick")] = decode_zigzag(tick_value);
    frame[StringName("ack")] = decode_zigzag(ack_value);
    frame[StringName("payload")]
        = decode_payload(reader, keys, quantizers, types);
    return frame;
}

PackedByteArray NetwCodec::encode_window(
    const Array &samples,
    const Array &keys,
    const Array &quantizers
) {
    NETW_ZONE_NC("NetwCodec encode window", colors::CODEC);
    if (samples.is_empty()) {
        return PackedByteArray();
    }
    Ref<NetwBitBufferWriter> writer;
    writer.instantiate();
    const Dictionary last = samples[samples.size() - 1];
    const int64_t base_tick = last.get(StringName("tick"), 0);
    put_varint(writer, base_tick + 1);
    writer->put_aligned_u8(samples.size());
    for (int index = 0; index < samples.size(); ++index) {
        const Dictionary sample = samples[index];
        const int64_t tick = sample.get(StringName("tick"), 0);
        writer->put_aligned_u8(base_tick - tick);
        encode_payload(
            writer,
            sample.get(StringName("input"), Dictionary()),
            keys,
            quantizers
        );
    }
    const PackedByteArray bytes = writer->to_bytes();
    NETW_ZONE_VALUE(bytes.size());
    NETW_PLOT(profile::names::CODEC_BYTES, bytes.size());
    return bytes;
}

Array NetwCodec::decode_window(
    const PackedByteArray &bytes,
    const Array &keys,
    const Array &quantizers,
    const Array &types
) {
    NETW_ZONE_NC("NetwCodec decode window", colors::CODEC);
    NETW_ZONE_VALUE(bytes.size());
    Array samples;
    if (bytes.is_empty()) {
        return samples;
    }
    Ref<NetwBitBufferReader> reader = NetwBitBufferReader::create(bytes);
    const int64_t base_tick_value = get_safe_varint(reader);
    if (base_tick_value < 0) {
        return samples;
    }
    const int64_t base_tick = base_tick_value - 1;
    const int count = int(reader->get_aligned_u8());
    for (int index = 0; index < count; ++index) {
        Dictionary sample;
        sample[StringName("tick")] = base_tick - reader->get_aligned_u8();
        sample[StringName("input")]
            = decode_payload(reader, keys, quantizers, types);
        samples.append(sample);
    }
    return samples;
}

void NetwCodec::encode_payload(
    const Ref<NetwBitBufferWriter> &writer,
    const Dictionary &payload,
    const Array &keys,
    const Array &quantizers
) {
    NETW_ZONE_NC("NetwCodec encode payload", colors::CODEC);
    NETW_ZONE_VALUE(keys.size());
    for (int index = 0; index < keys.size(); ++index) {
        encode_value(
            writer,
            payload.get(keys[index], Variant()),
            quantizer_at(quantizers, index)
        );
    }
}

Dictionary NetwCodec::decode_payload(
    const Ref<NetwBitBufferReader> &reader,
    const Array &keys,
    const Array &quantizers,
    const Array &types
) {
    NETW_ZONE_NC("NetwCodec decode payload", colors::CODEC);
    NETW_ZONE_VALUE(keys.size());
    Dictionary payload;
    for (int index = 0; index < keys.size(); ++index) {
        payload[keys[index]] = decode_value(
            reader,
            type_at(types, index),
            quantizer_at(quantizers, index)
        );
    }
    return payload;
}

} // namespace netw

#include "netw/api/bit_stream.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwBitStream::_bind_methods() {
    ClassDB::bind_static_method(
        get_class_static(),
        D_METHOD("writer"),
        &NetwBitStream::writer
    );
    ClassDB::bind_static_method(
        get_class_static(),
        D_METHOD("reader", "bytes"),
        &NetwBitStream::reader
    );
    ClassDB::bind_static_method(
        get_class_static(),
        D_METHOD("measurer"),
        &NetwBitStream::measurer
    );
    ClassDB::bind_method(D_METHOD("get_mode"), &NetwBitStream::get_mode);
    ClassDB::bind_method(
        D_METHOD("bits", "value", "count"),
        &NetwBitStream::bits
    );
    ClassDB::bind_method(
        D_METHOD("int_range", "value", "low", "high"),
        &NetwBitStream::int_range
    );
    ClassDB::bind_method(
        D_METHOD("varuint", "value", "max_bytes"),
        &NetwBitStream::varuint,
        DEFVAL(10)
    );
    ClassDB::bind_method(
        D_METHOD("svarint", "value", "max_bytes"),
        &NetwBitStream::svarint,
        DEFVAL(10)
    );
    ClassDB::bind_method(D_METHOD("bool1", "value"), &NetwBitStream::bool1);
    ClassDB::bind_method(
        D_METHOD("bytes_capped", "value", "cap"),
        &NetwBitStream::bytes_capped
    );
    ClassDB::bind_method(D_METHOD("string", "value"), &NetwBitStream::string);
    ClassDB::bind_method(
        D_METHOD("align_verify"),
        &NetwBitStream::align_verify
    );
    ClassDB::bind_method(D_METHOD("ok"), &NetwBitStream::ok);
    ClassDB::bind_method(D_METHOD("bit_length"), &NetwBitStream::bit_length);
    ClassDB::bind_method(
        D_METHOD("bits_remaining"),
        &NetwBitStream::bits_remaining
    );
    ClassDB::bind_method(D_METHOD("to_bytes"), &NetwBitStream::to_bytes);

    BIND_ENUM_CONSTANT(WRITE);
    BIND_ENUM_CONSTANT(READ);
    BIND_ENUM_CONSTANT(MEASURE);
}

Ref<NetwBitStream> NetwBitStream::writer() {
    Ref<NetwBitStream> made;
    made.instantiate();
    made->mode = WRITE;
    return made;
}

Ref<NetwBitStream> NetwBitStream::reader(const PackedByteArray &p_bytes) {
    Ref<NetwBitStream> made;
    made.instantiate();
    made->mode = READ;
    made->reading.seat(p_bytes);
    return made;
}

Ref<NetwBitStream> NetwBitStream::measurer() {
    Ref<NetwBitStream> made;
    made.instantiate();
    made->mode = MEASURE;
    return made;
}

int64_t NetwBitStream::bits(int64_t p_value, int p_count) {
    uint64_t staged = uint64_t(p_value);
    run([&](auto &stream) { return stream.bits(staged, p_count); });
    return int64_t(staged);
}

int64_t NetwBitStream::int_range(
    int64_t p_value,
    int64_t p_low,
    int64_t p_high
) {
    int64_t staged = p_value;
    run([&](auto &stream) { return stream.int_range(staged, p_low, p_high); });
    return staged;
}

int64_t NetwBitStream::varuint(int64_t p_value, int p_max_bytes) {
    uint64_t staged = uint64_t(p_value);
    run([&](auto &stream) { return stream.varuint(staged, p_max_bytes); });
    return int64_t(staged);
}

int64_t NetwBitStream::svarint(int64_t p_value, int p_max_bytes) {
    int64_t staged = p_value;
    run([&](auto &stream) { return stream.svarint(staged, p_max_bytes); });
    return staged;
}

bool NetwBitStream::bool1(bool p_value) {
    bool staged = p_value;
    run([&](auto &stream) { return stream.bool1(staged); });
    return staged;
}

PackedByteArray NetwBitStream::bytes_capped(
    const PackedByteArray &p_value,
    int p_cap
) {
    PackedByteArray staged = p_value;
    run([&](auto &stream) { return stream.bytes_capped(staged, p_cap); });
    return staged;
}

String NetwBitStream::string(const String &p_value) {
    String staged = p_value;
    run([&](auto &stream) { return wire::string_field(stream, staged); });
    return staged;
}

bool NetwBitStream::align_verify() {
    return run([&](auto &stream) { return stream.align_verify(); });
}

bool NetwBitStream::ok() {
    return run([&](auto &stream) { return stream.ok(); });
}

int64_t NetwBitStream::bit_length() {
    int64_t out = 0;
    run([&](auto &stream) {
        out = stream.bit_length();
        return true;
    });
    return out;
}

int64_t NetwBitStream::bits_remaining() {
    return mode == READ ? reading.bits_remaining() : 0;
}

PackedByteArray NetwBitStream::to_bytes() const {
    return mode == WRITE ? writing.to_bytes() : PackedByteArray();
}

} // namespace netw

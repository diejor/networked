#include "netw/api/bit_buffer.hpp"

#include <algorithm>

#include "godot/class_db.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

uint64_t low_mask(int count) {
    return count >= 64 ? UINT64_MAX : (uint64_t(1) << count) - 1;
}

} // namespace

void NetwBitBufferWriter::_bind_methods() {
    ClassDB::bind_method(D_METHOD("reset"), &NetwBitBufferWriter::reset);
    ClassDB::bind_method(
        D_METHOD("put_bits", "value", "count"),
        &NetwBitBufferWriter::put_bits
    );
    ClassDB::bind_method(D_METHOD("align"), &NetwBitBufferWriter::align);
    ClassDB::bind_method(
        D_METHOD("put_aligned_u8", "value"),
        &NetwBitBufferWriter::put_aligned_u8
    );
    ClassDB::bind_method(
        D_METHOD("put_aligned_u16", "value"),
        &NetwBitBufferWriter::put_aligned_u16
    );
    ClassDB::bind_method(
        D_METHOD("put_aligned_u32", "value"),
        &NetwBitBufferWriter::put_aligned_u32
    );
    ClassDB::bind_method(
        D_METHOD("put_aligned_bytes", "bytes"),
        &NetwBitBufferWriter::put_aligned_bytes
    );
    ClassDB::bind_method(D_METHOD("to_bytes"), &NetwBitBufferWriter::to_bytes);
}

void NetwBitBufferWriter::reset() {
    output.clear();
    accumulator = 0;
    bit_count = 0;
}

void NetwBitBufferWriter::put_bits(int64_t value, int count) {
    NETW_ZONE_NC("NetwBitBufferWriter put bits", colors::BIT_BUFFER);
    NETW_ZONE_VALUE(count);
    if (count <= 0) {
        return;
    }
    accumulator |= (uint64_t(value) & low_mask(count)) << bit_count;
    bit_count += count;
    while (bit_count >= 8) {
        output.append(uint8_t(accumulator & 0xff));
        accumulator >>= 8;
        bit_count -= 8;
    }
}

void NetwBitBufferWriter::align() {
    if (bit_count <= 0) {
        return;
    }
    output.append(uint8_t(accumulator & 0xff));
    accumulator = 0;
    bit_count = 0;
}

void NetwBitBufferWriter::put_aligned_u8(int64_t value) {
    NETW_ZONE_NC("NetwBitBufferWriter put u8", colors::BIT_BUFFER);
    align();
    output.append(uint8_t(value & 0xff));
}

void NetwBitBufferWriter::put_aligned_u16(int64_t value) {
    NETW_ZONE_NC("NetwBitBufferWriter put u16", colors::BIT_BUFFER);
    align();
    for (int index = 0; index < 2; ++index) {
        output.append(uint8_t((uint64_t(value) >> (index * 8)) & 0xff));
    }
}

void NetwBitBufferWriter::put_aligned_u32(int64_t value) {
    NETW_ZONE_NC("NetwBitBufferWriter put u32", colors::BIT_BUFFER);
    align();
    for (int index = 0; index < 4; ++index) {
        output.append(uint8_t((uint64_t(value) >> (index * 8)) & 0xff));
    }
}

void NetwBitBufferWriter::put_aligned_bytes(const PackedByteArray &bytes) {
    NETW_ZONE_NC("NetwBitBufferWriter put bytes", colors::BIT_BUFFER);
    NETW_ZONE_VALUE(bytes.size());
    align();
    output.append_array(bytes);
}

PackedByteArray NetwBitBufferWriter::to_bytes() {
    align();
    return output;
}

void NetwBitBufferReader::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwBitBufferReader",
        D_METHOD("create", "bytes"),
        &NetwBitBufferReader::create
    );
    ClassDB::bind_method(
        D_METHOD("reset", "bytes"),
        &NetwBitBufferReader::reset
    );
    ClassDB::bind_method(
        D_METHOD("get_bits", "count"),
        &NetwBitBufferReader::get_bits
    );
    ClassDB::bind_method(D_METHOD("align"), &NetwBitBufferReader::align);
    ClassDB::bind_method(
        D_METHOD("get_aligned_u8"),
        &NetwBitBufferReader::get_aligned_u8
    );
    ClassDB::bind_method(
        D_METHOD("get_aligned_u16"),
        &NetwBitBufferReader::get_aligned_u16
    );
    ClassDB::bind_method(
        D_METHOD("get_aligned_u32"),
        &NetwBitBufferReader::get_aligned_u32
    );
    ClassDB::bind_method(
        D_METHOD("get_aligned_bytes", "count"),
        &NetwBitBufferReader::get_aligned_bytes
    );
    ClassDB::bind_method(
        D_METHOD("remaining_bytes"),
        &NetwBitBufferReader::remaining_bytes
    );
    ClassDB::bind_method(D_METHOD("ok"), &NetwBitBufferReader::ok);
}

Ref<NetwBitBufferReader> NetwBitBufferReader::create(
    const PackedByteArray &bytes
) {
    Ref<NetwBitBufferReader> reader;
    reader.instantiate();
    reader->reset(bytes);
    return reader;
}

void NetwBitBufferReader::reset(const PackedByteArray &bytes) {
    input = bytes;
    position = 0;
    accumulator = 0;
    bit_count = 0;
    healthy = true;
}

uint8_t NetwBitBufferReader::take_byte() {
    const bool has_byte = position < input.size();
    healthy = healthy && has_byte;
    const uint8_t byte = has_byte ? input[position] : 0;
    position += 1;
    return byte;
}

int64_t NetwBitBufferReader::get_bits(int count) {
    NETW_ZONE_NC("NetwBitBufferReader get bits", colors::BIT_BUFFER);
    NETW_ZONE_VALUE(count);
    if (count <= 0) {
        return 0;
    }
    while (bit_count < count) {
        const uint8_t byte = take_byte();
        accumulator |= uint64_t(byte) << bit_count;
        bit_count += 8;
    }
    const uint64_t result = accumulator & low_mask(count);
    accumulator >>= count;
    bit_count -= count;
    return int64_t(result);
}

void NetwBitBufferReader::align() {
    accumulator = 0;
    bit_count = 0;
}

int64_t NetwBitBufferReader::get_aligned_u8() {
    NETW_ZONE_NC("NetwBitBufferReader get u8", colors::BIT_BUFFER);
    align();
    return take_byte();
}

int64_t NetwBitBufferReader::get_aligned_u16() {
    NETW_ZONE_NC("NetwBitBufferReader get u16", colors::BIT_BUFFER);
    align();
    uint64_t value = 0;
    for (int index = 0; index < 2; ++index) {
        value |= uint64_t(take_byte()) << (index * 8);
    }
    return int64_t(value);
}

int64_t NetwBitBufferReader::get_aligned_u32() {
    NETW_ZONE_NC("NetwBitBufferReader get u32", colors::BIT_BUFFER);
    align();
    uint64_t value = 0;
    for (int index = 0; index < 4; ++index) {
        value |= uint64_t(take_byte()) << (index * 8);
    }
    return int64_t(value);
}

PackedByteArray NetwBitBufferReader::get_aligned_bytes(int count) {
    NETW_ZONE_NC("NetwBitBufferReader get bytes", colors::BIT_BUFFER);
    NETW_ZONE_VALUE(count);
    align();
    const int safe_count = std::max(count, 0);
    const int64_t end = std::min<int64_t>(position + safe_count, input.size());
    healthy = healthy && position + safe_count <= input.size();
    PackedByteArray bytes = input.slice(position, end);
    position += safe_count;
    return bytes;
}

int NetwBitBufferReader::remaining_bytes() {
    align();
    return int(std::max<int64_t>(input.size() - position, 0));
}

} // namespace netw

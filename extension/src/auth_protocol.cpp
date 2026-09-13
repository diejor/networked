#include "netw/auth_protocol.hpp"

using namespace godot;

namespace netw::auth {

namespace {

const uint8_t HELLO_BYTES[4] = {0x4E, 0x48, 0x45, 0x4C};
const uint8_t PROBE_BYTES[4] = {0x4E, 0x50, 0x52, 0x42};

PackedByteArray magic_of(const uint8_t p_bytes[4]) {
    PackedByteArray magic;
    for (int at = 0; at < 4; at++) {
        magic.push_back(p_bytes[at]);
    }
    return magic;
}

bool matches(const PackedByteArray &p_data, const uint8_t p_magic[4]) {
    if (p_data.size() < 4) {
        return false;
    }
    for (int at = 0; at < 4; at++) {
        if (p_data[at] != p_magic[at]) {
            return false;
        }
    }
    return true;
}

void append_u64(PackedByteArray &r_buffer, uint64_t p_value) {
    for (int at = 0; at < 8; at++) {
        r_buffer.push_back(uint8_t((p_value >> (at * 8)) & 0xFF));
    }
}

uint64_t read_u64(const PackedByteArray &p_data, int p_at) {
    uint64_t out = 0;
    for (int at = 0; at < 8; at++) {
        out |= uint64_t(uint8_t(p_data[p_at + at])) << (at * 8);
    }
    return out;
}

} // namespace

PackedByteArray magic_hello() {
    return magic_of(HELLO_BYTES);
}

PackedByteArray magic_probe() {
    return magic_of(PROBE_BYTES);
}

Kind classify(const PackedByteArray &p_data) {
    if (p_data.size() < PROBE_HEADER_LEN) {
        return Kind::UNKNOWN;
    }
    if (matches(p_data, HELLO_BYTES)) {
        return Kind::HELLO;
    }
    if (matches(p_data, PROBE_BYTES)) {
        return Kind::PROBE;
    }
    return Kind::UNKNOWN;
}

PackedByteArray encode_client_hello(
    const PackedByteArray &p_provider_payload,
    uint64_t p_app_tag,
    int p_flags
) {
    PackedByteArray buffer = magic_of(HELLO_BYTES);
    buffer.push_back(PROTOCOL_VERSION);
    append_u64(buffer, p_app_tag);
    buffer.push_back(uint8_t(p_flags & 0xFF));
    buffer.append_array(p_provider_payload);
    return buffer;
}

Hello decode_client_hello(
    const PackedByteArray &p_data,
    uint64_t p_local_app_tag
) {
    Hello decoded;
    if (!matches(p_data, HELLO_BYTES) || p_data.size() < HELLO_HEADER_LEN) {
        return decoded;
    }
    decoded.version = int(p_data[4]);
    decoded.app_tag = read_u64(p_data, 5);
    if (decoded.version != int(PROTOCOL_VERSION)) {
        decoded.refusal = Refusal::VERSION;
        return decoded;
    }
    if (decoded.app_tag != p_local_app_tag) {
        decoded.refusal = Refusal::APP;
        return decoded;
    }
    decoded.refusal = Refusal::NONE;
    decoded.flags = int(p_data[13]);
    decoded.provider_payload = p_data.slice(HELLO_HEADER_LEN, p_data.size());
    return decoded;
}

PackedByteArray encode_probe_request(int p_flags) {
    PackedByteArray buffer = magic_of(PROBE_BYTES);
    buffer.push_back(PROTOCOL_VERSION);
    buffer.push_back(uint8_t(p_flags & 0xFF));
    return buffer;
}

ProbeRequest decode_probe_request(const PackedByteArray &p_data) {
    ProbeRequest decoded;
    if (!matches(p_data, PROBE_BYTES) || p_data.size() < PROBE_HEADER_LEN) {
        return decoded;
    }
    decoded.version = int(p_data[4]);
    if (decoded.version != int(PROTOCOL_VERSION)) {
        return decoded;
    }
    decoded.ok = true;
    decoded.flags = int(p_data[5]);
    return decoded;
}

PackedByteArray encode_probe_reply(
    int p_status,
    const PackedByteArray &p_payload
) {
    PackedByteArray buffer = magic_of(PROBE_BYTES);
    buffer.push_back(PROTOCOL_VERSION);
    buffer.push_back(uint8_t(p_status & 0xFF));
    buffer.append_array(p_payload);
    return buffer;
}

ProbeReply decode_probe_reply(const PackedByteArray &p_data) {
    ProbeReply decoded;
    if (!matches(p_data, PROBE_BYTES) || p_data.size() < PROBE_HEADER_LEN) {
        return decoded;
    }
    decoded.version = int(p_data[4]);
    if (decoded.version != int(PROTOCOL_VERSION)) {
        return decoded;
    }
    decoded.ok = true;
    decoded.status = int(p_data[5]);
    decoded.payload = p_data.slice(PROBE_HEADER_LEN, p_data.size());
    return decoded;
}

} // namespace netw::auth

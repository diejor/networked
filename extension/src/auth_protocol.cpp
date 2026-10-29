#include "netw/auth_protocol.hpp"

using namespace godot;

namespace netw::auth {

namespace {

const uint8_t HELLO_BYTES[4] = {0x4E, 0x48, 0x45, 0x4C};
const uint8_t PROBE_BYTES[4] = {0x4E, 0x50, 0x52, 0x42};
const uint8_t GAME_BYTES[4] = {0x4E, 0x47, 0x41, 0x4D};
const uint8_t JOIN_BYTES[4] = {0x4E, 0x4A, 0x4F, 0x4E};
const uint8_t ANSWER_BYTES[4] = {0x4E, 0x41, 0x4E, 0x53};

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

void append_u32(PackedByteArray &r_buffer, uint32_t p_value) {
    for (int at = 0; at < 4; at++) {
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

uint32_t read_u32(const PackedByteArray &p_data, int p_at) {
    uint32_t out = 0;
    for (int at = 0; at < 4; at++) {
        out |= uint32_t(uint8_t(p_data[p_at + at])) << (at * 8);
    }
    return out;
}

PackedByteArray tagged(
    const uint8_t p_magic[4],
    const PackedByteArray &p_body
) {
    PackedByteArray buffer = magic_of(p_magic);
    buffer.append_array(p_body);
    return buffer;
}

PackedByteArray answer_header(AnswerKind p_kind) {
    PackedByteArray buffer = magic_of(ANSWER_BYTES);
    buffer.push_back(PROTOCOL_VERSION);
    buffer.push_back(uint8_t(p_kind));
    return buffer;
}

} // namespace

PackedByteArray magic_hello() {
    return magic_of(HELLO_BYTES);
}

PackedByteArray magic_probe() {
    return magic_of(PROBE_BYTES);
}

Kind classify(const PackedByteArray &p_data) {
    if (p_data.size() < TAG_LEN) {
        return Kind::UNKNOWN;
    }
    if (matches(p_data, HELLO_BYTES)) {
        return Kind::HELLO;
    }
    if (matches(p_data, PROBE_BYTES)) {
        return Kind::PROBE;
    }
    if (matches(p_data, GAME_BYTES)) {
        return Kind::GAME;
    }
    if (matches(p_data, JOIN_BYTES)) {
        return Kind::JOIN;
    }
    if (matches(p_data, ANSWER_BYTES)) {
        return Kind::ANSWER;
    }
    return Kind::UNKNOWN;
}

PackedByteArray encode_client_hello(
    uint64_t p_app_tag,
    int p_flags,
    int64_t p_deadline_msec
) {
    PackedByteArray buffer = magic_of(HELLO_BYTES);
    buffer.push_back(PROTOCOL_VERSION);
    append_u64(buffer, p_app_tag);
    buffer.push_back(uint8_t(p_flags & 0xFF));
    const int64_t deadline = p_deadline_msec < 0 ? 0 : p_deadline_msec;
    append_u32(
        buffer,
        uint32_t(deadline > int64_t(UINT32_MAX) ? UINT32_MAX : deadline)
    );
    return buffer;
}

Hello decode_client_hello(
    const PackedByteArray &p_data,
    uint64_t p_local_app_tag
) {
    Hello decoded;
    if (!matches(p_data, HELLO_BYTES) || p_data.size() < 14) {
        return decoded;
    }
    decoded.version = int(p_data[4]);
    decoded.app_tag = read_u64(p_data, 5);
    if (decoded.version != int(PROTOCOL_VERSION)) {
        decoded.refusal = Refusal::VERSION;
        return decoded;
    }
    if (p_data.size() < HELLO_HEADER_LEN) {
        return decoded;
    }
    if (decoded.app_tag != p_local_app_tag) {
        decoded.refusal = Refusal::APP;
        return decoded;
    }
    decoded.refusal = Refusal::NONE;
    decoded.flags = int(p_data[13]);
    decoded.deadline_msec = int64_t(read_u32(p_data, 14));
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

PackedByteArray encode_game(const PackedByteArray &p_data) {
    return tagged(GAME_BYTES, p_data);
}

PackedByteArray encode_join(const PackedByteArray &p_join) {
    return tagged(JOIN_BYTES, p_join);
}

PackedByteArray payload_of(const PackedByteArray &p_data) {
    return p_data.slice(TAG_LEN, p_data.size());
}

PackedByteArray encode_admitted(const PackedByteArray &p_roster) {
    PackedByteArray buffer = answer_header(AnswerKind::ADMITTED);
    buffer.append_array(p_roster);
    return buffer;
}

PackedByteArray encode_confirmed() {
    return answer_header(AnswerKind::CONFIRMED);
}

PackedByteArray encode_refused(int64_t p_code, const String &p_reason) {
    PackedByteArray buffer = answer_header(AnswerKind::REFUSED);
    append_u32(buffer, uint32_t(int32_t(p_code)));
    const CharString text = capped_reason(p_reason).utf8();
    buffer.push_back(uint8_t(text.length()));
    for (int at = 0; at < text.length(); ++at) {
        buffer.push_back(uint8_t(text[at]));
    }
    return buffer;
}

Answer decode_answer(const PackedByteArray &p_data) {
    Answer decoded;
    if (!matches(p_data, ANSWER_BYTES) || p_data.size() < ANSWER_HEADER_LEN
        || int(p_data[4]) != int(PROTOCOL_VERSION)) {
        return decoded;
    }
    const int kind = int(p_data[5]);
    if (kind == int(AnswerKind::ADMITTED)
        || kind == int(AnswerKind::CONFIRMED)) {
        decoded.ok = true;
        decoded.kind = AnswerKind(kind);
        decoded.payload = p_data.slice(ANSWER_HEADER_LEN, p_data.size());
        return decoded;
    }
    if (kind != int(AnswerKind::REFUSED)
        || p_data.size() < ANSWER_HEADER_LEN + 5) {
        return decoded;
    }
    const int length = int(p_data[ANSWER_HEADER_LEN + 4]);
    if (p_data.size() != ANSWER_HEADER_LEN + 5 + length) {
        return decoded;
    }
    decoded.ok = true;
    decoded.kind = AnswerKind::REFUSED;
    decoded.code = int64_t(int32_t(read_u32(p_data, ANSWER_HEADER_LEN)));
    decoded.reason = String::utf8(
        reinterpret_cast<const char *>(p_data.ptr()) + ANSWER_HEADER_LEN + 5,
        length
    );
    return decoded;
}

String capped_reason(const String &p_reason) {
    const CharString text = p_reason.utf8();
    if (text.length() <= REASON_CAP) {
        return p_reason;
    }
    int cut = REASON_CAP;
    while (cut > 0 && (uint8_t(text[cut]) & 0xC0) == 0x80) {
        cut -= 1;
    }
    return String::utf8(text.get_data(), cut);
}

String bind_challenge(
    const String &p_challenge,
    const String &p_server_identity
) {
    return p_server_identity.is_empty()
        ? p_challenge
        : (p_challenge + p_server_identity).sha256_text();
}

} // namespace netw::auth

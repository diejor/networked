#include "netw/connect/lan_query.hpp"

#include "netw/auth_protocol.hpp"

using namespace godot;

namespace netw::connect::lan {

namespace {

const uint8_t QUERY_BYTES[4] = {0x4E, 0x57, 0x4C, 0x51};
const uint8_t REPLY_BYTES[4] = {0x4E, 0x57, 0x4C, 0x52};
const uint8_t VERSION = 1;
const int QUERY_LEN = 4 + 1 + 8 + 8;
const int REPLY_HEADER_LEN = QUERY_LEN + 8 + 2;

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

PackedByteArray header(
    const uint8_t p_magic[4],
    uint64_t p_app_tag,
    uint64_t p_nonce
) {
    PackedByteArray buffer;
    for (int at = 0; at < 4; at++) {
        buffer.push_back(p_magic[at]);
    }
    buffer.push_back(VERSION);
    append_u64(buffer, p_app_tag);
    append_u64(buffer, p_nonce);
    return buffer;
}

bool header_matches(
    const PackedByteArray &p_data,
    const uint8_t p_magic[4],
    int p_min_len,
    uint64_t p_app_tag
) {
    if (p_data.size() < p_min_len) {
        return false;
    }
    for (int at = 0; at < 4; at++) {
        if (uint8_t(p_data[at]) != p_magic[at]) {
            return false;
        }
    }
    return uint8_t(p_data[4]) == VERSION && read_u64(p_data, 5) == p_app_tag;
}

PackedByteArray reply_with(
    const PackedByteArray &p_header,
    const Ref<NetwServerInfo> &p_info
) {
    PackedByteArray buffer = p_header;
    buffer.append_array(auth::encode_probe_reply(
        int(auth::ProbeStatus::OK),
        NetwServerInfo::to_payload(p_info)
    ));
    return buffer;
}

} // namespace

PackedByteArray encode_query(uint64_t p_app_tag, uint64_t p_nonce) {
    return header(QUERY_BYTES, p_app_tag, p_nonce);
}

bool decode_query(
    const PackedByteArray &p_data,
    uint64_t p_app_tag,
    uint64_t &r_nonce
) {
    if (p_data.size() != QUERY_LEN
        || !header_matches(p_data, QUERY_BYTES, QUERY_LEN, p_app_tag)) {
        return false;
    }
    r_nonce = read_u64(p_data, 13);
    return true;
}

PackedByteArray encode_reply(
    uint64_t p_app_tag,
    uint64_t p_nonce,
    uint64_t p_instance,
    int64_t p_gameplay_port,
    const Ref<NetwServerInfo> &p_info
) {
    PackedByteArray head = header(REPLY_BYTES, p_app_tag, p_nonce);
    append_u64(head, p_instance);
    head.push_back(uint8_t(p_gameplay_port & 0xFF));
    head.push_back(uint8_t((p_gameplay_port >> 8) & 0xFF));
    PackedByteArray whole = reply_with(head, p_info);
    if (whole.size() <= MAX_DATAGRAM || p_info.is_null()) {
        return whole;
    }
    const Ref<NetwServerInfo> trimmed
        = NetwServerInfo::from_payload(NetwServerInfo::to_payload(p_info));
    trimmed->set_metadata(Dictionary());
    return reply_with(head, trimmed);
}

bool decode_reply(
    const PackedByteArray &p_data,
    uint64_t p_app_tag,
    uint64_t p_nonce,
    Reply &r_reply
) {
    if (!header_matches(p_data, REPLY_BYTES, REPLY_HEADER_LEN, p_app_tag)
        || read_u64(p_data, 13) != p_nonce) {
        return false;
    }
    const auth::ProbeReply probed = auth::decode_probe_reply(
        p_data.slice(REPLY_HEADER_LEN, p_data.size())
    );
    if (!probed.ok || probed.status != int(auth::ProbeStatus::OK)) {
        return false;
    }
    r_reply.instance = read_u64(p_data, 21);
    r_reply.gameplay_port
        = int64_t(uint8_t(p_data[29])) | (int64_t(uint8_t(p_data[30])) << 8);
    r_reply.info = NetwServerInfo::from_payload(probed.payload);
    return r_reply.gameplay_port > 0 && r_reply.info.is_valid();
}

} // namespace netw::connect::lan

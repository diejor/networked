#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/server_info.hpp"

namespace netw::connect::lan {

const int MAX_DATAGRAM = 1200;

struct Reply {
    uint64_t instance = 0;
    int64_t gameplay_port = 0;
    godot::Ref<NetwServerInfo> info;
};

godot::PackedByteArray encode_query(uint64_t p_app_tag, uint64_t p_nonce);
bool decode_query(
    const godot::PackedByteArray &p_data,
    uint64_t p_app_tag,
    uint64_t &r_nonce
);
godot::PackedByteArray encode_reply(
    uint64_t p_app_tag,
    uint64_t p_nonce,
    uint64_t p_instance,
    int64_t p_gameplay_port,
    const godot::Ref<NetwServerInfo> &p_info
);
bool decode_reply(
    const godot::PackedByteArray &p_data,
    uint64_t p_app_tag,
    uint64_t p_nonce,
    Reply &r_reply
);

} // namespace netw::connect::lan

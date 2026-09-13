#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::auth {

inline constexpr uint8_t PROTOCOL_VERSION = 4;
inline constexpr int HELLO_HEADER_LEN = 14;
inline constexpr int PROBE_HEADER_LEN = 6;

enum class Kind : int {
    UNKNOWN = 0,
    HELLO = 1,
    PROBE = 2,
};

enum class ProbeStatus : int {
    OK = 0,
    BUSY = 1,
    UNSUPPORTED = 2,
    ERROR = 3,
};

enum class Refusal : int {
    NONE = 0,
    FRAMING = 1,
    VERSION = 2,
    APP = 3,
};

struct Hello {
    Refusal refusal = Refusal::FRAMING;
    int version = 0;
    uint64_t app_tag = 0;
    int flags = 0;
    godot::PackedByteArray provider_payload;

    bool ok() const {
        return refusal == Refusal::NONE;
    }
};

struct ProbeRequest {
    bool ok = false;
    int version = 0;
    int flags = 0;
};

struct ProbeReply {
    bool ok = false;
    int version = 0;
    int status = 0;
    godot::PackedByteArray payload;
};

godot::PackedByteArray magic_hello();
godot::PackedByteArray magic_probe();

Kind classify(const godot::PackedByteArray &p_data);

godot::PackedByteArray encode_client_hello(
    const godot::PackedByteArray &p_provider_payload,
    uint64_t p_app_tag,
    int p_flags
);

Hello decode_client_hello(
    const godot::PackedByteArray &p_data,
    uint64_t p_local_app_tag
);

godot::PackedByteArray encode_probe_request(int p_flags);

ProbeRequest decode_probe_request(const godot::PackedByteArray &p_data);

godot::PackedByteArray encode_probe_reply(
    int p_status,
    const godot::PackedByteArray &p_payload
);

ProbeReply decode_probe_reply(const godot::PackedByteArray &p_data);

} // namespace netw::auth

#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::auth {

inline constexpr uint8_t PROTOCOL_VERSION = 6;
inline constexpr int TAG_LEN = 4;
inline constexpr int HELLO_HEADER_LEN = 18;
inline constexpr int HELLO_PROOF_CAPABLE = 1;
inline constexpr int HELLO_PROOF_CHALLENGE = 2;
inline constexpr int HELLO_GAME_AUTH = 4;
inline constexpr int CHALLENGE_BYTES = 16;
inline constexpr int CHALLENGE_LENGTH = 32;
inline constexpr int PROBE_HEADER_LEN = 6;
inline constexpr int ANSWER_HEADER_LEN = 6;
inline constexpr int REASON_CAP = 255;

enum class Kind : int {
    UNKNOWN = 0,
    HELLO = 1,
    PROBE = 2,
    GAME = 3,
    JOIN = 4,
    ANSWER = 5,
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

enum class AnswerKind : int {
    ADMITTED = 0,
    REFUSED = 1,
    CONFIRMED = 2,
};

struct Hello {
    Refusal refusal = Refusal::FRAMING;
    int version = 0;
    uint64_t app_tag = 0;
    int flags = 0;
    int64_t deadline_msec = 0;

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

struct Answer {
    bool ok = false;
    AnswerKind kind = AnswerKind::REFUSED;
    int64_t code = 0;
    godot::String reason;
    godot::PackedByteArray payload;
};

godot::PackedByteArray magic_hello();
godot::PackedByteArray magic_probe();

Kind classify(const godot::PackedByteArray &p_data);

godot::PackedByteArray encode_client_hello(
    uint64_t p_app_tag,
    int p_flags,
    int64_t p_deadline_msec = 0
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

godot::PackedByteArray encode_game(const godot::PackedByteArray &p_data);
godot::PackedByteArray encode_join(const godot::PackedByteArray &p_join);
godot::PackedByteArray payload_of(const godot::PackedByteArray &p_data);

godot::PackedByteArray encode_admitted(const godot::PackedByteArray &p_roster);
godot::PackedByteArray encode_confirmed();
godot::PackedByteArray encode_refused(
    int64_t p_code,
    const godot::String &p_reason
);
Answer decode_answer(const godot::PackedByteArray &p_data);

godot::String capped_reason(const godot::String &p_reason);

godot::String bind_challenge(
    const godot::String &p_challenge,
    const godot::String &p_server_identity
);

} // namespace netw::auth

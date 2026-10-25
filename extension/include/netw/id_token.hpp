#pragma once

#include <cstdint>

#include "godot/crypto.hpp"
#include "godot/hash_map.hpp"
#include "godot/variant.hpp"

namespace netw::id_token {

inline constexpr double CLOCK_SKEW_SECONDS = 60.0;
inline constexpr int64_t KEY_LIFETIME_MSEC = 3600000;
inline constexpr int64_t FETCH_COOLDOWN_MSEC = 30000;
inline constexpr int MAX_TOKEN_LENGTH = 16384;
inline constexpr int MAX_KEY_LENGTH = 2048;
inline constexpr int MAX_SIGNATURE_BYTES = 1024;
inline constexpr int MAX_KEYS_BYTES = 65536;
inline constexpr int64_t KEYS_RESULT_SUCCESS = 0;
inline constexpr int64_t KEYS_RESULT_NOT_SENT = -1;
inline constexpr double RENEW_SECONDS = 300.0;

using Keys = godot::HashMap<godot::String, godot::Ref<godot::CryptoKey>>;

struct Token {
    godot::String signed_part;
    godot::PackedByteArray signature;
    godot::Dictionary claims;
    godot::String kid;
};

struct Rules {
    godot::String issuer;
    godot::String audience;
    godot::String nonce;
    godot::String name_claim;
    double now = 0.0;
};

struct Proven {
    godot::String subject;
    godot::String name;
};

bool decode_base64url(const godot::String &p_text, godot::PackedByteArray &r);
godot::String encode_base64(const godot::PackedByteArray &p_bytes);
godot::String encode_base64url(const godot::PackedByteArray &p_bytes);

bool read_token(const godot::String &p_text, Token &r_token);
bool read_claims(
    const godot::Dictionary &p_claims,
    const Rules &p_rules,
    Proven &r_proven
);
godot::Dictionary claims_of(const godot::String &p_text);
double expiry_of(const godot::Dictionary &p_claims);
bool names_key(
    const godot::Dictionary &p_claims,
    const godot::String &p_public_pem
);

Keys read_keys(
    int64_t p_result,
    int64_t p_code,
    const godot::PackedByteArray &p_body
);

} // namespace netw::id_token

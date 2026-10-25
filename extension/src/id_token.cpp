#include "netw/id_token.hpp"

#include <cmath>

#include "godot/json.hpp"
#include "godot/utility.hpp"

using namespace godot;

namespace netw::id_token {

namespace {

constexpr char BASE64[]
    = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char BASE64URL[]
    = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

int base64url_value(char32_t p_char) {
    if (p_char >= 'A' && p_char <= 'Z') {
        return int(p_char - 'A');
    }
    if (p_char >= 'a' && p_char <= 'z') {
        return int(p_char - 'a') + 26;
    }
    if (p_char >= '0' && p_char <= '9') {
        return int(p_char - '0') + 52;
    }
    if (p_char == '-') {
        return 62;
    }
    return p_char == '_' ? 63 : -1;
}

String encoded_with(
    const PackedByteArray &p_bytes,
    const char *p_alphabet,
    bool p_padded
) {
    String out;
    const int64_t size = p_bytes.size();
    for (int64_t at = 0; at < size; at += 3) {
        const uint32_t left = uint32_t(p_bytes[at]) << 16;
        const uint32_t middle
            = at + 1 < size ? uint32_t(p_bytes[at + 1]) << 8 : 0;
        const uint32_t right = at + 2 < size ? uint32_t(p_bytes[at + 2]) : 0;
        const uint32_t group = left | middle | right;
        out += String::chr(p_alphabet[(group >> 18) & 63]);
        out += String::chr(p_alphabet[(group >> 12) & 63]);
        if (at + 1 < size) {
            out += String::chr(p_alphabet[(group >> 6) & 63]);
        } else if (p_padded) {
            out += "=";
        }
        if (at + 2 < size) {
            out += String::chr(p_alphabet[group & 63]);
        } else if (p_padded) {
            out += "=";
        }
    }
    return out;
}

bool json_of(const PackedByteArray &p_bytes, Variant &r_value) {
    Ref<JSON> parser;
    parser.instantiate();
    if (parser->parse(gd::utf8_text(p_bytes)) != OK) {
        return false;
    }
    r_value = parser->get_data();
    return true;
}

bool is_time(const Variant &p_value) {
    return p_value.get_type() == Variant::FLOAT
        && std::isfinite(double(p_value));
}

bool names_audience(const Variant &p_aud, const String &p_audience) {
    if (p_aud.get_type() == Variant::STRING) {
        return String(p_aud) == p_audience;
    }
    if (p_aud.get_type() != Variant::ARRAY) {
        return false;
    }
    const Array listed = p_aud;
    return listed.size() == 1 && listed[0].get_type() == Variant::STRING
        && String(listed[0]) == p_audience;
}

bool string_equals(const Variant &p_value, const String &p_expected) {
    return p_value.get_type() == Variant::STRING
        && String(p_value) == p_expected;
}

Ref<CryptoKey> rsa_key_of(const PackedByteArray &p_modulus) {
    const int64_t size = p_modulus.size();
    PackedByteArray der;
    const int64_t outer = size + 10;
    const int64_t inner = size + 1;
    const uint8_t head[] = {
        0x30, 0x82, uint8_t(outer >> 8), uint8_t(outer & 0xFF),
        0x02, 0x82, uint8_t(inner >> 8), uint8_t(inner & 0xFF), 0x00,
    };
    for (const uint8_t byte : head) {
        der.push_back(byte);
    }
    der.append_array(p_modulus);
    const uint8_t exponent[] = {0x02, 0x03, 0x01, 0x00, 0x01};
    for (const uint8_t byte : exponent) {
        der.push_back(byte);
    }
    const String pem = String("-----BEGIN RSA PUBLIC KEY-----\n")
        + encode_base64(der) + String("\n-----END RSA PUBLIC KEY-----\n");
    Ref<CryptoKey> key = gd::new_crypto_key();
    if (key.is_null() || key->load_from_string(pem, true) != OK) {
        return Ref<CryptoKey>();
    }
    return key;
}

} // namespace

bool decode_base64url(const String &p_text, PackedByteArray &r) {
    const int64_t length = p_text.length();
    if (length < 2 || length % 4 == 1) {
        return false;
    }
    r = PackedByteArray();
    uint32_t group = 0;
    int bits = 0;
    for (int64_t at = 0; at < length; ++at) {
        const int value = base64url_value(p_text[at]);
        if (value < 0) {
            return false;
        }
        group = (group << 6) | uint32_t(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            r.push_back(uint8_t((group >> bits) & 0xFF));
        }
    }
    return true;
}

String encode_base64(const PackedByteArray &p_bytes) {
    return encoded_with(p_bytes, BASE64, true);
}

String encode_base64url(const PackedByteArray &p_bytes) {
    return encoded_with(p_bytes, BASE64URL, false);
}

bool read_token(const String &p_text, Token &r_token) {
    if (p_text.length() > MAX_TOKEN_LENGTH) {
        return false;
    }
    const PackedStringArray parts = p_text.split(".");
    if (parts.size() != 3) {
        return false;
    }
    PackedByteArray header_bytes;
    PackedByteArray claims_bytes;
    if (!decode_base64url(parts[0], header_bytes)
        || !decode_base64url(parts[1], claims_bytes)
        || !decode_base64url(parts[2], r_token.signature)) {
        return false;
    }
    Variant header;
    Variant claims;
    if (!json_of(header_bytes, header) || !json_of(claims_bytes, claims)
        || header.get_type() != Variant::DICTIONARY
        || claims.get_type() != Variant::DICTIONARY) {
        return false;
    }
    const Dictionary fields = header;
    if (fields.has("crit") || fields.has("b64")
        || !string_equals(fields.get("alg", Variant()), String("RS256"))) {
        return false;
    }
    const Variant kid = fields.get("kid", String());
    if (kid.get_type() != Variant::STRING) {
        return false;
    }
    r_token.kid = kid;
    r_token.claims = claims;
    r_token.signed_part = parts[0] + String(".") + parts[1];
    return true;
}

bool read_claims(
    const Dictionary &p_claims,
    const Rules &p_rules,
    Proven &r_proven
) {
    const Variant subject = p_claims.get("sub", Variant());
    const Variant name = p_claims.get(p_rules.name_claim, String());
    if (!string_equals(p_claims.get("iss", Variant()), p_rules.issuer)
        || !string_equals(p_claims.get("nonce", Variant()), p_rules.nonce)
        || !names_audience(p_claims.get("aud", Variant()), p_rules.audience)
        || !string_equals(
            p_claims.get("azp", p_rules.audience), p_rules.audience
        )
        || subject.get_type() != Variant::STRING
        || String(subject).is_empty()
        || name.get_type() != Variant::STRING) {
        return false;
    }
    const Variant expiry = p_claims.get("exp", Variant());
    const Variant issued = p_claims.get("iat", Variant());
    const Variant not_before = p_claims.get("nbf", issued);
    if (!is_time(expiry) || !is_time(issued) || !is_time(not_before)) {
        return false;
    }
    if (double(expiry) + CLOCK_SKEW_SECONDS <= p_rules.now
        || double(issued) >= double(expiry)
        || std::fmax(double(issued), double(not_before))
            > p_rules.now + CLOCK_SKEW_SECONDS) {
        return false;
    }
    r_proven.subject = subject;
    r_proven.name = name;
    return true;
}

Dictionary claims_of(const String &p_text) {
    const PackedStringArray parts = p_text.split(".");
    PackedByteArray claims_bytes;
    Variant claims;
    if (parts.size() != 3 || !decode_base64url(parts[1], claims_bytes)
        || !json_of(claims_bytes, claims)
        || claims.get_type() != Variant::DICTIONARY) {
        return Dictionary();
    }
    return claims;
}

double expiry_of(const Dictionary &p_claims) {
    const Variant expiry = p_claims.get("exp", Variant());
    return is_time(expiry) ? double(expiry) : 0.0;
}

bool names_key(const Dictionary &p_claims, const String &p_public_pem) {
    return string_equals(
        p_claims.get("nonce", Variant()), p_public_pem.sha256_text()
    );
}

Keys read_keys(
    int64_t p_result,
    int64_t p_code,
    const PackedByteArray &p_body
) {
    Keys keys;
    Variant listed;
    if (p_result != KEYS_RESULT_SUCCESS || p_code != 200
        || p_body.size() > MAX_KEYS_BYTES || !json_of(p_body, listed)
        || listed.get_type() != Variant::DICTIONARY) {
        return keys;
    }
    const Variant found = Dictionary(listed).get("keys", Variant());
    if (found.get_type() != Variant::ARRAY) {
        return keys;
    }
    const Array entries = found;
    Array verify_only;
    verify_only.push_back(String("verify"));
    for (int64_t at = 0; at < entries.size(); ++at) {
        if (entries[at].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary jwk = entries[at];
        const Variant kid = jwk.get("kid", Variant());
        const Variant modulus = jwk.get("n", Variant());
        const Variant key_ops = jwk.get("key_ops", verify_only);
        PackedByteArray n;
        if (!string_equals(jwk.get("kty", Variant()), String("RSA"))
            || !string_equals(jwk.get("alg", String("RS256")), String("RS256"))
            || !string_equals(jwk.get("use", String("sig")), String("sig"))
            || key_ops.get_type() != Variant::ARRAY
            || Array(key_ops) != verify_only
            || !string_equals(jwk.get("e", Variant()), String("AQAB"))
            || kid.get_type() != Variant::STRING
            || modulus.get_type() != Variant::STRING
            || !decode_base64url(String(modulus), n)) {
            continue;
        }
        if (n.size() < 256 || n.size() > 1024 || n[0] < 128
            || n[n.size() - 1] % 2 == 0) {
            continue;
        }
        const Ref<CryptoKey> key = rsa_key_of(n);
        if (keys.has(String(kid)) || key.is_null()) {
            return Keys();
        }
        keys.insert(String(kid), key);
    }
    return keys;
}

} // namespace netw::id_token

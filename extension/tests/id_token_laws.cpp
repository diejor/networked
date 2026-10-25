#include "support/fault_log.h"
#include "support/netw_test.h"
#include "support/test_rsa_keys.h"

#include "godot/crypto.hpp"
#include "godot/json.hpp"
#include "godot/local_vector.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/id_token.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/promise.hpp"
#include "netw/id_token.hpp"

#include <initializer_list>
#include <utility>
#include <vector>

namespace TestNetwIdTokenLaws {

using namespace godot;
namespace id_token = netw::id_token;

constexpr const char *CHALLENGE = "0123456789abcdef0123456789abcdef";
constexpr const char *APP = "1561065725962092666";
constexpr const char *SUBJECT = "123456789012345678";
constexpr int64_t NOW = 1800000000;

Ref<CryptoKey> private_key(const char *p_pem) {
    Ref<CryptoKey> key = netw::gd::new_crypto_key();
    key->load_from_string(String(p_pem), false);
    return key;
}

Ref<CryptoKey> public_key(const Ref<CryptoKey> &p_private) {
    Ref<CryptoKey> key = netw::gd::new_crypto_key();
    key->load_from_string(p_private->save_to_string(true), true);
    return key;
}

Dictionary with(std::initializer_list<std::pair<const char *, Variant>> p) {
    Dictionary made;
    for (const std::pair<const char *, Variant> &field : p) {
        made[String(field.first)] = field.second;
    }
    return made;
}

Array listing(std::initializer_list<Variant> p_items) {
    Array made;
    for (const Variant &item : p_items) {
        made.push_back(item);
    }
    return made;
}

Dictionary merged(const Dictionary &p_base, const Dictionary &p_changes) {
    Dictionary made = p_base.duplicate();
    const Array keys = p_changes.keys();
    for (int64_t at = 0; at < keys.size(); ++at) {
        made[keys[at]] = p_changes[keys[at]];
    }
    return made;
}

String encoded_json(const Variant &p_value) {
    return id_token::encode_base64url(
        JSON::stringify(p_value).to_utf8_buffer()
    );
}

String signed_token(
    const Dictionary &p_header,
    const Dictionary &p_claims,
    const Ref<CryptoKey> &p_signer
) {
    const String part
        = encoded_json(p_header) + String(".") + encoded_json(p_claims);
    return part + String(".")
        + id_token::encode_base64url(
            netw::gd::rs256_sign(part.sha256_buffer(), p_signer)
        );
}

Array proof_of(
    const String &p_token,
    const Ref<CryptoKey> &p_proof_key,
    const String &p_signed
) {
    return listing({
        p_token,
        p_proof_key->save_to_string(true),
        netw::gd::rs256_sign(p_signed.sha256_buffer(), p_proof_key),
    });
}

PackedByteArray modulus_of(const Ref<CryptoKey> &p_key) {
    const PackedStringArray lines
        = p_key->save_to_string(true).strip_edges().split("\n");
    String body;
    for (int64_t at = 1; at + 1 < lines.size(); ++at) {
        body += lines[at];
    }
    body = body.replace("+", "-").replace("/", "_").replace("=", "");
    PackedByteArray der;
    id_token::decode_base64url(body, der);
    return der.slice(33, 33 + 256);
}

Dictionary jwk_of(const char *p_kid, const PackedByteArray &p_modulus) {
    return with({
        {"kid", String(p_kid)},
        {"kty", String("RSA")},
        {"alg", String("RS256")},
        {"use", String("sig")},
        {"n", id_token::encode_base64url(p_modulus)},
        {"e", String("AQAB")},
    });
}

PackedByteArray jwks_of(const Array &p_keys) {
    return JSON::stringify(with({{"keys", p_keys}})).to_utf8_buffer();
}

Ref<netw::NetwJoinRequest> request_for(const String &p_challenge) {
    Ref<netw::NetwJoinRequest> request;
    request.instantiate();
    request->bind_to(9, StringName("ana"), Array(), p_challenge, true);
    return request;
}

Dictionary discord_claims(const Ref<CryptoKey> &p_proof_key) {
    return with({
        {"iss", String("https://discord.com")},
        {"aud", String(APP)},
        {"nonce", p_proof_key->save_to_string(true).sha256_text()},
        {"sub", String(SUBJECT)},
        {"iat", int64_t(1)},
        {"exp", int64_t(4102444800)},
    });
}

Ref<netw::NetwIdToken> discord_token() {
    Ref<netw::NetwIdToken> token;
    token.instantiate();
    token->set_issuer("https://discord.com");
    token->set_audience(APP);
    token->set_prefix("discord");
    return token;
}

bool answered_ok(const Variant &p_answer) {
    return p_answer.get_type() == Variant::INT && int64_t(p_answer) == OK;
}

enum Twist {
    PLAIN,
    OTHER_SIGNER,
    TAMPERED,
    MALFORMED_JSON,
    RAW,
    ANOTHER_CHALLENGE,
    NOT_AN_ARRAY,
    DROP_EXP,
};

TEST_CASE(
    "[Networked][IdToken][Hosted] a token proves its account only when its "
    "header, every claim, its signature and the challenge signature hold"
) {
    const Ref<CryptoKey> issuer = private_key(netw_test::ISSUER_RSA_KEY);
    const Ref<CryptoKey> other = private_key(netw_test::OTHER_RSA_KEY);
    const Ref<CryptoKey> proof_key = private_key(netw_test::PROOF_RSA_KEY);
    const Dictionary header = with({
        {"alg", String("RS256")}, {"kid", String("fixture-rsa")},
    });
    const Dictionary claims = discord_claims(proof_key);
    struct Row {
        const char *label;
        Dictionary header;
        Dictionary claims;
        Twist twist;
        const char *raw;
        bool proves;
        const char *name;
    };
    const Dictionary none;
    const std::vector<Row> rows = {
        {"valid", none, none, PLAIN, "", true, ""},
        {"name", none, with({{"name", String("Fixture Player")}}), PLAIN, "",
         true, "Fixture Player"},
        {"nonce", none, with({{"nonce", String("another-host")}}), PLAIN, "",
         false, ""},
        {"audience", none, with({{"aud", String("another-application")}}),
         PLAIN, "", false, ""},
        {"audience type", none, with({{"aud", int64_t(1561065725962092666)}}),
         PLAIN, "", false, ""},
        {"audience listed alone", none,
         with({{"aud", listing({String(APP)})}}), PLAIN, "", true, ""},
        {"audience listed with another", none,
         with({{"aud", listing({String(APP), String("another-application")})},
               {"azp", String(APP)}}),
         PLAIN, "", false, ""},
        {"authorized party", none,
         with({{"azp", String("another-application")}}), PLAIN, "", false,
         ""},
        {"issuer", none, with({{"iss", String("https://discord.com/")}}),
         PLAIN, "", false, ""},
        {"subject empty", none, with({{"sub", String("")}}), PLAIN, "", false,
         ""},
        {"name not text", none, with({{"name", int64_t(5)}}), PLAIN, "",
         false, ""},
        {"missing exp", none, none, DROP_EXP, "", false, ""},
        {"algorithm", with({{"alg", String("HS256")}}), none, PLAIN, "", false,
         ""},
        {"unsigned", with({{"alg", String("none")}}), none, PLAIN, "", false,
         ""},
        {"unknown key", with({{"kid", String("rotated-key")}}), none, PLAIN,
         "", false, ""},
        {"key id not text", with({{"kid", int64_t(7)}}), none, PLAIN, "",
         false, ""},
        {"critical", with({{"crit", listing({String("extension")})}}), none,
         PLAIN, "", false, ""},
        {"unencoded payload", with({{"b64", false}}), none, PLAIN, "", false,
         ""},
        {"signature", none, none, OTHER_SIGNER, "", false, ""},
        {"tampered", none, none, TAMPERED, "", false, ""},
        {"malformed json", none, none, MALFORMED_JSON, "", false, ""},
        {"issued inside skew", none, with({{"iat", NOW + 30}}), PLAIN, "",
         true, ""},
        {"not before inside skew", none, with({{"nbf", NOW + 30}}), PLAIN, "",
         true, ""},
        {"expired inside skew", none,
         with({{"iat", NOW - 600}, {"exp", NOW - 30}}), PLAIN, "", true, ""},
        {"issued past skew", none, with({{"iat", NOW + 120}}), PLAIN, "",
         false, ""},
        {"not before past skew", none, with({{"nbf", NOW + 120}}), PLAIN, "",
         false, ""},
        {"expired past skew", none,
         with({{"iat", NOW - 600}, {"exp", NOW - 120}}), PLAIN, "", false,
         ""},
        {"empty", none, none, RAW, "", false, ""},
        {"two parts", none, none, RAW, "a.b", false, ""},
        {"four parts", none, none, RAW, "a.b.c.d", false, ""},
        {"another challenge", none, none, ANOTHER_CHALLENGE, "", false, ""},
        {"proof not an array", none, none, NOT_AN_ARRAY, "", false, ""},
    };
    const Ref<netw::NetwIdToken> token = discord_token();
    token->keys_answered(
        id_token::KEYS_RESULT_SUCCESS, 200,
        jwks_of(listing({jwk_of("fixture-rsa", modulus_of(issuer))})), 0
    );
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(label, row.label);
        CAPTURE(label);
        Dictionary row_claims = merged(claims, row.claims);
        if (row.twist == DROP_EXP) {
            row_claims.erase(String("exp"));
        }
        const String made = signed_token(
            merged(header, row.header), row_claims,
            row.twist == OTHER_SIGNER ? other : issuer
        );
        const PackedStringArray parts = made.split(".");
        String text = made;
        if (row.twist == TAMPERED) {
            text = parts[0] + String(".")
                + encoded_json(merged(claims, with({{"sub", String("999")}})))
                + String(".") + parts[2];
        } else if (row.twist == MALFORMED_JSON) {
            text = id_token::encode_base64url(
                       String("not json").to_utf8_buffer()
                   )
                + String(".") + parts[1] + String(".") + parts[2];
        } else if (row.twist == RAW) {
            text = String(row.raw);
        }
        const Variant proof = row.twist == NOT_AN_ARRAY
            ? Variant(netw::gd::var_to_bytes(
                  proof_of(text, proof_key, String(CHALLENGE))
              ))
            : Variant(proof_of(
                  text, proof_key,
                  String(row.twist == ANOTHER_CHALLENGE ? "another" : CHALLENGE)
              ));
        const Ref<netw::NetwJoinRequest> request
            = request_for(String(CHALLENGE));

        CHECK(answered_ok(token->verify_at(request, proof, NOW, 1)));
        const PackedStringArray proven = request->get_accounts();
        CHECK(proven.size() == (row.proves ? 1 : 0));
        if (row.proves && proven.size() == 1) {
            CHECK(bool(proven[0] == String("discord:") + SUBJECT));
            CHECK(bool(request->name_of(proven[0]) == String(row.name)));
        }
    }
}

TEST_CASE(
    "[Networked][IdToken][Hosted] a token with no key id is checked against "
    "the fixed key, and with no fixed key it proves nothing"
) {
    const Ref<CryptoKey> issuer = private_key(netw_test::ISSUER_RSA_KEY);
    const Ref<CryptoKey> other = private_key(netw_test::OTHER_RSA_KEY);
    const Ref<CryptoKey> proof_key = private_key(netw_test::PROOF_RSA_KEY);
    struct Shape {
        const char *iss;
        const char *aud;
        const char *prefix;
        const char *sub;
        const char *name;
        const char *instance;
    };
    const Shape nakama = {
        "netw-nakama", "my-game", "nakama",
        "2ca95571-c4b7-4258-8ff2-f49734f40b38", "ana", "",
    };
    const Shape worker = {
        "https://1561065725962092666.discordsays.com", APP, "discord",
        SUBJECT, "Ana", "i-1520000000000000000-gc-1-2",
    };
    struct Row {
        const char *label;
        const Shape *shape;
        bool fixed_key;
        const char *kid;
        bool signed_by_issuer;
        bool proves;
    };
    const Row rows[] = {
        {"fixed key, no key id", &nakama, true, "", true, true},
        {"fixed key ignores a key id", &nakama, true, "unlisted", true, true},
        {"fixed key, another signer", &nakama, true, "", false, false},
        {"no fixed key, no key id", &nakama, false, "", true, false},
        {"activity worker token, instance claim unread", &worker, true, "",
         true, true},
        {"activity worker token, another signer", &worker, true, "", false,
         false},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(label, row.label);
        CAPTURE(label);
        const Shape &shape = *row.shape;
        Dictionary claims = with({
            {"iss", String(shape.iss)},
            {"sub", String(shape.sub)},
            {"name", String(shape.name)},
            {"aud", String(shape.aud)},
            {"nonce", proof_key->save_to_string(true).sha256_text()},
            {"iat", NOW - 10},
            {"exp", NOW + 3590},
        });
        if (String(shape.instance).length() > 0) {
            claims["instance"] = String(shape.instance);
        }
        Ref<netw::NetwIdToken> token;
        token.instantiate();
        token->set_issuer(shape.iss);
        token->set_audience(shape.aud);
        token->set_prefix(shape.prefix);
        token->keys_answered(
            id_token::KEYS_RESULT_SUCCESS, 200,
            jwks_of(listing({jwk_of("listed", modulus_of(issuer))})), 0
        );
        if (row.fixed_key) {
            token->set_key(public_key(issuer));
        }
        Dictionary header = with({
            {"alg", String("RS256")}, {"typ", String("JWT")},
        });
        if (String(row.kid).length() > 0) {
            header["kid"] = String(row.kid);
        }
        const String made = signed_token(
            header, claims, row.signed_by_issuer ? issuer : other
        );
        const Ref<netw::NetwJoinRequest> request
            = request_for(String(CHALLENGE));

        CHECK(answered_ok(token->verify_at(
            request, proof_of(made, proof_key, String(CHALLENGE)), NOW, 1
        )));
        CHECK(request->get_accounts().size() == (row.proves ? 1 : 0));
        if (row.proves && request->get_accounts().size() == 1) {
            CHECK(bool(
                request->get_accounts()[0]
                == String(shape.prefix) + String(":") + String(shape.sub)
            ));
            CHECK(bool(request->get_account_name() == String()));
            CHECK(bool(request->name_of(request->get_accounts()[0])
                       == String(shape.name)));
        }
    }
}

TEST_CASE(
    "[Networked][IdToken][Hosted] a key set keeps only RSA signing keys with "
    "a sound modulus, and a duplicate key id or a failed answer keeps none"
) {
    const PackedByteArray modulus
        = modulus_of(private_key(netw_test::ISSUER_RSA_KEY));
    PackedByteArray low = modulus;
    low.set(0, 0x7F);
    PackedByteArray even = modulus;
    even.set(even.size() - 1, uint8_t(even[even.size() - 1] & 0xFE));
    PackedByteArray small;
    small.resize(128);
    small.fill(0xC1);
    const Dictionary jwk = jwk_of("k", modulus);
    Dictionary bare = jwk.duplicate();
    bare.erase(String("alg"));
    bare.erase(String("use"));
    struct Row {
        const char *label;
        int64_t result;
        int64_t code;
        PackedByteArray body;
        bool holds;
    };
    const auto one = [](const Dictionary &p_jwk) {
        return jwks_of(listing({p_jwk}));
    };
    const String filler = String("x").repeat(id_token::MAX_KEYS_BYTES);
    const std::vector<Row> rows = {
        {"valid", 0, 200, one(jwk), true},
        {"defaults for alg, use and key_ops", 0, 200, one(bare), true},
        {"not RSA", 0, 200, one(merged(jwk, with({{"kty", String("EC")}}))),
         false},
        {"another algorithm", 0, 200,
         one(merged(jwk, with({{"alg", String("HS256")}}))), false},
        {"encryption key", 0, 200,
         one(merged(jwk, with({{"use", String("enc")}}))), false},
        {"signing operation", 0, 200,
         one(merged(jwk, with({{"key_ops", listing({String("sign")})}}))),
         false},
        {"another exponent", 0, 200,
         one(merged(jwk, with({{"e", String("AAEAAQ")}}))), false},
        {"key id not text", 0, 200,
         one(merged(jwk, with({{"kid", int64_t(3)}}))), false},
        {"modulus padded", 0, 200,
         one(merged(jwk, with({{"n", String("AQAB==")}}))), false},
        {"modulus too short", 0, 200, one(jwk_of("k", small)), false},
        {"modulus leading bit clear", 0, 200, one(jwk_of("k", low)), false},
        {"modulus even", 0, 200, one(jwk_of("k", even)), false},
        {"other entries skipped", 0, 200,
         jwks_of(listing({String("junk"), jwk})), true},
        {"duplicate key id", 0, 200, jwks_of(listing({jwk, jwk})), false},
        {"no keys array", 0, 200,
         JSON::stringify(with({{"key", listing({jwk})}})).to_utf8_buffer(),
         false},
        {"server error", 0, 500, one(jwk), false},
        {"not received", 2, 0, one(jwk), false},
        {"over the size limit", 0, 200,
         JSON::stringify(with({{"keys", listing({jwk})}, {"pad", filler}}))
             .to_utf8_buffer(),
         false},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(label, row.label);
        CAPTURE(label);
        const id_token::Keys read
            = id_token::read_keys(row.result, row.code, row.body);
        CHECK(read.has(String("k")) == row.holds);
    }
}

class CountingIdToken : public netw::NetwIdToken {
public:
    int downloads = 0;

protected:
    Error request_keys() override {
        downloads += 1;
        return OK;
    }
};

int64_t ticks() {
    return int64_t(Time::get_singleton()->get_ticks_msec());
}

Ref<netw::NetwPromise> promise_in(const Variant &p_value) {
    return Ref<netw::NetwPromise>(
        Object::cast_to<netw::NetwPromise>(netw::gd::live_object(p_value))
    );
}

TEST_CASE(
    "[Networked][IdToken][Hosted] an unknown key id downloads the key set "
    "once, joins a download already running, and waits out the cooldown "
    "after a set that still lacks it"
) {
    const Ref<CryptoKey> issuer = private_key(netw_test::ISSUER_RSA_KEY);
    const Ref<CryptoKey> other = private_key(netw_test::OTHER_RSA_KEY);
    const Ref<CryptoKey> proof_key = private_key(netw_test::PROOF_RSA_KEY);
    const Ref<CountingIdToken> token(memnew(CountingIdToken));
    token->set_issuer("https://discord.com");
    token->set_audience(APP);
    token->set_prefix("discord");
    token->set_keys_url("https://keys.invalid/jwks");
    const Array listed = listing({jwk_of("fixture-rsa", modulus_of(issuer))});
    const Array rotated = listing({
        jwk_of("fixture-rsa", modulus_of(issuer)),
        jwk_of("rotated-key", modulus_of(other)),
    });
    token->keys_answered(0, 200, jwks_of(listed), ticks());
    const auto proof_by = [&](const char *p_kid) {
        return proof_of(
            signed_token(
                with({{"alg", String("RS256")}, {"kid", String(p_kid)}}),
                discord_claims(proof_key), other
            ),
            proof_key, String(CHALLENGE)
        );
    };

    const Ref<netw::NetwJoinRequest> first = request_for(String(CHALLENGE));
    const Ref<netw::NetwJoinRequest> second = request_for(String(CHALLENGE));
    const Ref<netw::NetwPromise> first_wait = promise_in(
        token->verify_at(first, proof_by("rotated-key"), NOW, ticks())
    );
    const Ref<netw::NetwPromise> second_wait = promise_in(
        token->verify_at(second, proof_by("rotated-key"), NOW, ticks())
    );
    NETW_CHECK_EQ(token->downloads, 1);
    token->keys_answered(0, 200, jwks_of(rotated), ticks());
    CHECK((first_wait.is_valid() && first_wait->get_is_completed()));
    CHECK((second_wait.is_valid() && second_wait->get_is_completed()));
    NETW_CHECK_EQ(first->get_accounts().size(), 1);
    NETW_CHECK_EQ(second->get_accounts().size(), 1);

    const Ref<netw::NetwJoinRequest> unknown = request_for(String(CHALLENGE));
    const Ref<netw::NetwPromise> unknown_wait = promise_in(
        token->verify_at(unknown, proof_by("unknown-key"), NOW, ticks())
    );
    NETW_CHECK_EQ(token->downloads, 2);
    token->keys_answered(0, 200, jwks_of(rotated), ticks());
    CHECK((unknown_wait.is_valid() && unknown_wait->get_is_completed()));
    NETW_CHECK_EQ(unknown->get_accounts().size(), 0);

    const Ref<netw::NetwJoinRequest> cooling = request_for(String(CHALLENGE));
    CHECK(answered_ok(
        token->verify_at(cooling, proof_by("unknown-key"), NOW, ticks())
    ));
    NETW_CHECK_EQ(token->downloads, 2);
    NETW_CHECK_EQ(cooling->get_accounts().size(), 0);

    const Ref<netw::NetwPromise> later = promise_in(token->verify_at(
        request_for(String(CHALLENGE)), proof_by("unknown-key"), NOW,
        ticks() + id_token::FETCH_COOLDOWN_MSEC + 1000
    ));
    CHECK(later.is_valid());
    NETW_CHECK_EQ(token->downloads, 3);
    token->keys_answered(0, 200, jwks_of(rotated), ticks());
}

TEST_CASE(
    "[Networked][IdToken][Hosted] a key download that fails keeps the "
    "trusted keys and warns once until a download succeeds"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][session] NetwIdToken got no usable signing keys from "
        "'https://keys.invalid/jwks'. Check NetwIdToken.keys_url and that "
        "this machine can reach it.",
        2
    );
    const Ref<CryptoKey> issuer = private_key(netw_test::ISSUER_RSA_KEY);
    const Ref<CryptoKey> proof_key = private_key(netw_test::PROOF_RSA_KEY);
    const Ref<CountingIdToken> token(memnew(CountingIdToken));
    token->set_issuer("https://discord.com");
    token->set_audience(APP);
    token->set_prefix("discord");
    token->set_keys_url("https://keys.invalid/jwks");
    const PackedByteArray listed
        = jwks_of(listing({jwk_of("fixture-rsa", modulus_of(issuer))}));
    token->keys_answered(0, 200, listed, 0);

    token->keys_answered(2, 0, PackedByteArray(), 0);
    token->keys_answered(0, 500, listed, 0);
    token->keys_answered(0, 200, jwks_of(Array()), 0);
    CHECK(token->holds_key(String("fixture-rsa")));
    const Ref<netw::NetwJoinRequest> request = request_for(String(CHALLENGE));
    const String made = signed_token(
        with({{"alg", String("RS256")}, {"kid", String("fixture-rsa")}}),
        discord_claims(proof_key), issuer
    );
    CHECK(answered_ok(token->verify_at(
        request, proof_of(made, proof_key, String(CHALLENGE)), NOW, 1
    )));
    NETW_CHECK_EQ(request->get_accounts().size(), 1);
    NETW_CHECK_EQ(token->downloads, 0);

    token->keys_answered(0, 200, listed, 0);
    token->keys_answered(2, 0, PackedByteArray(), 0);
}

class TokenDesk : public RefCounted {
public:
    enum Mode {
        MINT,
        REJECT,
        NOT_A_PROMISE,
        NOT_TEXT,
        ANOTHER_NONCE,
        LATER,
    };

    Mode mode = MINT;
    Ref<CryptoKey> signer;
    double expires = 0.0;
    PackedStringArray nonces;
    PackedStringArray minted;
    Ref<netw::NetwPromise> waiting;

    String mint(const String &p_nonce, double p_expires) const {
        return signed_token(
            with({{"alg", String("RS256")}, {"kid", String("desk")}}),
            with({
                {"iss", String("https://game.test")},
                {"aud", String(APP)},
                {"nonce", p_nonce},
                {"sub", String("desk-player")},
                {"iat", int64_t(NOW - 10)},
                {"exp", int64_t(p_expires)},
            }),
            signer
        );
    }

    Variant fetch(const String &p_nonce) {
        nonces.push_back(p_nonce);
        if (mode == NOT_A_PROMISE) {
            return String("not a promise");
        }
        if (mode == REJECT) {
            return netw::NetwPromise::rejected(ERR_UNAUTHORIZED, "refused");
        }
        if (mode == NOT_TEXT) {
            return netw::NetwPromise::resolved(int64_t(7));
        }
        const String token = mint(
            mode == ANOTHER_NONCE || mode == LATER ? String("another")
                                                   : p_nonce,
            expires
        );
        minted.push_back(token);
        if (mode == LATER) {
            waiting.instantiate();
            return waiting;
        }
        return netw::NetwPromise::resolved(token);
    }
};

Ref<TokenDesk> desk_for(const char *p_issuer, TokenDesk::Mode p_mode) {
    Ref<TokenDesk> desk(memnew(TokenDesk));
    desk->mode = p_mode;
    desk->signer = private_key(netw_test::ISSUER_RSA_KEY);
    desk->expires = double(NOW + 3600);
    netw::NetwIdToken::provide(
        String(p_issuer), callable_mp(desk.ptr(), &TokenDesk::fetch)
    );
    return desk;
}

Ref<netw::NetwIdToken> client_of(const char *p_issuer) {
    Ref<netw::NetwIdToken> token;
    token.instantiate();
    token->set_issuer(String(p_issuer));
    return token;
}

Variant settled(const Variant &p_value) {
    const Ref<netw::NetwPromise> waiting = promise_in(p_value);
    if (waiting.is_null()) {
        return p_value;
    }
    CHECK(waiting->get_is_settled());
    return waiting->get_is_completed() ? waiting->get_result() : Variant();
}

TEST_CASE(
    "[Networked][IdToken][Hosted] prove answers null without a source, or "
    "when the source refuses or answers anything but a token"
) {
    struct Row {
        const char *issuer;
        bool provided;
        TokenDesk::Mode mode;
    };
    const Row rows[] = {
        {"https://none.test", false, TokenDesk::MINT},
        {"https://refuse.test", true, TokenDesk::REJECT},
        {"https://promise.test", true, TokenDesk::NOT_A_PROMISE},
        {"https://text.test", true, TokenDesk::NOT_TEXT},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(issuer, row.issuer);
        CAPTURE(issuer);
        Ref<TokenDesk> desk;
        if (row.provided) {
            desk = desk_for(row.issuer, row.mode);
        }
        const Variant proof
            = settled(client_of(row.issuer)->prove_at(String(CHALLENGE), NOW));
        CHECK(proof.get_type() == Variant::NIL);
        if (desk.is_valid()) {
            NETW_CHECK_EQ(desk->nonces.size(), 1);
        }
    }
}

TEST_CASE(
    "[Networked][IdToken][Hosted] a proof is the source's token, a public "
    "key whose hash was the token's nonce, and that key's signature of the "
    "challenge, and a server with the issuer's key proves its account"
) {
    const Ref<TokenDesk> desk
        = desk_for("https://game.test", TokenDesk::MINT);
    const Variant proof = settled(
        client_of("https://game.test")->prove_at(String(CHALLENGE), NOW)
    );
    const Array parts
        = proof.get_type() == Variant::ARRAY ? Array(proof) : Array();
    CHECK(parts.size() == 3);
    CHECK(desk->minted.size() == 1);
    if (parts.size() != 3 || desk->minted.size() != 1) {
        return;
    }
    const String pem = parts[1];
    CHECK(bool(String(parts[0]) == desk->minted[0]));
    CHECK(bool(desk->nonces[0] == pem.sha256_text()));
    Ref<CryptoKey> proving = netw::gd::new_crypto_key();
    CHECK(proving->load_from_string(pem, true) == OK);
    CHECK(netw::gd::rs256_verify(
        String(CHALLENGE).sha256_buffer(), parts[2], proving
    ));

    Ref<netw::NetwIdToken> server;
    server.instantiate();
    server->set_issuer("https://game.test");
    server->set_audience(APP);
    server->set_prefix("game");
    server->set_key(public_key(desk->signer));
    const Ref<netw::NetwJoinRequest> request = request_for(String(CHALLENGE));
    CHECK(answered_ok(server->verify_at(request, proof, NOW, 1)));
    CHECK(bool(request->get_accounts()
               == PackedStringArray({"game:desk-player"})));
}

TEST_CASE(
    "[Networked][IdToken][Hosted] prove reuses its token and key while more "
    "than five minutes of the token remain, and renews both after that"
) {
    const Ref<TokenDesk> desk
        = desk_for("https://reuse.test", TokenDesk::MINT);
    const Ref<netw::NetwIdToken> client = client_of("https://reuse.test");
    const int64_t expires = NOW + 3600;
    struct Row {
        const char *label;
        int64_t now;
        int fetches;
    };
    const Row rows[] = {
        {"first", NOW, 1},
        {"soon after", NOW + 100, 1},
        {"just over five minutes left", expires - 301, 1},
        {"under five minutes left", expires - 299, 2},
    };
    String last_pem;
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(label, row.label);
        CAPTURE(label);
        const Variant proof
            = settled(client->prove_at(String(CHALLENGE), double(row.now)));
        CHECK(proof.get_type() == Variant::ARRAY);
        if (proof.get_type() != Variant::ARRAY) {
            return;
        }
        const String pem = Array(proof)[1];
        NETW_CHECK_EQ(desk->nonces.size(), row.fetches);
        CHECK(bool(desk->nonces[desk->nonces.size() - 1] == pem.sha256_text()));
        if (!last_pem.is_empty()) {
            CHECK(bool((pem == last_pem) == (row.fetches == 1)));
        }
        last_pem = pem;
    }
}

TEST_CASE(
    "[Networked][IdToken][Hosted] a later provide for an issuer replaces "
    "the earlier source, and an empty Callable removes it"
) {
    const Ref<TokenDesk> earlier
        = desk_for("https://replace.test", TokenDesk::MINT);
    const Ref<TokenDesk> later
        = desk_for("https://replace.test", TokenDesk::MINT);
    const Variant proof = settled(
        client_of("https://replace.test")->prove_at(String(CHALLENGE), NOW)
    );
    CHECK(proof.get_type() == Variant::ARRAY);
    NETW_CHECK_EQ(earlier->nonces.size(), 0);
    NETW_CHECK_EQ(later->nonces.size(), 1);

    const Ref<TokenDesk> removed
        = desk_for("https://removed.test", TokenDesk::MINT);
    netw::NetwIdToken::provide(String("https://removed.test"), Callable());
    const Variant none = settled(
        client_of("https://removed.test")->prove_at(String(CHALLENGE), NOW)
    );
    CHECK(none.get_type() == Variant::NIL);
    NETW_CHECK_EQ(removed->nonces.size(), 0);
}

TEST_CASE(
    "[Networked][IdToken][Hosted] prove uses a held token while more than "
    "five minutes of it remain, and a held token for another key is ignored"
) {
    struct Row {
        const char *issuer;
        int64_t left;
        bool matching_key;
        bool uses_held;
    };
    const Row rows[] = {
        {"https://held.test", 3600, true, true},
        {"https://held-late.test", 200, true, false},
        {"https://held-other.test", 3600, false, false},
    };
    const Ref<CryptoKey> proof_key = private_key(netw_test::PROOF_RSA_KEY);
    const String held_pem = proof_key->save_to_string(true);
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(issuer, row.issuer);
        CAPTURE(issuer);
        const Ref<TokenDesk> desk = desk_for(row.issuer, TokenDesk::MINT);
        const String held
            = desk->mint(held_pem.sha256_text(), double(NOW + row.left));
        netw::NetwIdToken::hold(
            String(row.issuer), held,
            row.matching_key ? proof_key
                             : private_key(netw_test::OTHER_RSA_KEY)
        );

        const Variant proof
            = settled(client_of(row.issuer)->prove_at(String(CHALLENGE), NOW));

        CHECK(proof.get_type() == Variant::ARRAY);
        if (proof.get_type() != Variant::ARRAY) {
            continue;
        }
        const Array parts = proof;
        NETW_CHECK_EQ(desk->nonces.size(), row.uses_held ? 0 : 1);
        CHECK(bool((String(parts[1]) == held_pem) == row.uses_held));
        CHECK(bool((String(parts[0]) == held) == row.uses_held));
    }
}

TEST_CASE(
    "[Networked][IdToken][Hosted] a fetched token whose nonce names another "
    "key is dropped, and prove answers with a token held meanwhile or null"
) {
    const Ref<CryptoKey> proof_key = private_key(netw_test::PROOF_RSA_KEY);
    const String held_pem = proof_key->save_to_string(true);

    const Ref<TokenDesk> stray
        = desk_for("https://stray.test", TokenDesk::ANOTHER_NONCE);
    const Variant dropped = settled(
        client_of("https://stray.test")->prove_at(String(CHALLENGE), NOW)
    );
    CHECK(dropped.get_type() == Variant::NIL);

    const Ref<TokenDesk> later
        = desk_for("https://stray-held.test", TokenDesk::LATER);
    const Variant asked = client_of("https://stray-held.test")
                              ->prove_at(String(CHALLENGE), NOW);
    netw::NetwIdToken::hold(
        String("https://stray-held.test"),
        later->mint(held_pem.sha256_text(), double(NOW + 3600)), proof_key
    );
    CHECK(later->waiting.is_valid());
    if (later->waiting.is_valid()) {
        later->waiting->resolve(later->minted[0]);
    }
    const Variant proof = settled(asked);
    CHECK(proof.get_type() == Variant::ARRAY);
    if (proof.get_type() == Variant::ARRAY) {
        CHECK(bool(String(Array(proof)[1]) == held_pem));
    }
}

} // namespace TestNetwIdTokenLaws

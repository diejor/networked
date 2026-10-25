#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/crypto.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/account_provider.hpp"
#include "netw/api/promise.hpp"
#include "netw/id_token.hpp"

namespace netw {

class NetwIdToken : public NetwAccountProvider {
    GDCLASS(NetwIdToken, NetwAccountProvider)

    godot::String issuer;
    godot::String audience;
    godot::String keys_url;
    godot::Ref<godot::CryptoKey> key;
    godot::String prefix;
    godot::String name_claim = "name";

    struct KeyWait {
        godot::Ref<NetwJoinRequest> request;
        godot::Variant proof;
        godot::Ref<NetwPromise> answer;
    };

    id_token::Keys keys;
    int64_t keys_expire_msec = 0;
    int64_t retry_msec = 0;
    bool keys_requested = false;
    bool fetch_fault_reported = false;
    godot::LocalVector<KeyWait> key_waits;
    godot::LocalVector<godot::Ref<NetwPromise>> key_promises;

    godot::Variant check(
        const godot::Ref<NetwJoinRequest> &p_request,
        const godot::Variant &p_proof,
        double p_now_unix,
        int64_t p_now_msec,
        bool p_may_fetch
    );
    void request_keys_once();
    void keys_completed(
        int64_t p_result,
        int64_t p_code,
        const godot::PackedStringArray &p_headers,
        const godot::PackedByteArray &p_body,
        const godot::Variant &p_request,
        const godot::Variant &p_keep
    );
    static void token_settled(const godot::String &p_issuer);

protected:
    static void _bind_methods();
    virtual godot::Error request_keys();

public:
    void set_issuer(const godot::String &p_issuer) { issuer = p_issuer; }
    godot::String get_issuer() const { return issuer; }
    void set_audience(const godot::String &p_audience) {
        audience = p_audience;
    }
    godot::String get_audience() const { return audience; }
    void set_keys_url(const godot::String &p_url) { keys_url = p_url; }
    godot::String get_keys_url() const { return keys_url; }
    void set_key(const godot::Ref<godot::CryptoKey> &p_key) { key = p_key; }
    godot::Ref<godot::CryptoKey> get_key() const { return key; }
    void set_prefix(const godot::String &p_prefix) { prefix = p_prefix; }
    godot::String get_prefix() const { return prefix; }
    void set_name_claim(const godot::String &p_claim) {
        name_claim = p_claim;
    }
    godot::String get_name_claim() const { return name_claim; }

    godot::Variant prove(
        const godot::String &p_challenge,
        const godot::Ref<NetwSession> &p_session
    ) override;
    godot::Variant verify(
        const godot::Ref<NetwJoinRequest> &p_request,
        const godot::Variant &p_proof
    ) override;
    godot::Ref<NetwPromise> fetch_keys();
    static void provide(
        const godot::String &p_issuer,
        const godot::Callable &p_fetch
    );
    static void hold(
        const godot::String &p_issuer,
        const godot::String &p_token,
        const godot::Ref<godot::CryptoKey> &p_key
    );
    static void forget_sources();

    godot::Variant prove_at(const godot::String &p_challenge, double p_now);
    godot::Variant verify_at(
        const godot::Ref<NetwJoinRequest> &p_request,
        const godot::Variant &p_proof,
        double p_now_unix,
        int64_t p_now_msec
    );
    void keys_answered(
        int64_t p_result,
        int64_t p_code,
        const godot::PackedByteArray &p_body,
        int64_t p_now_msec
    );
    bool holds_key(const godot::String &p_kid) const {
        return keys.has(p_kid);
    }
};

} // namespace netw

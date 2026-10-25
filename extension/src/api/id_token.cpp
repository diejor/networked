#include "netw/api/id_token.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

struct Source {
    Callable fetch;
    Ref<CryptoKey> key;
    String pem;
    String token;
    double expires = 0.0;
    Ref<NetwPromise> pending;
    Ref<CryptoKey> pending_key;
    String pending_pem;
    Array waits;
};

HashMap<String, Source> &sources() {
    static HashMap<String, Source> book;
    return book;
}

double unix_now() {
    return Time::get_singleton()->get_unix_time_from_system();
}

int64_t msec_now() {
    return int64_t(Time::get_singleton()->get_ticks_msec());
}

Variant proof_of(const Source &p_source, const String &p_challenge) {
    Array proof;
    proof.push_back(p_source.token);
    proof.push_back(p_source.pem);
    proof.push_back(gd::rs256_sign(p_challenge.sha256_buffer(), p_source.key));
    return proof;
}

Array wait_for(const String &p_challenge, const Ref<NetwPromise> &p_answer) {
    Array wait;
    wait.push_back(p_challenge);
    wait.push_back(p_answer);
    return wait;
}

void answer_waits(Source &r_source, bool p_fetched) {
    const Array waits = r_source.waits;
    r_source.waits = Array();
    for (int64_t at = 0; at < waits.size(); ++at) {
        const Array wait = waits[at];
        const Ref<NetwPromise> answer(
            Object::cast_to<NetwPromise>(gd::live_object(wait[1]))
        );
        answer->resolve(p_fetched ? proof_of(r_source, wait[0]) : Variant());
    }
}

} // namespace

Variant NetwIdToken::check(
    const Ref<NetwJoinRequest> &p_request,
    const Variant &p_proof,
    double p_now_unix,
    int64_t p_now_msec,
    bool p_may_fetch
) {
    const Variant answered = Variant(int64_t(OK));
    if (p_request.is_null() || p_proof.get_type() != Variant::ARRAY) {
        return answered;
    }
    const Array parts = p_proof;
    if (parts.size() != 3 || parts[0].get_type() != Variant::STRING
        || parts[1].get_type() != Variant::STRING
        || parts[2].get_type() != Variant::PACKED_BYTE_ARRAY) {
        return answered;
    }
    const String token_text = parts[0];
    const String key_text = parts[1];
    const PackedByteArray signature = parts[2];
    const String challenge = p_request->get_challenge();
    if (challenge.is_empty() || audience.is_empty() || key_text.is_empty()
        || key_text.length() > id_token::MAX_KEY_LENGTH
        || signature.size() > id_token::MAX_SIGNATURE_BYTES) {
        return answered;
    }
    id_token::Token token;
    id_token::Proven proven;
    const id_token::Rules
        rules{issuer, audience, key_text.sha256_text(), name_claim, p_now_unix};
    if (!id_token::read_token(token_text, token)
        || !id_token::read_claims(token.claims, rules, proven)) {
        return answered;
    }
    Ref<CryptoKey> signer = key;
    if (signer.is_null()) {
        if (token.kid.is_empty()) {
            return answered;
        }
        const bool stale
            = !keys.has(token.kid) || p_now_msec >= keys_expire_msec;
        if (stale && p_may_fetch && !keys_url.is_empty()
            && p_now_msec >= retry_msec) {
            Ref<NetwPromise> later;
            later.instantiate();
            key_waits.push_back(KeyWait{p_request, p_proof, later});
            request_keys_once();
            return later;
        }
        if (stale) {
            retry_msec = p_now_msec + id_token::FETCH_COOLDOWN_MSEC;
            return answered;
        }
        signer = keys[token.kid];
    }
    const Ref<CryptoKey> proof_key = gd::new_crypto_key();
    if (!gd::rs256_verify(
            token.signed_part.sha256_buffer(),
            token.signature,
            signer
        )
        || proof_key.is_null()
        || proof_key->load_from_string(key_text, true) != OK
        || !gd::rs256_verify(challenge.sha256_buffer(), signature, proof_key)) {
        return answered;
    }
    p_request->add_account(prefix + String(":") + proven.subject, proven.name);
    return answered;
}

Variant NetwIdToken::verify(
    const Ref<NetwJoinRequest> &p_request,
    const Variant &p_proof
) {
    return check(p_request, p_proof, unix_now(), msec_now(), true);
}

Variant NetwIdToken::verify_at(
    const Ref<NetwJoinRequest> &p_request,
    const Variant &p_proof,
    double p_now_unix,
    int64_t p_now_msec
) {
    return check(p_request, p_proof, p_now_unix, p_now_msec, true);
}

Ref<NetwPromise> NetwIdToken::fetch_keys() {
    if (keys_url.is_empty()) {
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            "NetwIdToken.keys_url is empty. Set it to the issuer's JWKS "
            "address."
        );
    }
    Ref<NetwPromise> waiting;
    waiting.instantiate();
    key_promises.push_back(waiting);
    request_keys_once();
    return waiting;
}

void NetwIdToken::request_keys_once() {
    if (keys_requested) {
        return;
    }
    keys_requested = true;
    if (request_keys() != OK) {
        keys_answered(
            id_token::KEYS_RESULT_NOT_SENT,
            0,
            PackedByteArray(),
            msec_now()
        );
    }
}

Error NetwIdToken::request_keys() {
    Node *root = gd::scene_root();
    if (root == nullptr) {
        return ERR_UNCONFIGURED;
    }
    Node *request = Object::cast_to<Node>(
        gd::live_object(gd::instantiate_class(StringName("HTTPRequest")))
    );
    if (request == nullptr) {
        return ERR_UNAVAILABLE;
    }
    request->set(StringName("timeout"), 10.0);
    request->set(StringName("body_size_limit"), id_token::MAX_KEYS_BYTES);
    request->set(StringName("max_redirects"), 0);
    root->add_child(request);
    request->connect(
        StringName("request_completed"),
        callable_mp(this, &NetwIdToken::keys_completed)
            .bind(Variant(request), Variant(Ref<NetwIdToken>(this)))
    );
    const Error sent
        = Error(int(request->call(StringName("request"), keys_url)));
    if (sent != OK) {
        request->queue_free();
    }
    return sent;
}

void NetwIdToken::keys_completed(
    int64_t p_result,
    int64_t p_code,
    const PackedStringArray &p_headers,
    const PackedByteArray &p_body,
    const Variant &p_request,
    const Variant &p_keep
) {
    Node *request = Object::cast_to<Node>(gd::live_object(p_request));
    if (request != nullptr) {
        request->queue_free();
    }
    keys_answered(p_result, p_code, p_body, msec_now());
}

void NetwIdToken::keys_answered(
    int64_t p_result,
    int64_t p_code,
    const PackedByteArray &p_body,
    int64_t p_now_msec
) {
    keys_requested = false;
    LocalVector<Ref<NetwPromise>> promises;
    promises = key_promises;
    key_promises.clear();
    LocalVector<KeyWait> waits;
    waits = key_waits;
    key_waits.clear();
    const id_token::Keys read = id_token::read_keys(p_result, p_code, p_body);
    if (read.is_empty() && !fetch_fault_reported) {
        fetch_fault_reported = true;
        NETW_WARN(
            sys::SESSION,
            "NetwIdToken got no usable signing keys from '%s'. Check "
            "NetwIdToken.keys_url and that this machine can reach it.",
            keys_url
        );
    }
    if (!read.is_empty()) {
        keys = read;
        keys_expire_msec = p_now_msec + id_token::KEY_LIFETIME_MSEC;
        retry_msec = 0;
        fetch_fault_reported = false;
    }
    for (const Ref<NetwPromise> &waiting : promises) {
        if (read.is_empty()) {
            waiting->reject(
                ERR_CANT_CONNECT,
                "The signing keys could not be downloaded."
            );
        } else {
            waiting->resolve(int64_t(OK));
        }
    }
    for (const KeyWait &wait : waits) {
        wait.answer->resolve(
            check(wait.request, wait.proof, unix_now(), p_now_msec, false)
        );
    }
}

Variant NetwIdToken::prove(
    const String &p_challenge,
    const Ref<NetwSession> &
) {
    return prove_at(p_challenge, unix_now());
}

Variant NetwIdToken::prove_at(const String &p_challenge, double p_now) {
    Source *source = sources().getptr(issuer);
    if (source == nullptr) {
        return Variant();
    }
    if (!source->token.is_empty()
        && source->expires - p_now > id_token::RENEW_SECONDS) {
        return proof_of(*source, p_challenge);
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    if (source->pending.is_valid() && !source->pending->get_is_settled()) {
        source->waits.push_back(wait_for(p_challenge, answer));
        return answer;
    }
    const Ref<CryptoKey> made = gd::new_rsa_key(2048);
    if (made.is_null()) {
        return Variant();
    }
    const String pem = made->save_to_string(true);
    bool called = false;
    const Variant returned
        = gd::call_checked(source->fetch, Variant(pem.sha256_text()), called);
    const Ref<NetwPromise> pending(
        Object::cast_to<NetwPromise>(gd::live_object(returned))
    );
    source = sources().getptr(issuer);
    if (!called || pending.is_null() || source == nullptr) {
        return Variant();
    }
    source->pending = pending;
    source->pending_key = made;
    source->pending_pem = pem;
    source->waits.push_back(wait_for(p_challenge, answer));
    pending->when_settled(
        callable_mp_static(&NetwIdToken::token_settled).bind(issuer)
    );
    return answer;
}

void NetwIdToken::token_settled(const String &p_issuer) {
    Source *source = sources().getptr(p_issuer);
    if (source == nullptr || source->pending.is_null()
        || !source->pending->get_is_settled()) {
        return;
    }
    const Variant token = source->pending->get_is_completed()
        ? source->pending->get_result()
        : Variant();
    const Dictionary claims = token.get_type() == Variant::STRING
        ? id_token::claims_of(token)
        : Dictionary();
    if (id_token::names_key(claims, source->pending_pem)) {
        source->token = token;
        source->key = source->pending_key;
        source->pem = source->pending_pem;
        source->expires = id_token::expiry_of(claims);
    }
    source->pending_key.unref();
    source->pending_pem = String();
    answer_waits(
        *source,
        !source->token.is_empty() && source->expires > unix_now()
    );
}

void NetwIdToken::hold(
    const String &p_issuer,
    const String &p_token,
    const Ref<CryptoKey> &p_key
) {
    const String pem
        = p_key.is_valid() ? p_key->save_to_string(true) : String();
    const Dictionary claims = id_token::claims_of(p_token);
    if (p_issuer.is_empty() || pem.is_empty()
        || !id_token::names_key(claims, pem)) {
        NETW_DEBUG(
            sys::SESSION,
            "NetwIdToken.hold ignored a token whose nonce does not name its "
            "key."
        );
        return;
    }
    Source &held = sources()[p_issuer];
    held.token = p_token;
    held.key = p_key;
    held.pem = pem;
    held.expires = id_token::expiry_of(claims);
}

void NetwIdToken::provide(const String &p_issuer, const Callable &p_fetch) {
    NETW_ERR_COND(
        p_issuer.is_empty() || (!p_fetch.is_null() && !p_fetch.is_valid()),
        sys::SESSION,
        "NetwIdToken.provide needs an issuer and a Callable that takes a "
        "nonce and returns a NetwPromise of a token."
    );
    Source *replaced = sources().getptr(p_issuer);
    if (replaced != nullptr) {
        answer_waits(*replaced, false);
    }
    if (p_fetch.is_null()) {
        sources().erase(p_issuer);
        return;
    }
    Source fresh;
    fresh.fetch = p_fetch;
    sources()[p_issuer] = fresh;
}

void NetwIdToken::forget_sources() {
    sources().clear();
}

void NetwIdToken::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_issuer", "issuer"),
        &NetwIdToken::set_issuer
    );
    ClassDB::bind_method(D_METHOD("get_issuer"), &NetwIdToken::get_issuer);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "issuer"),
        "set_issuer",
        "get_issuer"
    );
    ClassDB::bind_method(
        D_METHOD("set_audience", "audience"),
        &NetwIdToken::set_audience
    );
    ClassDB::bind_method(D_METHOD("get_audience"), &NetwIdToken::get_audience);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "audience"),
        "set_audience",
        "get_audience"
    );
    ClassDB::bind_method(
        D_METHOD("set_keys_url", "url"),
        &NetwIdToken::set_keys_url
    );
    ClassDB::bind_method(D_METHOD("get_keys_url"), &NetwIdToken::get_keys_url);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "keys_url"),
        "set_keys_url",
        "get_keys_url"
    );
    ClassDB::bind_method(D_METHOD("set_key", "key"), &NetwIdToken::set_key);
    ClassDB::bind_method(D_METHOD("get_key"), &NetwIdToken::get_key);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "key",
            PROPERTY_HINT_RESOURCE_TYPE,
            "CryptoKey"
        ),
        "set_key",
        "get_key"
    );
    ClassDB::bind_method(
        D_METHOD("set_prefix", "prefix"),
        &NetwIdToken::set_prefix
    );
    ClassDB::bind_method(D_METHOD("get_prefix"), &NetwIdToken::get_prefix);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "prefix"),
        "set_prefix",
        "get_prefix"
    );
    ClassDB::bind_method(
        D_METHOD("set_name_claim", "claim"),
        &NetwIdToken::set_name_claim
    );
    ClassDB::bind_method(
        D_METHOD("get_name_claim"),
        &NetwIdToken::get_name_claim
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "name_claim"),
        "set_name_claim",
        "get_name_claim"
    );
    ClassDB::bind_method(D_METHOD("fetch_keys"), &NetwIdToken::fetch_keys);
    ClassDB::bind_static_method(
        "NetwIdToken",
        D_METHOD("provide", "issuer", "fetch"),
        &NetwIdToken::provide
    );
    ClassDB::bind_static_method(
        "NetwIdToken",
        D_METHOD("hold", "issuer", "token", "key"),
        &NetwIdToken::hold
    );
}

} // namespace netw

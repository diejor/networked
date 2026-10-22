#include "netw/api/group_promise.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_PEER_COMPLETED = "peer_completed";
const char *SIG_SETTLED = "settled";

} // namespace

Ref<NetwGroupPromise> NetwGroupPromise::create(
    const PackedInt32Array &p_peers
) {
    Ref<NetwGroupPromise> out;
    out.instantiate();
    for (int at = 0; at < p_peers.size(); ++at) {
        out->expected.push_back(int64_t(p_peers[at]));
    }
    return out;
}

Ref<NetwGroupPromise> NetwGroupPromise::refused(
    Error p_code,
    const String &p_detail
) {
    Ref<NetwGroupPromise> out;
    out.instantiate();
    out->failed = true;
    out->code = p_code;
    out->detail = p_detail;
    return out;
}

bool NetwGroupPromise::forget(int64_t p_peer) {
    for (uint32_t at = 0; at < expected.size(); ++at) {
        if (expected[at] == p_peer) {
            expected.remove_at(at);
            return true;
        }
    }
    return false;
}

PackedInt32Array NetwGroupPromise::get_expected_peers() const {
    PackedInt32Array out;
    for (uint32_t at = 0; at < expected.size(); ++at) {
        out.push_back(int32_t(expected[at]));
    }
    return out;
}

Ref<NetwGroupPromise> NetwGroupPromise::then(const Callable &p_callback) {
    if (completed) {
        Array args;
        args.push_back(results);
        p_callback.callv(args);
    } else {
        then_callbacks.push_back(p_callback);
    }
    return Ref<NetwGroupPromise>(this);
}

Ref<NetwGroupPromise> NetwGroupPromise::catch_error(
    const Callable &p_callback
) {
    if (failed) {
        Array args;
        args.push_back(code);
        args.push_back(detail);
        p_callback.callv(args);
    } else {
        catch_callbacks.push_back(p_callback);
    }
    return Ref<NetwGroupPromise>(this);
}

void NetwGroupPromise::resolve_peer(int64_t p_peer, const Variant &p_value) {
    if (completed || failed) {
        return;
    }
    if (!forget(p_peer)) {
        return;
    }
    results[p_peer] = p_value;
    emit_signal(StringName(SIG_PEER_COMPLETED), p_peer, p_value);
    settle_when_answered();
}

void NetwGroupPromise::refuse_peer(
    int64_t p_peer,
    Error p_code,
    const String &p_detail
) {
    if (completed || failed || !forget(p_peer)) {
        return;
    }
    if (refusal == OK) {
        refusal = p_code;
        refusal_detail = p_detail;
    }
    settle_when_answered();
}

void NetwGroupPromise::remove_peer(int64_t p_peer) {
    if (completed || failed) {
        return;
    }
    if (forget(p_peer)) {
        settle_when_answered();
    }
}

void NetwGroupPromise::settle_when_answered() {
    if (!expected.is_empty()) {
        return;
    }
    if (refusal != OK) {
        reject(refusal, refusal_detail);
        return;
    }
    resolve_all();
}

Variant NetwGroupPromise::answer() const {
    return completed ? Variant(results) : Variant(code);
}

Signal NetwGroupPromise::wait() {
    return promise_wait::open(waiting, completed || failed, answer());
}

void NetwGroupPromise::resolve_all() {
    if (completed || failed) {
        return;
    }
    completed = true;
    emit_signal(StringName(SIG_SETTLED));
    const LocalVector<Callable> chained(then_callbacks);
    Array args;
    args.push_back(results);
    for (uint32_t at = 0; at < chained.size(); ++at) {
        chained[at].callv(args);
    }
    promise_wait::release(waiting, results);
}

void NetwGroupPromise::reject(Error p_code, const String &p_detail) {
    if (completed || failed) {
        return;
    }
    failed = true;
    code = p_code;
    detail = p_detail;
    if (catch_callbacks.is_empty() && waiting.is_empty()
        && !has_connections(StringName(SIG_SETTLED))) {
        NETW_WARN(
            sys::SESSION,
            "A NetwGroupPromise failed with error %d (%s) and nothing "
            "handled it. Attach NetwGroupPromise.catch_error or await "
            "NetwGroupPromise.wait.",
            p_code,
            p_detail
        );
    }
    emit_signal(StringName(SIG_SETTLED));
    const LocalVector<Callable> chained(catch_callbacks);
    Array args;
    args.push_back(p_code);
    args.push_back(p_detail);
    for (uint32_t at = 0; at < chained.size(); ++at) {
        chained[at].callv(args);
    }
    promise_wait::release(waiting, Variant(p_code));
}

void NetwGroupPromise::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwGroupPromise",
        D_METHOD("create", "peers"),
        &NetwGroupPromise::create
    );
    ClassDB::bind_method(D_METHOD("wait"), &NetwGroupPromise::wait);
    ClassDB::bind_method(D_METHOD("answer"), &NetwGroupPromise::answer);
    ClassDB::bind_method(D_METHOD("then", "cb"), &NetwGroupPromise::then);
    ClassDB::bind_method(
        D_METHOD("catch_error", "cb"),
        &NetwGroupPromise::catch_error
    );
    ClassDB::bind_method(
        D_METHOD("resolve_peer", "peer_id", "val"),
        &NetwGroupPromise::resolve_peer
    );
    ClassDB::bind_method(
        D_METHOD("remove_peer", "peer_id"),
        &NetwGroupPromise::remove_peer
    );
    ClassDB::bind_method(
        D_METHOD("resolve_all"),
        &NetwGroupPromise::resolve_all
    );
    ClassDB::bind_method(
        D_METHOD("reject", "err_code", "err_detail"),
        &NetwGroupPromise::reject,
        DEFVAL(String())
    );

    ClassDB::bind_method(
        D_METHOD("get_is_completed"),
        &NetwGroupPromise::get_is_completed
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_completed"),
        "",
        "get_is_completed"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_failed"),
        &NetwGroupPromise::get_is_failed
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_failed"), "", "get_is_failed");
    ClassDB::bind_method(
        D_METHOD("get_is_settled"),
        &NetwGroupPromise::get_is_settled
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_settled"),
        "",
        "get_is_settled"
    );
    ClassDB::bind_method(
        D_METHOD("get_results"),
        &NetwGroupPromise::get_results
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "results"),
        "",
        "get_results"
    );
    ClassDB::bind_method(
        D_METHOD("get_expected_peers"),
        &NetwGroupPromise::get_expected_peers
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_INT32_ARRAY, "expected_peers"),
        "",
        "get_expected_peers"
    );
    ClassDB::bind_method(D_METHOD("get_code"), &NetwGroupPromise::get_code);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "code"), "", "get_code");
    ClassDB::bind_method(D_METHOD("get_detail"), &NetwGroupPromise::get_detail);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "detail"), "", "get_detail");

    ADD_SIGNAL(MethodInfo(
        SIG_PEER_COMPLETED,
        PropertyInfo(Variant::INT, "peer_id"),
        PropertyInfo(
            Variant::NIL,
            "value",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT
        )
    ));
    ADD_SIGNAL(MethodInfo(SIG_SETTLED));
}

} // namespace netw

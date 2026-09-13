#include "netw/api/promise.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_COMPLETED = "completed";
const char *SIG_FAILED = "failed";
const char *SIG_SETTLED = "settled";
const char *SIG_READY = "ready";

} // namespace

Ref<NetwPromise> NetwPromise::resolved(const Variant &p_value) {
    Ref<NetwPromise> out;
    out.instantiate();
    out->completed = true;
    out->result = p_value;
    return out;
}

Ref<NetwPromise> NetwPromise::rejected(Error p_code, const String &p_detail) {
    Ref<NetwPromise> out;
    out.instantiate();
    out->failed = true;
    out->code = p_code;
    out->detail = p_detail;
    return out;
}

Ref<NetwPromise> NetwPromise::then(const Callable &callback) {
    if (completed) {
        Array args;
        args.push_back(result);
        callback.callv(args);
    } else {
        then_callbacks.push_back(callback);
    }
    return Ref<NetwPromise>(this);
}

Ref<NetwPromise> NetwPromise::catch_error(const Callable &callback) {
    if (failed) {
        Array args;
        args.push_back(code);
        args.push_back(detail);
        callback.callv(args);
    } else {
        catch_callbacks.push_back(callback);
    }
    return Ref<NetwPromise>(this);
}

Ref<NetwPromise> NetwPromise::when_settled(const Callable &callback) {
    if (!callback.is_valid()) {
        return Ref<NetwPromise>(this);
    }
    if (completed || failed) {
        callback.callv(Array());
        return Ref<NetwPromise>(this);
    }
    connect(StringName(SIG_SETTLED), callback, Object::CONNECT_ONE_SHOT);
    return Ref<NetwPromise>(this);
}

Variant NetwPromise::answer() const {
    return completed ? result : Variant(code);
}

Signal NetwPromise::wait() {
    if (completed || failed) {
        Callable(this, StringName("emit_signal"))
            .bind(StringName(SIG_READY), answer())
            .call_deferred();
    }
    return Signal(this, StringName(SIG_READY));
}

void NetwPromise::resolve(const Variant &value) {
    if (completed || failed) {
        return;
    }
    completed = true;
    result = value;
    emit_signal(StringName(SIG_COMPLETED), value);
    emit_signal(StringName(SIG_SETTLED));
    emit_signal(StringName(SIG_READY), value);
    const LocalVector<Callable> chained(then_callbacks);
    Array args;
    args.push_back(value);
    for (uint32_t index = 0; index < chained.size(); ++index) {
        chained[index].callv(args);
    }
}

void NetwPromise::reject(Error error_code, const String &error_detail) {
    if (completed || failed) {
        return;
    }
    failed = true;
    code = error_code;
    detail = error_detail;
    if (catch_callbacks.is_empty()
        && !has_connections(StringName(SIG_FAILED))) {
        NETW_WARN(
            sys::SESSION,
            "a promise was rejected with no error handler attached: %d %s",
            error_code,
            error_detail
        );
    }
    emit_signal(StringName(SIG_FAILED), error_code, error_detail);
    emit_signal(StringName(SIG_SETTLED));
    emit_signal(StringName(SIG_READY), error_code);
    const LocalVector<Callable> chained(catch_callbacks);
    Array args;
    args.push_back(error_code);
    args.push_back(error_detail);
    for (uint32_t index = 0; index < chained.size(); ++index) {
        chained[index].callv(args);
    }
}

void NetwPromise::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPromise",
        D_METHOD("resolved", "value"),
        &NetwPromise::resolved
    );
    ClassDB::bind_static_method(
        "NetwPromise",
        D_METHOD("rejected", "code", "detail"),
        &NetwPromise::rejected,
        DEFVAL(String())
    );
    ClassDB::bind_method(D_METHOD("then", "cb"), &NetwPromise::then);
    ClassDB::bind_method(
        D_METHOD("catch_error", "cb"),
        &NetwPromise::catch_error
    );
    ClassDB::bind_method(
        D_METHOD("when_settled", "cb"),
        &NetwPromise::when_settled
    );
    ClassDB::bind_method(D_METHOD("wait"), &NetwPromise::wait);
    ClassDB::bind_method(D_METHOD("answer"), &NetwPromise::answer);
    ClassDB::bind_method(D_METHOD("resolve", "val"), &NetwPromise::resolve);
    ClassDB::bind_method(
        D_METHOD("reject", "err_code", "err_detail"),
        &NetwPromise::reject,
        DEFVAL(String())
    );

    ClassDB::bind_method(
        D_METHOD("get_is_completed"),
        &NetwPromise::get_is_completed
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_completed"),
        "",
        "get_is_completed"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_failed"),
        &NetwPromise::get_is_failed
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_failed"), "", "get_is_failed");
    ClassDB::bind_method(
        D_METHOD("get_is_settled"),
        &NetwPromise::get_is_settled
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_settled"),
        "",
        "get_is_settled"
    );
    ClassDB::bind_method(D_METHOD("get_result"), &NetwPromise::get_result);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::NIL,
            "result",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_NIL_IS_VARIANT
        ),
        "",
        "get_result"
    );
    ClassDB::bind_method(D_METHOD("get_code"), &NetwPromise::get_code);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "code"), "", "get_code");
    ClassDB::bind_method(D_METHOD("get_detail"), &NetwPromise::get_detail);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "detail"), "", "get_detail");

    ADD_SIGNAL(MethodInfo(SIG_COMPLETED, PropertyInfo(Variant::NIL, "value")));
    ADD_SIGNAL(MethodInfo(
        SIG_FAILED,
        PropertyInfo(Variant::INT, "code"),
        PropertyInfo(Variant::STRING, "detail")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SETTLED));
    ADD_SIGNAL(MethodInfo(SIG_READY, PropertyInfo(Variant::NIL, "answer")));
}

} // namespace netw

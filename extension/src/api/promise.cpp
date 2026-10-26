#include "netw/api/promise.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/game_call.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_SETTLED = "settled";
const char *SIG_ANSWERED = "answered";

const GameCall PROMISE_THEN
    = {sys::SESSION, "The NetwPromise.then callback", "func(result)", 1};
const GameCall PROMISE_CATCH = {
    sys::SESSION,
    "The NetwPromise.catch_error callback",
    "func(code: Error, detail: String)",
    2
};
const GameCall PROMISE_SETTLED
    = {sys::SESSION, "The NetwPromise.when_settled callback", "func()", 0};

void answer_waiter(const Ref<RefCounted> &p_waiter, const Variant &p_answer) {
    p_waiter->emit_signal(StringName(SIG_ANSWERED), p_answer);
}

} // namespace

Signal promise_wait::open(
    Waiting &p_waiting,
    bool p_settled,
    const Variant &p_answer
) {
    Ref<RefCounted> waiter;
    waiter.instantiate();
    Dictionary carried;
    carried["name"] = String("answer");
    carried["type"] = int(Variant::NIL);
    Array arguments;
    arguments.push_back(carried);
    gd::add_user_signal(waiter.ptr(), StringName(SIG_ANSWERED), arguments);
    const Signal answered(waiter.ptr(), StringName(SIG_ANSWERED));
    if (p_settled) {
        callable_mp_static(&answer_waiter)
            .bind(waiter, p_answer)
            .call_deferred();
    } else {
        p_waiting.push_back(waiter);
    }
    return answered;
}

void promise_wait::release(Waiting &p_waiting, const Variant &p_answer) {
    Waiting released;
    released = p_waiting;
    p_waiting.clear();
    for (uint32_t at = 0; at < released.size(); ++at) {
        answer_waiter(released[at], p_answer);
    }
}

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

Ref<NetwPromise> NetwPromise::refused(Error p_code, const String &p_detail) {
    Ref<NetwPromise> out = rejected(p_code, p_detail);
    out->failure_read = true;
    return out;
}

Ref<NetwPromise> NetwPromise::then(const Callable &callback) {
    if (completed) {
        Array args;
        args.push_back(result);
        game_call(callback, args, PROMISE_THEN);
    } else {
        then_callbacks.push_back(callback);
    }
    return Ref<NetwPromise>(this);
}

Ref<NetwPromise> NetwPromise::catch_error(const Callable &callback) {
    failure_read = true;
    if (failed) {
        Array args;
        args.push_back(code);
        args.push_back(detail);
        game_call(callback, args, PROMISE_CATCH);
    } else {
        catch_callbacks.push_back(callback);
    }
    return Ref<NetwPromise>(this);
}

Ref<NetwPromise> NetwPromise::when_settled(const Callable &callback) {
    if (!callback.is_valid()) {
        return Ref<NetwPromise>(this);
    }
    failure_read = true;
    if (completed || failed) {
        game_call(callback, Array(), PROMISE_SETTLED);
        return Ref<NetwPromise>(this);
    }
    connect(StringName(SIG_SETTLED), callback, Object::CONNECT_ONE_SHOT);
    return Ref<NetwPromise>(this);
}

Variant NetwPromise::answer() const {
    failure_read = true;
    return completed ? result : Variant(code);
}

Signal NetwPromise::wait() {
    return promise_wait::open(waiting, completed || failed, answer());
}

void NetwPromise::_notification(int p_what) {
    if (p_what != NOTIFICATION_PREDELETE || !failed || failure_read
        || code == ERR_SKIP) {
        return;
    }
    NETW_WARN(
        sys::SESSION,
        "A NetwPromise failed with error %d (%s) and nothing handled it. "
        "Attach NetwPromise.catch_error or await NetwPromise.wait.",
        code,
        detail
    );
}

void NetwPromise::resolve(const Variant &value) {
    if (completed || failed) {
        return;
    }
    const Ref<NetwPromise> settling(this);
    completed = true;
    result = value;
    emit_signal(StringName(SIG_SETTLED));
    const LocalVector<Callable> chained(then_callbacks);
    Array args;
    args.push_back(value);
    for (uint32_t index = 0; index < chained.size(); ++index) {
        game_call(chained[index], args, PROMISE_THEN);
    }
    promise_wait::release(waiting, value);
}

void NetwPromise::reject(Error error_code, const String &error_detail) {
    if (completed || failed) {
        return;
    }
    const Ref<NetwPromise> settling(this);
    failed = true;
    code = error_code;
    detail = error_detail;
    if (has_connections(StringName(SIG_SETTLED))) {
        failure_read = true;
    }
    emit_signal(StringName(SIG_SETTLED));
    const LocalVector<Callable> chained(catch_callbacks);
    Array args;
    args.push_back(error_code);
    args.push_back(error_detail);
    for (uint32_t index = 0; index < chained.size(); ++index) {
        game_call(chained[index], args, PROMISE_CATCH);
    }
    promise_wait::release(waiting, Variant(error_code));
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

    ADD_SIGNAL(MethodInfo(SIG_SETTLED));
}

} // namespace netw

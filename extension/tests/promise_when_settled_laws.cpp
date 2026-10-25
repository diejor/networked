#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "netw/api/promise.hpp"
#include "support/netw_call_log.h"

namespace TestNetwPromiseWhenSettledLaws {

using namespace godot;
using netw::NetwPromise;
using netw_test::CallLog;

Ref<NetwPromise> &sole_holder() {
    static Ref<NetwPromise> held;
    return held;
}

bool &alive_after_release() {
    static bool alive = false;
    return alive;
}

void release_holder(int64_t p_promise) {
    sole_holder().unref();
    alive_after_release()
        = netw::gd::instance_from_id(ObjectID(uint64_t(p_promise))) != nullptr;
}

void release_on_resolve(const Variant &, int64_t p_promise) {
    release_holder(p_promise);
}

void release_on_reject(int64_t, const String &, int64_t p_promise) {
    release_holder(p_promise);
}

TEST_CASE(
    "[Networked][Session][Hosted] WS1 a promise that already settled runs the "
    "subscriber now, which is the whole reason a caller subscribes instead of "
    "awaiting: the settled edge it would await for has already gone by"
) {
    const CallLog answer;

    NetwPromise::resolved(Variant(7))->when_settled(answer.callable("done"));
    NETW_CHECK_EQ(answer.count("done"), 1);

    NetwPromise::rejected(ERR_UNAUTHORIZED, String())
        ->when_settled(answer.callable("done"));
    NETW_CHECK_EQ(answer.count("done"), 2);
}

TEST_CASE(
    "[Networked][Session][Hosted] WS2 a pending promise runs the subscriber on "
    "its resolve edge and never before, so a request answered later is "
    "answered once rather than answered early with nothing decided"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const CallLog answer;

    promise->when_settled(answer.callable("done"));
    NETW_CHECK_EQ(answer.count("done"), 0);

    promise->resolve(Variant(7));
    NETW_CHECK_EQ(answer.count("done"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] WS3 a rejection runs the subscriber too, "
    "which is what separates this from a then chain: a request refused by "
    "authority still owes its requester the answer that it was refused"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const CallLog answer;
    const CallLog completed;

    promise->then(completed.callable("then"));
    promise->when_settled(answer.callable("done"));
    promise->reject(ERR_UNAUTHORIZED, String("refused"));

    NETW_CHECK_EQ(answer.count("done"), 1);
    NETW_CHECK_EQ(completed.count("then"), 0);
    NETW_CHECK_EQ(promise->get_code(), ERR_UNAUTHORIZED);
}

TEST_CASE(
    "[Networked][Session][Hosted] WS4 the subscription is spent on the first "
    "settle, so a promise driven again answers the same requester no second "
    "time and one request never yields two answers"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();
    const CallLog answer;

    promise->when_settled(answer.callable("done"));
    promise->resolve(Variant(7));
    promise->resolve(Variant(9));
    promise->reject(ERR_UNAUTHORIZED, String());

    NETW_CHECK_EQ(answer.count("done"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] WS5 every settle edge carries no arguments "
    "and the chain answers the promise, so a subscriber reads the outcome off "
    "the promise it already holds and calls stay chainable either way"
) {
    Ref<NetwPromise> pending;
    pending.instantiate();
    const CallLog answer;

    CHECK(bool(pending->when_settled(answer.callable("done")) == pending));

    pending->reject(ERR_BUSY, String());

    NETW_CHECK_EQ(answer.count("done"), 1);
    CHECK(answer.args("done").is_empty());

    const Ref<NetwPromise> settled = NetwPromise::resolved(Variant());
    CHECK(bool(settled->when_settled(answer.callable("done")) == settled));
    CHECK(answer.args("done", 1).is_empty());
}

TEST_CASE(
    "[Networked][Session][Hosted] WS6 an invalid subscriber is refused rather "
    "than connected, because a dead edge on a promise nobody settles is a "
    "leak that reads as a request still waiting for an answer"
) {
    Ref<NetwPromise> promise;
    promise.instantiate();

    CHECK(bool(promise->when_settled(Callable()) == promise));
    CHECK_FALSE(promise->has_connections(StringName("settled")));

    promise->resolve(Variant(7));

    CHECK(promise->get_is_settled());
}

TEST_CASE(
    "[Networked][Session][Hosted] WS7 a promise whose last holder lets go "
    "inside its own callback lives until the settle returns, so a transport "
    "freed by the answer it delivered cannot free the promise mid-settle"
) {
    for (const bool rejects : {false, true}) {
        CAPTURE(rejects);
        Ref<NetwPromise> made;
        made.instantiate();
        const int64_t id = int64_t(uint64_t(netw::gd::instance_id(made.ptr())));
        NetwPromise *settling = made.ptr();
        if (rejects) {
            made->catch_error(callable_mp_static(&release_on_reject).bind(id));
        } else {
            made->then(callable_mp_static(&release_on_resolve).bind(id));
        }
        sole_holder() = made;
        made.unref();
        alive_after_release() = false;

        if (rejects) {
            settling->reject(ERR_CANT_CREATE, String("refused"));
        } else {
            settling->resolve(Variant(7));
        }

        CHECK(alive_after_release());
        CHECK(netw::gd::instance_from_id(ObjectID(uint64_t(id))) == nullptr);
    }
}

} // namespace TestNetwPromiseWhenSettledLaws

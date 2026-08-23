#include "support/netw_test.h"

#include "netw/api/promise.hpp"
#include "support/netw_call_log.h"

namespace TestNetwPromiseWhenSettledLaws {

using namespace godot;
using netw::NetwPromise;
using netw_test::CallLog;

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

} // namespace TestNetwPromiseWhenSettledLaws

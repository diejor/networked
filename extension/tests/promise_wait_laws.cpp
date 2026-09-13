#include "support/netw_test.h"

#include "netw/api/promise.hpp"
#include "support/netw_call_log.h"

namespace TestNetwPromiseWaitLaws {

using namespace godot;
using netw::NetwPromise;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Session][Hosted] W3 a settled promise answers its result "
    "when it completed and its code when it failed, so one channel carries "
    "both outcomes and a caller reads the answer without asking which "
    "happened first"
) {
    CHECK(NetwPromise::resolved(Variant(7))->answer() == Variant(7));
    CHECK(
        NetwPromise::rejected(ERR_UNAUTHORIZED, String())->answer()
        == Variant(int(ERR_UNAUTHORIZED))
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] W4a wait() on a settled promise notifies "
    "nobody during the call itself, so a caller that waits after the answer "
    "arrived still subscribes before the answer is delivered"
) {
    const CallLog answer;

    Ref<NetwPromise> promise;
    promise.instantiate();
    promise->connect(StringName("settled"), answer.callable("settled"));
    promise->connect(StringName("completed"), answer.callable("completed"));
    promise->then(answer.callable("then"));

    promise->resolve(Variant(7));
    NETW_CHECK_EQ(answer.count("settled"), 1);
    NETW_CHECK_EQ(answer.count("completed"), 1);
    NETW_CHECK_EQ(answer.count("then"), 1);

    promise->wait();
    promise->wait();

    NETW_CHECK_EQ(answer.count("settled"), 1);
    NETW_CHECK_EQ(answer.count("completed"), 1);
    NETW_CHECK_EQ(answer.count("then"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] W5 a pending promise emits ready once on "
    "its settle edge and every waiter is listening to that one emission, so "
    "two callers waiting on one answer are both answered"
) {
    const CallLog answer;

    Ref<NetwPromise> promise;
    promise.instantiate();
    promise->wait();
    promise->wait();
    promise->connect(StringName("ready"), answer.callable("first"));
    promise->connect(StringName("ready"), answer.callable("second"));

    NETW_CHECK_EQ(answer.count("first"), 0);
    NETW_CHECK_EQ(answer.count("second"), 0);

    promise->resolve(Variant(7));

    NETW_CHECK_EQ(answer.count("first"), 1);
    NETW_CHECK_EQ(answer.count("second"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] W6 a rejected promise reaches its waiters "
    "on the same channel a resolved one does, so a caller that only waits "
    "never hangs on the failing path"
) {
    const CallLog answer;

    Ref<NetwPromise> promise;
    promise.instantiate();
    promise->connect(StringName("ready"), answer.callable("ready"));

    promise->reject(ERR_TIMEOUT, String("gone"));

    NETW_CHECK_EQ(answer.count("ready"), 1);
    CHECK(promise->answer() == Variant(int(ERR_TIMEOUT)));
}

TEST_CASE(
    "[Networked][Session][Hosted] W7 wait() answers a Signal on the ready "
    "channel whether the promise is pending or settled, so the caller has "
    "one thing to await and never branches on which it got"
) {
    Ref<NetwPromise> pending;
    pending.instantiate();
    const Signal from_pending = pending->wait();
    CHECK(from_pending.get_name() == StringName("ready"));

    const Ref<NetwPromise> settled = NetwPromise::resolved(Variant(7));
    const Signal from_settled = settled->wait();
    CHECK(from_settled.get_name() == StringName("ready"));
}

} // namespace TestNetwPromiseWaitLaws

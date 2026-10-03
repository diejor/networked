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
    "nobody during the call itself and never settles it again, so a late "
    "waiter cannot wake a listener that already heard the answer"
) {
    const CallLog answer;

    Ref<NetwPromise> promise;
    promise.instantiate();
    promise->connect(StringName("settled"), answer.callable("settled"));
    promise->then(answer.callable("then"));

    promise->resolve(Variant(7));
    NETW_CHECK_EQ(answer.count("settled"), 1);
    NETW_CHECK_EQ(answer.count("then"), 1);

    promise->wait().connect(answer.callable("late"));
    promise->wait().connect(answer.callable("later"));

    NETW_CHECK_EQ(answer.count("settled"), 1);
    NETW_CHECK_EQ(answer.count("then"), 1);
    NETW_CHECK_EQ(answer.count("late"), 0);
    NETW_CHECK_EQ(answer.count("later"), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] W5 a pending promise answers every waiter "
    "once on its settle edge, and each waiter hears only its own answer"
) {
    const CallLog answer;

    Ref<NetwPromise> promise;
    promise.instantiate();
    promise->wait().connect(answer.callable("first"));
    promise->wait().connect(answer.callable("second"));

    NETW_CHECK_EQ(answer.count("first"), 0);
    NETW_CHECK_EQ(answer.count("second"), 0);

    promise->resolve(Variant(7));
    promise->resolve(Variant(9));

    NETW_CHECK_EQ(answer.count("first"), 1);
    NETW_CHECK_EQ(answer.count("second"), 1);
    CHECK(answer.args("first")[0] == Variant(7));
    CHECK(answer.args("second")[0] == Variant(7));
}

TEST_CASE(
    "[Networked][Session][Hosted] W6 a rejected promise answers its waiters "
    "with the code, so a caller that only waits never hangs on the failing "
    "path"
) {
    const CallLog answer;

    Ref<NetwPromise> promise;
    promise.instantiate();
    promise->wait().connect(answer.callable("waited"));

    promise->reject(ERR_TIMEOUT, String("gone"));

    NETW_CHECK_EQ(answer.count("waited"), 1);
    CHECK(answer.args("waited")[0] == Variant(int(ERR_TIMEOUT)));
}

TEST_CASE(
    "[Networked][Session][Hosted] W7 every wait() answers a signal of its "
    "own, pending or settled, so two waits can never resume each other"
) {
    Ref<NetwPromise> pending;
    pending.instantiate();
    const Signal one = pending->wait();
    const Signal two = pending->wait();
    CHECK_FALSE(one.get_object_id() == two.get_object_id());
    CHECK_FALSE(one.get_object_id() == pending->get_instance_id());

    const Ref<NetwPromise> settled = NetwPromise::resolved(Variant(7));
    const Signal three = settled->wait();
    const Signal four = settled->wait();
    CHECK_FALSE(three.get_object_id() == four.get_object_id());
}

const CallLog *reentry_log = nullptr;
NetwPromise *reentry_promise = nullptr;

void chain_while_settling() {
    reentry_promise->then(reentry_log->callable("reentered"));
}

TEST_CASE(
    "[Networked][Session][Hosted] W12 a callback registered while the promise "
    "settles runs once with the stored result, because the result is stored "
    "before anything is told"
) {
    const CallLog answer;
    Ref<NetwPromise> promise;
    promise.instantiate();
    reentry_log = &answer;
    reentry_promise = promise.ptr();
    promise->connect(
        StringName("settled"),
        callable_mp_static(&chain_while_settling)
    );

    promise->resolve(Variant(7));
    promise->resolve(Variant(9));

    NETW_CHECK_EQ(answer.count("reentered"), 1);
    CHECK(answer.args("reentered")[0] == Variant(7));
    reentry_log = nullptr;
    reentry_promise = nullptr;
}

} // namespace TestNetwPromiseWaitLaws

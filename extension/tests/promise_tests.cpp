#include "support/netw_test.h"

#include "netw/api/promise.hpp"
#include "support/netw_call_log.h"
#include "support/netw_recorder.h"

namespace TestNetwPromise {

using namespace godot;
using netw::NetwPromise;
using netw_test::CallLog;
using netw_test::Recorder;

Ref<NetwPromise> fresh() {
    Ref<NetwPromise> promise;
    promise.instantiate();
    return promise;
}

TEST_CASE("[Networked][Promise][Hosted] P1 a promise settles exactly once") {
    Ref<NetwPromise> promise = fresh();
    Recorder settles(
        promise.ptr(),
        Vector<StringName>({"completed", "failed", "settled"})
    );

    CHECK_FALSE(promise->get_is_settled());
    NETW_CHECK_EQ(promise->get_code(), int(OK));

    promise->resolve(7);

    CHECK(promise->get_is_completed());
    CHECK(promise->get_is_settled());
    CHECK_FALSE(promise->get_is_failed());
    NETW_CHECK_EQ(int(promise->get_result()), 7);

    promise->resolve(9);
    promise->reject(int(ERR_TIMEOUT), "late");

    NETW_CHECK_EQ(int(promise->get_result()), 7);
    NETW_CHECK_EQ(promise->get_code(), int(OK));
    CHECK_FALSE(promise->get_is_failed());
    NETW_CHECK_EQ(settles.count("completed"), 1);
    NETW_CHECK_EQ(settles.count("failed"), 0);
    NETW_CHECK_EQ(settles.count("settled"), 1);
    REQUIRE(settles.args("completed").size() == 1);
    NETW_CHECK_EQ(int(settles.args("completed")[0]), 7);
}

TEST_CASE(
    "[Networked][Promise][Hosted] P2 a rejection carries its code and its "
    "reason, and a second answer does not replace it"
) {
    Ref<NetwPromise> promise = fresh();
    CallLog log;
    promise->catch_error(log.callable("caught"));

    promise->reject(int(ERR_UNAUTHORIZED), "refused");

    CHECK(promise->get_is_failed());
    CHECK(promise->get_is_settled());
    CHECK_FALSE(promise->get_is_completed());
    NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
    CHECK(promise->get_detail() == String("refused"));
    NETW_CHECK_EQ(log.count("caught"), 1);

    promise->reject(int(ERR_TIMEOUT), "later");
    NETW_CHECK_EQ(promise->get_code(), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(log.count("caught"), 1);
}

TEST_CASE(
    "[Networked][Promise][Hosted] P3 chaining before and after a settle both "
    "run, and neither runs the other side"
) {
    Ref<NetwPromise> early = fresh();
    CallLog log;
    early->then(log.callable("early then"));
    early->catch_error(log.callable("early catch"));

    early->resolve(1);

    NETW_CHECK_EQ(log.count("early then"), 1);
    NETW_CHECK_EQ(log.count("early catch"), 0);

    early->then(log.callable("late then"));
    early->catch_error(log.callable("late catch"));

    NETW_CHECK_EQ(log.count("late then"), 1);
    NETW_CHECK_EQ(log.count("late catch"), 0);

    Ref<NetwPromise> broken = fresh();
    broken->then(log.callable("broken then"));
    broken->catch_error(log.callable("broken catch"));

    broken->reject(int(ERR_UNAVAILABLE), "");

    NETW_CHECK_EQ(log.count("broken then"), 0);
    NETW_CHECK_EQ(log.count("broken catch"), 1);
}

TEST_CASE(
    "[Networked][Promise][Hosted] P4 settled fires once on either outcome, "
    "after the outcome's own signal"
) {
    Ref<NetwPromise> kept = fresh();
    Recorder on_kept(
        kept.ptr(),
        Vector<StringName>({"completed", "failed", "settled"})
    );

    kept->resolve(3);
    kept->resolve(4);

    NETW_CHECK_EQ(on_kept.count("completed"), 1);
    NETW_CHECK_EQ(on_kept.count("settled"), 1);
    NETW_CHECK_EQ(on_kept.count("failed"), 0);
    REQUIRE(on_kept.order().size() == 2);
    CHECK(bool(on_kept.order()[0] == StringName("completed")));
    CHECK(bool(on_kept.order()[1] == StringName("settled")));

    Ref<NetwPromise> broken = fresh();
    Recorder on_broken(
        broken.ptr(),
        Vector<StringName>({"completed", "failed", "settled"})
    );

    broken->reject(int(ERR_TIMEOUT), "gone");

    NETW_CHECK_EQ(on_broken.count("failed"), 1);
    NETW_CHECK_EQ(on_broken.count("settled"), 1);
    NETW_CHECK_EQ(on_broken.count("completed"), 0);
    REQUIRE(on_broken.order().size() == 2);
    CHECK(bool(on_broken.order()[0] == StringName("failed")));
    CHECK(bool(on_broken.order()[1] == StringName("settled")));
}

TEST_CASE(
    "[Networked][Promise][Hosted] P5 a chain answers the promise it chained"
) {
    Ref<NetwPromise> promise = fresh();
    CallLog log;

    const Ref<NetwPromise> chained
        = promise->then(log.callable("a"))->catch_error(log.callable("b"));

    CHECK(chained == promise);
}

} // namespace TestNetwPromise

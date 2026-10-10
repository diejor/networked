#include "support/fault_log.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/promise.hpp"

namespace TestNetwPromiseUnhandledLaws {

using namespace godot;
using netw::NetwPromise;
using netw_test::FaultWatch;

const char *UNHANDLED = "nothing handled it";

int64_t warnings_while(void (*p_act)()) {
    const FaultWatch faults;
    p_act();
    return faults.warnings_containing(UNHANDLED);
}

Ref<NetwPromise> fresh() {
    Ref<NetwPromise> promise;
    promise.instantiate();
    return promise;
}

TEST_CASE(
    "[Networked][Promise] PU1 a promise rejected before its caller awaits it "
    "warns nothing once the caller does"
) {
    const int64_t warned = warnings_while([] {
        const Ref<NetwPromise> promise = fresh();
        promise->reject(ERR_TIMEOUT, "the host did not answer");
        promise->wait();
    });
    NETW_CHECK_EQ(warned, 0);
}

TEST_CASE(
    "[Networked][Promise] PU2 a failed promise freed with nobody reading its "
    "failure warns exactly once"
) {
    const int64_t rejected_then_freed = warnings_while([] {
        const Ref<NetwPromise> promise = fresh();
        promise->reject(ERR_TIMEOUT, "the host did not answer");
    });
    NETW_CHECK_EQ(rejected_then_freed, 1);

    const int64_t made_failed_then_freed = warnings_while([] {
        NetwPromise::rejected(ERR_CANT_CONNECT, "no route");
    });
    NETW_CHECK_EQ(made_failed_then_freed, 1);
}

TEST_CASE(
    "[Networked][Promise] PU3 reading is_failed, code or detail counts as "
    "handling the failure"
) {
    const int64_t read_failed = warnings_while([] {
        const Ref<NetwPromise> promise = fresh();
        promise->reject(ERR_TIMEOUT, "the host did not answer");
        (void)promise->get_is_failed();
    });
    NETW_CHECK_EQ(read_failed, 0);

    const int64_t read_code = warnings_while([] {
        (void)NetwPromise::rejected(ERR_CANT_CONNECT, "no route")->get_code();
    });
    NETW_CHECK_EQ(read_code, 0);

    const int64_t read_detail = warnings_while([] {
        (void)NetwPromise::rejected(ERR_CANT_CONNECT, "no route")->get_detail();
    });
    NETW_CHECK_EQ(read_detail, 0);
}

TEST_CASE(
    "[Networked][Promise] PU4 a promise cancelled with ERR_SKIP never warns"
) {
    const int64_t warned = warnings_while([] {
        const Ref<NetwPromise> promise = fresh();
        promise->reject(ERR_SKIP, "cancelled");
    });
    NETW_CHECK_EQ(warned, 0);
}

} // namespace TestNetwPromiseUnhandledLaws

#endif

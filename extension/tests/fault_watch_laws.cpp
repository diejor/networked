#include "support/netw_test.h"

#include "support/fault_log.h"

#include "netw/log.hpp"

namespace TestFaultWatchLaws {

using netw_test::FaultLedger;
using netw_test::FaultLine;
using netw_test::FaultWatch;

bool asserted_by_a_watch(const FaultLine &p_line) {
    FaultLedger &ledger = netw_test::fault_ledger();
    const std::lock_guard<std::mutex> held(ledger.lock);
    return ledger.asserted.count(p_line.serial) > 0;
}

TEST_CASE(
    "[Networked][Faults][Hosted] FW1 a fault one watch asserts is expected "
    "in the record of every watch that saw it, and a fault no watch asserts "
    "is not"
) {
    FaultWatch outer;
    {
        FaultWatch inner;
        NETW_WARN(netw::sys::TEST, "FW1 asserted by the inner watch");
        NETW_WARN(netw::sys::TEST, "FW1 left to the outer watch");
        NETW_CHECK_EQ(
            inner.warnings_containing("FW1 asserted by the inner watch"),
            int64_t(1)
        );
    }

    int seen = 0;
    for (const FaultLine &line : outer.log->recorded()) {
        if (line.text.contains("FW1 asserted by the inner watch")) {
            CHECK(asserted_by_a_watch(line));
            seen += 1;
        } else if (line.text.contains("FW1 left to the outer watch")) {
            CHECK_FALSE(asserted_by_a_watch(line));
            seen += 1;
        }
    }
    NETW_CHECK_EQ(seen, 2);

    NETW_CHECK_EQ(
        outer.warnings_containing("FW1 left to the outer watch"),
        int64_t(1)
    );
}

} // namespace TestFaultWatchLaws

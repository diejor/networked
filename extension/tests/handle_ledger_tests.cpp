#include "support/netw_test.h"

#include "netw/handle_ledger.hpp"

namespace TestNetwHandleLedger {

using namespace godot;
using netw::NetwHandleLedger;

TEST_CASE("[Networked][Handle][Hosted] Handle ledger ids are monotonic") {
    NetwHandleLedger held;
    NetwHandleLedger *const ledger = &held;
    RID previous;
    for (int64_t id = 1; id <= 256; id++) {
        const RID rid = ledger->rid_create();
        CHECK(rid.is_valid());
        CHECK(rid != previous);
        CHECK(ledger->rid_from_id(id) == rid);
        previous = rid;
    }
    CHECK(ledger->id_count() == 256);
}

TEST_CASE(
    "[Networked][Handle][Hosted] Handle ledger frees exactly one handle"
) {
    NetwHandleLedger held;
    NetwHandleLedger *const ledger = &held;
    const RID first = ledger->rid_create();
    const RID second = ledger->rid_create();

    CHECK(ledger->rid_free(first));
    CHECK_FALSE(ledger->rid_is_valid(first));
    CHECK_FALSE(ledger->rid_from_id(1).is_valid());
    CHECK(ledger->rid_is_valid(second));
    CHECK(ledger->rid_from_id(2) == second);
    CHECK(ledger->id_count() == 1);
    CHECK_FALSE(ledger->rid_free(first));
}

TEST_CASE("[Networked][Handle][Hosted] Handle ledgers reject foreign handles") {
    NetwHandleLedger first;
    NetwHandleLedger second;
    const RID foreign = first.rid_create();

    CHECK_FALSE(second.rid_is_valid(foreign));
    CHECK_FALSE(second.rid_free(foreign));
    CHECK(second.id_count() == 0);
}

TEST_CASE("[Networked][Handle][Hosted] Handle ledger clear restarts the kind") {
    NetwHandleLedger held;
    NetwHandleLedger *const ledger = &held;
    const RID first = ledger->rid_create();
    ledger->rid_create();

    ledger->clear();

    CHECK_FALSE(ledger->rid_is_valid(first));
    CHECK(ledger->id_count() == 0);
    const RID restarted = ledger->rid_create();
    CHECK(ledger->rid_from_id(1) == restarted);
}

} // namespace TestNetwHandleLedger

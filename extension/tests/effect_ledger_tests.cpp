#include "support/netw_test.h"

#include "netw/effect_ledger.hpp"
#include "support/netw_call_log.h"

#include "godot/callable.hpp"
#include "godot/variant.hpp"

namespace TestNetwEffectLedger {

using namespace godot;
using netw::NetwEffectLedger;
using netw_test::CallLog;

constexpr int64_t DEADLINE = 9;

Ref<NetwEffectLedger> fresh() {
    Ref<NetwEffectLedger> ledger;
    ledger.instantiate();
    return ledger;
}

StringName act(int p_slot) {
    return StringName(vformat("act__p__1__%d", p_slot));
}

// A revert that arms another key while the sweep that ran it is still walking.
// Nothing else distinguishes a sweep that collected its expired set up front
// from one that discards as it iterates.
class ArmingSink final : public CallableCustom {
    Ref<NetwEffectLedger> ledger;
    StringName armed_key;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    ArmingSink(
        const Ref<NetwEffectLedger> &p_ledger,
        const StringName &p_armed_key
    )
        : ledger(p_ledger), armed_key(p_armed_key),
          anchor(netw::gd::instance_id(p_ledger.ptr())) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("ArmingSink");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &ArmingSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &ArmingSink::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        ledger->arm(armed_key, Callable(), DEADLINE);
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

TEST_CASE("[Networked][Effect][Hosted] Adopt drops the revert unrun") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);
    ledger->arm(key, log.callable("revert"), DEADLINE);

    CHECK(ledger->watch(
        key,
        log.callable("confirmed"),
        log.callable("denied")
    ));

    ledger->adopt(key);

    NETW_CHECK_EQ(log.count("revert"), 0);
    NETW_CHECK_EQ(log.count("confirmed"), 1);
    NETW_CHECK_EQ(log.count("denied"), 0);
    CHECK_FALSE(ledger->pending(key));
    NETW_CHECK_EQ(ledger->count(), 0);
}

TEST_CASE("[Networked][Effect][Hosted] Discard runs the revert exactly once") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);
    ledger->arm(key, log.callable("revert"), DEADLINE);
    ledger->watch(key, log.callable("confirmed"), log.callable("denied"));

    ledger->discard(key);
    ledger->discard(key);

    NETW_CHECK_EQ(log.count("revert"), 1);
    NETW_CHECK_EQ(log.count("denied"), 1);
    NETW_CHECK_EQ(log.count("confirmed"), 0);
    CHECK(log.order() == Vector<StringName>({ "revert", "denied" }));
}

TEST_CASE("[Networked][Effect][Hosted] A resolved key is deaf to the other "
          "outcome") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);
    ledger->arm(key, log.callable("revert"), DEADLINE);
    ledger->watch(key, log.callable("confirmed"), log.callable("denied"));

    ledger->discard(key);
    ledger->adopt(key);

    NETW_CHECK_EQ(log.count("confirmed"), 0);
    NETW_CHECK_EQ(log.count("revert"), 1);
    NETW_CHECK_EQ(log.count("denied"), 1);
}

TEST_CASE("[Networked][Effect][Hosted] A watcher never outlives its own act") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);
    ledger->arm(key, Callable(), DEADLINE);
    ledger->watch(key, log.callable("confirmed"), log.callable("denied"));
    ledger->adopt(key);

    ledger->arm(key, Callable(), DEADLINE);
    ledger->discard(key);

    NETW_CHECK_EQ(log.count("confirmed"), 1);
    NETW_CHECK_EQ(log.count("denied"), 0);
}

TEST_CASE("[Networked][Effect][Hosted] A deadline expires at its own tick") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);
    ledger->arm(key, log.callable("revert"), DEADLINE);
    ledger->watch(key, Callable(), log.callable("denied"));

    ledger->sweep(DEADLINE - 1);

    NETW_CHECK_EQ(log.count("revert"), 0);
    CHECK(ledger->pending(key));

    ledger->sweep(DEADLINE);

    NETW_CHECK_EQ(log.count("revert"), 1);
    NETW_CHECK_EQ(log.count("denied"), 1);
    CHECK_FALSE(ledger->pending(key));
}

TEST_CASE("[Networked][Effect][Hosted] A watch on an unarmed key is refused") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);

    CHECK_FALSE(ledger->watch(
        key,
        log.callable("confirmed"),
        log.callable("denied")
    ));

    ledger->arm(key, Callable(), DEADLINE);
    ledger->adopt(key);

    NETW_CHECK_EQ(log.count("confirmed"), 0);
    NETW_CHECK_EQ(log.count("denied"), 0);
}

TEST_CASE("[Networked][Effect][Hosted] Re-arming replaces the pending revert") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    const StringName key = act(0);
    ledger->arm(key, log.callable("first"), DEADLINE);
    ledger->arm(key, log.callable("second"), DEADLINE);

    NETW_CHECK_EQ(ledger->count(), 1);

    ledger->discard(key);

    NETW_CHECK_EQ(log.count("first"), 0);
    NETW_CHECK_EQ(log.count("second"), 1);
}

TEST_CASE("[Networked][Effect][Hosted] A sweep never sweeps what its own "
          "revert armed") {
    Ref<NetwEffectLedger> ledger = fresh();
    const StringName expiring = act(0);
    const StringName armed = act(1);
    ledger->arm(
        expiring,
        Callable(memnew(ArmingSink(ledger, armed))),
        DEADLINE
    );

    ledger->sweep(DEADLINE);

    CHECK_FALSE(ledger->pending(expiring));
    CHECK(ledger->pending(armed));
    NETW_CHECK_EQ(ledger->count(), 1);
}

TEST_CASE("[Networked][Effect][Hosted] An empty key arms nothing") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;

    ledger->arm(StringName(), log.callable("revert"), DEADLINE);

    NETW_CHECK_EQ(ledger->count(), 0);
    CHECK_FALSE(ledger->watch(StringName(), Callable(), Callable()));

    ledger->sweep(DEADLINE);

    NETW_CHECK_EQ(log.count("revert"), 0);
}

TEST_CASE("[Networked][Effect][Hosted] Clear forgets every armed act") {
    Ref<NetwEffectLedger> ledger = fresh();
    CallLog log;
    ledger->arm(act(0), log.callable("revert"), DEADLINE);
    ledger->arm(act(1), log.callable("revert"), DEADLINE);

    NETW_CHECK_EQ(ledger->count(), 2);

    ledger->clear();

    NETW_CHECK_EQ(ledger->count(), 0);
    ledger->sweep(DEADLINE);

    NETW_CHECK_EQ(log.count("revert"), 0);
}

} // namespace TestNetwEffectLedger

// The redundancy an unreliable input lane heals with.
//
// Inputs are not interchangeable: tick 41 is not a stale tick 42, it is a
// different input the simulation still owes a step to. So a windowed lane
// repeats the range still in flight rather than retransmitting, and the two
// numbers that decide what "still in flight" means are the depth and the floor.
// Both failures they prevent look identical from the sender, which is why they
// need laws rather than reasoning.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/window_ring.hpp"

using namespace godot;

namespace TestNetwReplWindowRing {

using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::repl::WindowRing;
using netw::repl::WindowSample;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

WirePlan axis() {
    SchemaRecord record;
    record.name = godot::StringName("Input");
    SchemaCore::append_column(
        &record,
        godot::StringName("a"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow sample(const WirePlan &p_plan, uint64_t p_value) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, p_value);
    return row;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a send repeats every tick the far side has not "
    "confirmed"
) {
    const WirePlan plan = axis();
    WindowRing ring = WindowRing::open(4);
    for (int64_t tick = 40; tick <= 42; ++tick) {
        REQUIRE(ring.record(tick, sample(plan, uint64_t(tick))));
    }

    const LocalVector<WindowSample> pending = ring.pending();
    REQUIRE(pending.size() == 3);

    // Oldest first, so a receiver applies them in the order the simulation
    // stepped them rather than having to sort.
    NETW_CHECK_EQ(pending[0].tick, 40);
    NETW_CHECK_EQ(pending[1].tick, 41);
    NETW_CHECK_EQ(pending[2].tick, 42);
}

TEST_CASE("[Networked][Repl][Hosted] a confirmed tick stops being repeated") {
    const WirePlan plan = axis();
    WindowRing ring = WindowRing::open(4);
    for (int64_t tick = 40; tick <= 42; ++tick) {
        ring.record(tick, sample(plan, uint64_t(tick)));
    }

    ring.confirm(41);
    const LocalVector<WindowSample> pending = ring.pending();

    // Without the floor the window grows without bound and the lane pays for
    // every input it ever sent, forever.
    REQUIRE(pending.size() == 1);
    NETW_CHECK_EQ(pending[0].tick, 42);
    NETW_CHECK_EQ(ring.floor_tick(), 41);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a tick at or under the floor is not recorded "
    "again"
) {
    const WirePlan plan = axis();
    WindowRing ring = WindowRing::open(4);
    ring.record(40, sample(plan, 40));
    ring.confirm(41);

    // A late poll for a tick the receiver has already stepped costs bytes and
    // buys nothing, so it is refused rather than carried.
    CHECK_FALSE(ring.record(41, sample(plan, 41)));
    CHECK_FALSE(ring.record(39, sample(plan, 39)));
    NETW_CHECK_EQ(ring.held(), 0);

    CHECK(ring.record(42, sample(plan, 42)));
    NETW_CHECK_EQ(ring.held(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the window is bounded, and it is the oldest "
    "that goes"
) {
    const WirePlan plan = axis();
    WindowRing ring = WindowRing::open(2);
    for (int64_t tick = 40; tick <= 43; ++tick) {
        ring.record(tick, sample(plan, uint64_t(tick)));
    }

    const LocalVector<WindowSample> pending = ring.pending();
    REQUIRE(pending.size() == 2);

    // The oldest has had the most chances to be heard, so dropping it forfeits
    // the least. Dropping the newest would forfeit the tick the simulation is
    // actually waiting on.
    NETW_CHECK_EQ(pending[0].tick, 42);
    NETW_CHECK_EQ(pending[1].tick, 43);
}

TEST_CASE(
    "[Networked][Repl][Hosted] re-recording one tick replaces its row rather "
    "than repeating the tick"
) {
    const WirePlan plan = axis();
    WindowRing ring = WindowRing::open(4);
    ring.record(40, sample(plan, 1));
    ring.record(40, sample(plan, 2));

    // A pass that polls one tick twice must not put two rows for it in flight,
    // or a receiver applying both steps the same tick twice.
    REQUIRE(ring.held() == 1);
    const LocalVector<WindowSample> pending = ring.pending();
    NETW_CHECK_EQ(pending[0].row.read(plan.column(0), 0), 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a confirmation older than the floor does not "
    "reopen it"
) {
    const WirePlan plan = axis();
    WindowRing ring = WindowRing::open(4);
    ring.record(42, sample(plan, 42));
    ring.confirm(41);
    ring.confirm(30);

    // Acks arrive out of order on an unreliable lane. A floor that moved
    // backwards would resend inputs the receiver has already stepped.
    NETW_CHECK_EQ(ring.floor_tick(), 41);
    NETW_CHECK_EQ(ring.pending().size(), 1);
}

} // namespace TestNetwReplWindowRing

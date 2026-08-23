// The reliable lane, and the one thing its reliability buys.
//
// The diff is the masked lane's. What differs is WHEN a baseline may advance,
// and these exist to pin that difference in both directions, because getting
// it backwards is expensive one way and silent the other.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/repl/retained_lane.hpp"
#include "netw/api/schema_core.hpp"

using namespace godot;

namespace TestNetwReplRetainedLane {

using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::repl::RetainedLane;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

const int PEER = 7;

WirePlan pair_plan() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Retained");
    SchemaCore::append_column(record, godot::StringName("a"), SchemaCore::I16, 1);
    SchemaCore::append_column(record, godot::StringName("b"), SchemaCore::I16, 1);
    SchemaCore::fix(record);
    return WirePlan::compile(record);
}

CodeRow row_of(const WirePlan &p_plan, uint64_t a, uint64_t b) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, a);
    row.write(p_plan.column(1), 0, b);
    return row;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that holds nothing is sent every column"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    CHECK_FALSE(lane.knows(PEER));
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 1, 2)), plan.full_mask());
    CHECK(lane.knows(PEER));
}

TEST_CASE(
    "[Networked][Repl][Hosted] the send is the proof, so the next pass diffs "
    "against it with no ack"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    REQUIRE(lane.send(PEER, row_of(plan, 1, 2)) == plan.full_mask());

    // No ack, no round trip. Delivery here is ordered and guaranteed, so
    // waiting for one would keep every column sticky across a round trip and
    // the lane would send each change twice as a matter of course.
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 1, 9)), uint64_t(0b10));
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 1, 9)), uint64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a column is sent once, not until something "
    "confirms it"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    lane.send(PEER, row_of(plan, 1, 2));
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 5, 2)), uint64_t(0b01));

    // The masked lane would keep column 0 sticky here until an ack carried it.
    // This one must not: there is nothing to be unsure about, and a sticky
    // column on a reliable lane is a byte spent to say what was already said.
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 5, 2)), uint64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] each peer is diffed against its own row"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    lane.send(PEER, row_of(plan, 1, 2));

    // A peer that arrived later holds nothing, and the lane owes it the whole
    // row however caught up the others are.
    NETW_CHECK_EQ(lane.send(9, row_of(plan, 1, 2)), plan.full_mask());
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 1, 2)), uint64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves the lane is owed the whole "
    "row when it returns"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    lane.send(PEER, row_of(plan, 1, 2));
    REQUIRE(lane.knows(PEER));

    LocalVector<int> nobody;
    lane.retain(nobody);
    CHECK_FALSE(lane.knows(PEER));

    // Same values it last held. A lane that kept the row would send nothing
    // and the peer would present a state it was never handed.
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 1, 2)), plan.full_mask());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row that is not this plan's advances nothing"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    CodeRow foreign;

    // A refusal must not look like a caught-up peer, and it must not leave the
    // lane believing the peer holds something it was never sent.
    NETW_CHECK_EQ(lane.send(PEER, foreign), uint64_t(0));
    CHECK_FALSE(lane.knows(PEER));
    NETW_CHECK_EQ(lane.send(PEER, row_of(plan, 1, 2)), plan.full_mask());
}

} // namespace TestNetwReplRetainedLane

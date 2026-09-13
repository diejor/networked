#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/retained_lane.hpp"

using namespace godot;

namespace TestNetwReplRetainedLane {

using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::repl::RetainedLane;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

const int PEER = 7;

WirePlan pair_plan() {
    SchemaRecord record;
    record.name = godot::StringName("Retained");
    SchemaCore::append_column(
        &record,
        godot::StringName("a"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("b"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow row_of(const WirePlan &p_plan, uint64_t a, uint64_t b) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, a);
    row.write(p_plan.column(1), 0, b);
    return row;
}

uint64_t sent_mask(RetainedLane &p_lane, int p_peer, const CodeRow &p_row) {
    return p_lane.send(p_peer, p_row).mask;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that holds nothing is sent every column"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    CHECK_FALSE(lane.knows(PEER));
    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 1, 2)), plan.full_mask());
    CHECK(lane.knows(PEER));
}

TEST_CASE(
    "[Networked][Repl][Hosted] the send is the proof, so the next pass diffs "
    "against it with no ack"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    REQUIRE(sent_mask(lane, PEER, row_of(plan, 1, 2)) == plan.full_mask());

    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 1, 9)), uint64_t(0b10));
    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 1, 9)), uint64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a column is sent once, not until something "
    "confirms it"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    sent_mask(lane, PEER, row_of(plan, 1, 2));
    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 5, 2)), uint64_t(0b01));

    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 5, 2)), uint64_t(0));
}

TEST_CASE("[Networked][Repl][Hosted] each peer is diffed against its own row") {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    sent_mask(lane, PEER, row_of(plan, 1, 2));

    NETW_CHECK_EQ(sent_mask(lane, 9, row_of(plan, 1, 2)), plan.full_mask());
    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 1, 2)), uint64_t(0));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves the lane is owed the whole "
    "row when it returns"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    sent_mask(lane, PEER, row_of(plan, 1, 2));
    REQUIRE(lane.knows(PEER));

    LocalVector<int> nobody;
    lane.retain(nobody);
    CHECK_FALSE(lane.knows(PEER));

    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 1, 2)), plan.full_mask());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row that is not this plan's advances nothing"
) {
    const WirePlan plan = pair_plan();
    RetainedLane lane = RetainedLane::open(plan);
    CodeRow foreign;

    NETW_CHECK_EQ(sent_mask(lane, PEER, foreign), uint64_t(0));
    CHECK_FALSE(lane.knows(PEER));
    NETW_CHECK_EQ(sent_mask(lane, PEER, row_of(plan, 1, 2)), plan.full_mask());
}

} // namespace TestNetwReplRetainedLane

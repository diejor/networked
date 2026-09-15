#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/lane_set.hpp"

using namespace godot;

namespace TestNetwReplLaneSet {

using godot::Array;
using netw::SchemaCore;
using netw::repl::LaneSet;
using netw::repl::RowLane;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;

const int64_t ROUTE = 3;

SchemaRecord body() {
    SchemaRecord record;
    record.name = godot::StringName("Body");
    SchemaCore::append_column(
        &record,
        godot::StringName("x"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return record;
}

Array one(int64_t x) {
    Array out;
    out.push_back(x);
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] opening an address twice keeps the lane it "
    "already had"
) {
    LaneSet set;
    RowLane *first = set.open(ROUTE, 0, body());
    NETW_REQUIRE_EQ(first != nullptr, true);

    RowLane *again = set.open(ROUTE, 0, body());
    NETW_CHECK_EQ(set.size(), 1);
    const bool the_same_lane_answered_twice = again == first;
    CHECK(the_same_lane_answered_twice);
}

TEST_CASE(
    "[Networked][Repl][Hosted] one address is a route and a component, not a "
    "route"
) {
    LaneSet set;
    RowLane *root = set.open(ROUTE, 0, body());
    RowLane *child = set.open(ROUTE, 4, body());
    NETW_CHECK_EQ(set.size(), 2);
    const bool the_components_are_separate_lanes = root != child;
    CHECK(the_components_are_separate_lanes);

    CodeRow row;
    CHECK(root->gather(one(10), row));
    CHECK(child->gather(one(20), row));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a route that dies takes its lanes with it"
) {
    LaneSet set;
    set.open(ROUTE, 0, body());
    set.open(ROUTE, 4, body());
    set.open(ROUTE + 1, 0, body());
    NETW_CHECK_EQ(set.size(), 3);

    set.close_route(ROUTE);

    NETW_CHECK_EQ(set.size(), 1);
    const bool the_dead_root_is_gone = set.find(ROUTE, 0) == nullptr;
    CHECK(the_dead_root_is_gone);
    const bool the_dead_component_is_gone = set.find(ROUTE, 4) == nullptr;
    CHECK(the_dead_component_is_gone);
    const bool the_living_route_is_untouched
        = set.find(ROUTE + 1, 0) != nullptr;
    CHECK(the_living_route_is_untouched);

    RowLane *reborn = set.open(ROUTE, 0, body());
    const bool the_address_reopens_as_a_new_lane = reborn != nullptr;
    CHECK(the_address_reopens_as_a_new_lane);
    NETW_CHECK_EQ(set.size(), 2);
}

} // namespace TestNetwReplLaneSet

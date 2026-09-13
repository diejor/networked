// Every lane a session holds, and the two things only the set can be held to.
//
// A single lane cannot notice either of these. It sees one row and one set of
// peers, and both failures below look from inside a lane exactly like ordinary
// correct behaviour: a caught-up peer costing nothing, and a peer being sent a
// diff. What makes them wrong is which peer, and which entity.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/lane_set.hpp"

using namespace godot;

namespace TestNetwReplLaneSet {

using godot::Array;
using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::repl::LaneSet;
using netw::repl::RowLane;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;

const int PEER = 7;
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

// Brings `p_peer` up to date on `p_lane`, which is the state both laws below
// start from: a peer the lane believes is caught up.
void catch_up(RowLane *p_lane, int p_peer, int64_t p_value, uint16_t p_seq) {
    CodeRow row;
    REQUIRE(p_lane->gather(one(p_value), row));
    p_lane->mask_for(p_peer, row);
    p_lane->stage(p_peer, p_seq, row);
    p_lane->acknowledge(p_peer, p_seq, 0);
    REQUIRE(p_lane->knows(p_peer));
}

TEST_CASE(
    "[Networked][Repl][Hosted] opening an address twice keeps the lane it "
    "already had"
) {
    LaneSet set;
    RowLane *first = set.open(ROUTE, 0, body());
    REQUIRE(first != nullptr);
    catch_up(first, PEER, 10, 1);

    RowLane *again = set.open(ROUTE, 0, body());
    NETW_CHECK_EQ(set.size(), 1);

    // Replacing the lane would discard every peer's confirmed baseline, and
    // the next pass would send whole rows to peers that were caught up. It
    // reads as a hiccup rather than a fault, which is why it needs a law.
    CHECK(again->knows(PEER));
}

TEST_CASE(
    "[Networked][Repl][Hosted] one address is a route and a component, not a "
    "route"
) {
    LaneSet set;
    RowLane *root = set.open(ROUTE, 0, body());
    RowLane *child = set.open(ROUTE, 4, body());
    NETW_CHECK_EQ(set.size(), 2);
    CHECK(root != child);

    catch_up(root, PEER, 10, 1);
    CHECK(root->knows(PEER));

    // The components of one entity replicate separately, so being caught up on
    // one says nothing about the other.
    CHECK_FALSE(child->knows(PEER));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a route that dies takes its lanes with it"
) {
    LaneSet set;
    catch_up(set.open(ROUTE, 0, body()), PEER, 10, 1);
    catch_up(set.open(ROUTE, 4, body()), PEER, 20, 1);
    catch_up(set.open(ROUTE + 1, 0, body()), PEER, 30, 1);
    NETW_CHECK_EQ(set.size(), 3);

    set.close_route(ROUTE);

    // Routes are reused. A lane surviving its route would hand the next entity
    // at that address the baselines of the last one, and every peer would be
    // sent a diff against a row belonging to an entity that no longer exists.
    NETW_CHECK_EQ(set.size(), 1);
    CHECK(set.find(ROUTE, 0) == nullptr);
    CHECK(set.find(ROUTE, 4) == nullptr);
    REQUIRE(set.find(ROUTE + 1, 0) != nullptr);
    CHECK(set.find(ROUTE + 1, 0)->knows(PEER));

    // The address is reopened by a new entity and starts believing nothing.
    RowLane *reborn = set.open(ROUTE, 0, body());
    CHECK_FALSE(reborn->knows(PEER));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves is forgotten by every lane"
) {
    LaneSet set;
    catch_up(set.open(ROUTE, 0, body()), PEER, 10, 1);
    catch_up(set.open(ROUTE, 4, body()), PEER, 20, 1);
    catch_up(set.open(ROUTE + 1, 0, body()), PEER, 30, 1);

    LocalVector<int> nobody;
    set.retain(nobody);

    // Forgetting in the lane that noticed and not the rest is the failure this
    // exists to prevent: peer ids are reused, so a lane still holding the old
    // peer's baseline sends the NEW peer a diff against a row it never saw,
    // and nothing later corrects it because the lane believes it is caught up.
    CHECK_FALSE(set.find(ROUTE, 0)->knows(PEER));
    CHECK_FALSE(set.find(ROUTE, 4)->knows(PEER));
    CHECK_FALSE(set.find(ROUTE + 1, 0)->knows(PEER));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer still on the roster keeps what it holds"
) {
    LaneSet set;
    catch_up(set.open(ROUTE, 0, body()), PEER, 10, 1);

    LocalVector<int> still_here;
    still_here.push_back(PEER);
    set.retain(still_here);

    // The other half. A retain that forgot everyone would heal every peer
    // every pass, which is correct and costs the whole row forever.
    CHECK(set.find(ROUTE, 0)->knows(PEER));
}

} // namespace TestNetwReplLaneSet

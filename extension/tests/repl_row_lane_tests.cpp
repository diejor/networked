// The send lane, composed: what the order of its stages is worth.
//
// Each piece under a lane already has its own laws. What a lane adds is the
// ORDER, and the order is a contract rather than a convenience. These are the
// mistakes it exists to make impossible, each written as the sequence that
// would produce it.

#include "support/netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/row_lane.hpp"
#include "netw/table/schema_core.hpp"

namespace TestNetwReplRowLane {

using godot::Array;
using godot::LocalVector;
using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::repl::RowLane;
using netw::wire::CodeRow;

const int PEER = 7;
const int OTHER = 9;

Ref<SchemaRecord> body() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Body");
    SchemaCore::append_column(record, godot::StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(record, godot::StringName("y"), SchemaCore::I16, 1);
    SchemaCore::fix(record);
    return record;
}

Array pair(int64_t x, int64_t y) {
    Array out;
    out.push_back(x);
    out.push_back(y);
    return out;
}

TEST_CASE("[Networked][Repl][Hosted] a lane opens on its schema or not at all") {
    RowLane good = RowLane::open(body());
    CHECK(good.valid());

    Ref<SchemaRecord> variant;
    variant.instantiate();
    variant->name = godot::StringName("Loose");
    SchemaCore::append_column(
        variant,
        godot::StringName("anything"),
        SchemaCore::VARIANT,
        1
    );
    SchemaCore::fix(variant);

    // A self-describing column has no fixed width, so the lane has no plan and
    // says so at open rather than at the first send.
    RowLane loose = RowLane::open(variant);
    CHECK_FALSE(loose.valid());

    CodeRow row;
    ERR_PRINT_OFF;
    CHECK_FALSE(loose.gather(pair(1, 2), row));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a gather that fails leaves the row the caller "
    "would have sent"
) {
    RowLane lane = RowLane::open(body());
    CodeRow row;
    REQUIRE(lane.gather(pair(3, 4), row));
    const uint64_t kept = row.read(lane.plan().column(0), 0);

    // The caller reuses one row across passes, so a refused gather that
    // half-wrote it would send a mix of this pass and the last under a mask
    // that claims both columns are current.
    Array wrong;
    wrong.push_back(1);
    ERR_PRINT_OFF;
    CHECK_FALSE(lane.gather(wrong, row));
    ERR_PRINT_ON;
    NETW_CHECK_EQ(row.read(lane.plan().column(0), 0), kept);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the first send to a peer carries the whole row"
) {
    RowLane lane = RowLane::open(body());
    CodeRow row;
    REQUIRE(lane.gather(pair(10, 20), row));

    CHECK_FALSE(lane.knows(PEER));
    NETW_CHECK_EQ(lane.mask_for(PEER, row), lane.plan().full_mask());
}

TEST_CASE(
    "[Networked][Repl][Hosted] one polled row serves every recipient and each "
    "gets its own mask"
) {
    RowLane lane = RowLane::open(body());
    CodeRow first;
    REQUIRE(lane.gather(pair(10, 20), first));

    // PEER is brought up to date and OTHER is not, which is the ordinary state
    // of a lane: peers join at different moments.
    REQUIRE(lane.mask_for(PEER, first) == lane.plan().full_mask());
    lane.stage(PEER, 1, first);
    lane.acknowledge(PEER, 1);
    REQUIRE(lane.knows(PEER));

    CodeRow second;
    REQUIRE(lane.gather(pair(10, 21), second));

    // The row is polled ONCE and shared. Each peer's mask is its own, because
    // the diff is against what that peer holds rather than against the last
    // thing the lane sent anybody.
    const uint64_t caught_up = lane.mask_for(PEER, second);
    const uint64_t fresh = lane.mask_for(OTHER, second);
    NETW_CHECK_EQ(caught_up, uint64_t(0b10));
    NETW_CHECK_EQ(fresh, lane.plan().full_mask());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer that leaves and returns is told "
    "everything again"
) {
    RowLane lane = RowLane::open(body());
    CodeRow row;
    REQUIRE(lane.gather(pair(10, 20), row));
    REQUIRE(lane.mask_for(PEER, row) == lane.plan().full_mask());
    lane.stage(PEER, 1, row);
    lane.acknowledge(PEER, 1);
    REQUIRE(lane.knows(PEER));

    LocalVector<int> nobody;
    lane.retain(nobody);
    CHECK_FALSE(lane.knows(PEER));

    // Same values it was last confirmed to hold. A lane that kept the baseline
    // would answer an empty mask and the peer would present a scene it was
    // never sent.
    NETW_CHECK_EQ(lane.mask_for(PEER, row), lane.plan().full_mask());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a caught-up peer costs the pass nothing"
) {
    RowLane lane = RowLane::open(body());
    CodeRow row;
    REQUIRE(lane.gather(pair(10, 20), row));
    REQUIRE(lane.mask_for(PEER, row) == lane.plan().full_mask());
    lane.stage(PEER, 1, row);
    lane.acknowledge(PEER, 1);

    CodeRow same;
    REQUIRE(lane.gather(pair(10, 20), same));
    NETW_CHECK_EQ(lane.mask_for(PEER, same), uint64_t(0));

    // And nothing was staged for it, so the in-flight window did not grow for
    // a datagram that was never sent.
    NETW_CHECK_EQ(lane.in_flight(PEER), 0);
}

} // namespace TestNetwReplRowLane

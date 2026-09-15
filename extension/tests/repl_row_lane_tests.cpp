#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/row_lane.hpp"

using namespace godot;

namespace TestNetwReplRowLane {

using godot::Array;
using netw::SchemaCore;
using netw::repl::RowLane;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;

SchemaRecord body() {
    SchemaRecord record;
    record.name = godot::StringName("Body");
    SchemaCore::append_column(
        &record,
        godot::StringName("x"),
        SchemaCore::I16,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("y"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return record;
}

Array pair(int64_t x, int64_t y) {
    Array out;
    out.push_back(x);
    out.push_back(y);
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a lane opens on its schema or not at all"
) {
    RowLane good = RowLane::open(body());
    CHECK(good.valid());

    SchemaRecord variant;
    variant.name = godot::StringName("Loose");
    SchemaCore::append_column(
        &variant,
        godot::StringName("anything"),
        SchemaCore::VARIANT,
        1
    );
    SchemaCore::fix(&variant);

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

} // namespace TestNetwReplRowLane

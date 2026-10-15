#include "support/fault_log.h"
#include "support/netw_test.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/value_row.hpp"
#include "support/books.h"
#include "support/netw_call_log.h"
#include "support/store_arrival.h"

namespace TestNetwReceiveTransaction {

using namespace godot;
using netw::SchemaCore;
using netw::enums::NetwMultiplayer;
using netw::repl::PropertyBinding;
using netw::repl::PropertyBindingPtr;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw::store::Landing;
using netw::wire::CodeRow;
using netw::wire::WirePlan;
using netw_test::StoreArrival;

const uint64_t TOKEN = 41;
const uint64_t COLUMN_POSITION = 1;
const uint64_t COLUMN_ROTATION = 2;
const uint64_t COLUMN_BOTH = COLUMN_POSITION | COLUMN_ROTATION;

uint64_t pose_set(netw::repl::SetBook &r_sets) {
    const uint64_t declared = r_sets.create();
    PropertySet *set = r_sets.get(declared);
    PropertyColumn position = PropertyColumn::create(
        StringName("position"),
        Ref<netw::NetwQuantize>(),
        false,
        NetwMultiplayer::ColumnType(SchemaCore::VECTOR2)
    );
    position.lane = PropertySet::VOLATILE;
    set->bind_column(position);
    PropertyColumn rotation = PropertyColumn::create(
        StringName("rotation"),
        Ref<netw::NetwQuantize>(),
        false,
        NetwMultiplayer::ColumnType(SchemaCore::F32)
    );
    rotation.lane = PropertySet::VOLATILE;
    set->bind_column(rotation);
    return declared;
}

Array pose_values(const Vector2 &p_position, double p_rotation) {
    Array out;
    out.push_back(p_position);
    out.push_back(p_rotation);
    return out;
}

CodeRow pose_row(
    PropertySet *p_set,
    const Vector2 &p_position,
    double p_rotation
) {
    const netw::table::SchemaRecord &schema = p_set->get_volatile_schema();
    const WirePlan plan = WirePlan::compile(schema);
    CodeRow row = CodeRow::for_plan(plan);
    netw::wire::encode_scalar_row(
        schema,
        pose_values(p_position, p_rotation),
        row
    );
    return row;
}

PackedByteArray pose_frame(
    PropertySet *p_set,
    const Vector2 &p_position,
    double p_rotation,
    uint64_t p_mask,
    uint64_t p_revision = 1,
    const CodeRow *p_baseline = nullptr
) {
    const netw::table::SchemaRecord &schema = p_set->get_volatile_schema();
    const WirePlan plan = WirePlan::compile(schema);
    const CodeRow row = pose_row(p_set, p_position, p_rotation);
    netw::repl::SnapshotHeader header;
    header.token = TOKEN;
    header.revision = p_revision;
    header.distance = p_baseline == nullptr ? 0 : p_revision - 1;
    header.tick = -1;
    header.reconcile_ack = -1;
    header.mask = p_baseline == nullptr ? plan.full_mask() : p_mask;
    return netw::repl::write_snapshot_row(header, -1, plan, row, p_baseline);
}

bool settle(StoreArrival &p_arrival, const PackedByteArray &p_frame) {
    return p_arrival.settle(p_frame, -1, false);
}

Vector2 row_position(StoreArrival &p_arrival) {
    return p_arrival.value(StringName("position"));
}

double row_rotation(StoreArrival &p_arrival) {
    return p_arrival.value(StringName("rotation"));
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-1 a row that never lands never reaches "
    "the header of the next row that does"
) {
    netw_test::Books books;
    const uint64_t pose = pose_set(books.sets);
    PropertySet *const set = books.sets.get(pose);
    Node2D *dst = memnew(Node2D);
    StoreArrival arrival(books.bindings.create(pose, dst));

    const CodeRow first = pose_row(set, Vector2(1.0, 1.0), 1.0);
    const bool committed
        = settle(arrival, pose_frame(set, Vector2(1.0, 1.0), 1.0, COLUMN_BOTH));
    CHECK(committed);
    CHECK(row_position(arrival) == Vector2(1.0, 1.0));

    const PackedByteArray denied
        = pose_frame(set, Vector2(2.0, 2.0), 1.0, COLUMN_POSITION, 2, &first);
    const bool the_denied_row_was_written = !denied.is_empty();
    CHECK(the_denied_row_was_written);

    const bool next = settle(
        arrival,
        pose_frame(set, Vector2(2.0, 2.0), 3.0, COLUMN_ROTATION, 3, &first)
    );
    CHECK(next);
    CHECK(row_position(arrival) == Vector2(1.0, 1.0));
    NETW_CHECK_CLOSE(row_rotation(arrival), 3.0, 0.0001);

    memdelete(dst);
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-2 a torn row is refused whole, and leaves "
    "the row and the baseline it would compose from exactly where they were"
) {
    netw_test::Books books;
    const uint64_t pose = pose_set(books.sets);
    PropertySet *const set = books.sets.get(pose);
    Node2D *dst = memnew(Node2D);
    StoreArrival arrival(books.bindings.create(pose, dst));

    const CodeRow base = pose_row(set, Vector2(1.0, 2.0), 0.5);
    NETW_CHECK_EQ(
        int(arrival.land(
            pose_frame(set, Vector2(1.0, 2.0), 0.5, COLUMN_BOTH),
            -1,
            false
        )),
        int(Landing::ACCEPTED)
    );
    const PackedByteArray delta
        = pose_frame(set, Vector2(4.0, 5.0), 2.0, COLUMN_BOTH, 2, &base);

    PackedByteArray torn = delta;
    torn.resize(delta.size() - 1);
    NETW_CHECK_EQ(int(arrival.land(torn, -1, false)), int(Landing::MALFORMED));
    const bool the_refusal_left_the_row_alone
        = row_position(arrival) == Vector2(1.0, 2.0);
    CHECK(the_refusal_left_the_row_alone);

    NETW_CHECK_EQ(int(arrival.land(delta, -1, false)), int(Landing::ACCEPTED));
    const bool the_baseline_still_composed
        = row_position(arrival) == Vector2(4.0, 5.0);
    CHECK(the_baseline_still_composed);

    memdelete(dst);
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-3 a row naming a property the receiving "
    "node does not carry lands nothing and notifies nobody"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][wire] An update for route * names property '*', "
        "which the node on this peer does not have, so it was "
        "dropped. Give the node the same script on every peer.",
        1
    );

    netw_test::Books books;
    const uint64_t pose = pose_set(books.sets);
    PropertySet *const set = books.sets.get(pose);
    Node *bare = memnew(Node);
    netw_test::LandedLog log;
    const PropertyBindingPtr binding = books.bindings.create(pose, bare);
    log.watch(binding);
    StoreArrival arrival(binding);

    const bool the_row_was_refused = !settle(
        arrival,
        pose_frame(set, Vector2(3.0, 3.0), 1.0, COLUMN_BOTH)
    );
    CHECK(the_row_was_refused);
    NETW_CHECK_EQ(log.count, int64_t(0));
    const netw::store::RowAddress at = arrival.at(0);
    const bool nothing_landed
        = !arrival.world().table(at.table).holds_values(at.row);
    CHECK(nothing_landed);

    memdelete(bare);
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Repl] C4-4 a target that vanishes after a landed row "
    "resets the stream, so the next row composes from nothing"
) {
    netw_test::Books books;
    const uint64_t pose = pose_set(books.sets);
    PropertySet *const set = books.sets.get(pose);
    Node *dst = netw_test::minted_node(
        "extends Node\n"
        "var position := Vector2.ZERO\n"
        "var rotation := 0.0\n"
    );
    REQUIRE(dst != nullptr);
    const Ref<Script> script = dst->get_script();
    const PropertyBindingPtr binding = books.bindings.create(pose, dst);
    StoreArrival arrival(binding);

    const bool committed
        = settle(arrival, pose_frame(set, Vector2(5.0, 5.0), 1.0, COLUMN_BOTH));
    CHECK(committed);
    CHECK(row_position(arrival) == Vector2(5.0, 5.0));

    const CodeRow first = pose_row(set, Vector2(5.0, 5.0), 1.0);
    dst->set_script(Variant());
    const bool the_row_was_refused = !settle(
        arrival,
        pose_frame(set, Vector2(6.0, 6.0), 1.0, COLUMN_POSITION, 2, &first)
    );
    CHECK(the_row_was_refused);
    const bool the_stream_was_reset = binding->stream_is_reset();
    CHECK(the_stream_was_reset);

    dst->set_script(script);
    const bool fresh = settle(
        arrival,
        pose_frame(set, Vector2(0.0, 0.0), 4.0, COLUMN_BOTH, 3)
    );
    CHECK(fresh);
    CHECK(row_position(arrival) == Vector2(0.0, 0.0));
    NETW_CHECK_CLOSE(row_rotation(arrival), 4.0, 0.0001);
    CHECK_FALSE(binding->stream_is_reset());

    memdelete(dst);
}

#endif

TEST_CASE(
    "[Networked][Repl][Hosted] C4-5 a closed write gate still lands the "
    "row it hands to its consumer, so the next row composes on it"
) {
    netw_test::Books books;
    const uint64_t pose = pose_set(books.sets);
    PropertySet *const set = books.sets.get(pose);
    Node2D *dst = memnew(Node2D);
    dst->set_position(Vector2(-1.0, -1.0));
    netw_test::LandedLog log;
    const PropertyBindingPtr binding = books.bindings.create(pose, dst);
    binding->write_gate = false;
    log.watch(binding);
    StoreArrival arrival(binding);

    settle(arrival, pose_frame(set, Vector2(7.0, 8.0), 1.0, COLUMN_BOTH));
    CHECK_FALSE(binding->writes_node(false));
    NETW_CHECK_EQ(log.count, int64_t(1));

    const CodeRow first = pose_row(set, Vector2(7.0, 8.0), 1.0);
    const bool the_second_row_landed = settle(
        arrival,
        pose_frame(set, Vector2(9.0, 9.0), 5.0, COLUMN_ROTATION, 2, &first)
    );
    CHECK(the_second_row_landed);
    NETW_CHECK_EQ(log.count, int64_t(2));
    const bool the_consumer_saw_the_landed_baseline
        = Vector2(log.value(StringName("position"))) == Vector2(7.0, 8.0);
    CHECK(the_consumer_saw_the_landed_baseline);

    memdelete(dst);
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-6 a row delivered twice lands and notifies "
    "once, and one that never lands notifies not at all"
) {
    netw_test::Books books;
    const uint64_t pose = pose_set(books.sets);
    PropertySet *const set = books.sets.get(pose);
    Node2D *dst = memnew(Node2D);
    netw_test::LandedLog log;
    const PropertyBindingPtr binding = books.bindings.create(pose, dst);
    log.watch(binding);
    StoreArrival arrival(binding);

    const PackedByteArray whole
        = pose_frame(set, Vector2(2.0, 4.0), 6.0, COLUMN_BOTH);
    const bool the_row_landed = settle(arrival, whole);
    CHECK(the_row_landed);
    NETW_CHECK_EQ(int(arrival.land(whole, -1, false)), int(Landing::DUPLICATE));
    const bool the_repeat_was_refused = !settle(arrival, whole);
    CHECK(the_repeat_was_refused);
    NETW_CHECK_EQ(log.count, int64_t(1));

    const CodeRow first = pose_row(set, Vector2(2.0, 4.0), 6.0);
    PackedByteArray torn
        = pose_frame(set, Vector2(3.0, 3.0), 6.0, COLUMN_POSITION, 2, &first);
    torn.resize(torn.size() - 1);
    const bool the_torn_row_was_refused = !settle(arrival, torn);
    CHECK(the_torn_row_was_refused);
    NETW_CHECK_EQ(log.count, int64_t(1));

    memdelete(dst);
}

} // namespace TestNetwReceiveTransaction

#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/snapshot_frame.hpp"

using namespace godot;

namespace TestNetwReplRowFrame {

using godot::PackedByteArray;
using netw::SchemaCore;
using netw::repl::read_snapshot_row;
using netw::repl::read_window_frame;
using netw::repl::RowFrameHeader;
using netw::repl::SnapshotHeader;
using netw::repl::WindowSample;
using netw::repl::write_snapshot_row;
using netw::repl::write_window_frame;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

constexpr int64_t BASE_TICK = 41;
constexpr int64_t ANY_LIFE = -1;
constexpr uint64_t TOKEN = 77;

WirePlan triple() {
    SchemaRecord record;
    record.name = godot::StringName("Triple");
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
    SchemaCore::append_column(
        &record,
        godot::StringName("z"),
        SchemaCore::I16,
        1
    );
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow filled(const WirePlan &p_plan, uint64_t a, uint64_t b, uint64_t c) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, a);
    row.write(p_plan.column(1), 0, b);
    row.write(p_plan.column(2), 0, c);
    return row;
}

SnapshotHeader absolute() {
    SnapshotHeader out;
    out.token = TOKEN;
    out.revision = 1;
    out.distance = 0;
    out.reconcile_ack = -1;
    out.tick = -1;
    return out;
}

SnapshotHeader stepping(uint64_t p_mask) {
    SnapshotHeader out = absolute();
    out.revision = 2;
    out.distance = 1;
    out.mask = p_mask;
    return out;
}

RowFrameHeader window_header(uint64_t p_mask) {
    RowFrameHeader out;
    out.life = 0;
    out.reconcile_ack = -1;
    out.mask = p_mask;
    return out;
}

TEST_CASE("[Networked][Repl][Hosted] a row round trips its header") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    SnapshotHeader sent = absolute();
    sent.revision = 5;
    sent.reconcile_ack = 38;
    const PackedByteArray bytes
        = write_snapshot_row(sent, BASE_TICK, plan, row, nullptr);
    NETW_REQUIRE_EQ(bytes.size() > 0, true);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(bytes, BASE_TICK, plan, got, into, nullptr, nullptr),
        true
    );

    NETW_CHECK_EQ(int64_t(got.token), int64_t(TOKEN));
    NETW_CHECK_EQ(int64_t(got.revision), int64_t(5));
    NETW_CHECK_EQ(got.reconcile_ack, 38);
    NETW_CHECK_EQ(got.mask, plan.full_mask());
    CHECK(into.equals(row));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a reconcile ack is an age below the datagram's "
    "tick, so the same ack costs the same bits at any tick"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    SnapshotHeader early = absolute();
    early.reconcile_ack = 38;
    SnapshotHeader late = absolute();
    late.reconcile_ack = 100038;

    const int64_t near_zero
        = write_snapshot_row(early, 41, plan, row, nullptr).size();
    const int64_t far_out
        = write_snapshot_row(late, 100041, plan, row, nullptr).size();
    NETW_CHECK_EQ(near_zero, far_out);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            write_snapshot_row(late, 100041, plan, row, nullptr),
            100041,
            plan,
            got,
            into,
            nullptr,
            nullptr
        ),
        true
    );
    NETW_CHECK_EQ(got.reconcile_ack, 100038);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a row with no reconcile ack spends one bit on "
    "saying so"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    SnapshotHeader acked = absolute();
    acked.reconcile_ack = 40;

    const int64_t without
        = write_snapshot_row(absolute(), BASE_TICK, plan, row, nullptr).size();
    const int64_t with
        = write_snapshot_row(acked, BASE_TICK, plan, row, nullptr).size();
    CHECK(without < with);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            write_snapshot_row(absolute(), BASE_TICK, plan, row, nullptr),
            BASE_TICK,
            plan,
            got,
            into,
            nullptr,
            nullptr
        ),
        true
    );
    NETW_CHECK_EQ(got.reconcile_ack, -1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] only the masked columns ride, and the rest are "
    "taken from the baseline the row names"
) {
    const WirePlan plan = triple();
    const CodeRow base = filled(plan, 7, 8, 9);
    const CodeRow fresh = filled(plan, 11, 22, 33);

    const PackedByteArray bytes
        = write_snapshot_row(stepping(0b010), BASE_TICK, plan, fresh, &base);
    NETW_REQUIRE_EQ(bytes.size() > 0, true);

    CodeRow held;
    SnapshotHeader got;
    NETW_REQUIRE_EQ(
        read_snapshot_row(bytes, BASE_TICK, plan, got, held, &base, nullptr),
        true
    );

    NETW_CHECK_EQ(held.read(plan.column(0), 0), 7);
    NETW_CHECK_EQ(held.read(plan.column(1), 0), 22);
    NETW_CHECK_EQ(held.read(plan.column(2), 0), 9);
}

TEST_CASE("[Networked][Repl][Hosted] a narrower mask is a shorter row") {
    const WirePlan plan = triple();
    const CodeRow base = filled(plan, 7, 8, 9);
    const CodeRow row = filled(plan, 11, 22, 33);

    const int64_t whole = write_snapshot_row(
                              stepping(plan.full_mask()),
                              BASE_TICK,
                              plan,
                              row,
                              &base
    )
                              .size();
    const int64_t one
        = write_snapshot_row(stepping(0b001), BASE_TICK, plan, row, &base)
              .size();

    CHECK(one < whole);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a mask naming a column the plan lacks is "
    "refused, and so is a row with no bytes at all"
) {
    const WirePlan plan = triple();
    const CodeRow base = filled(plan, 7, 8, 9);
    const CodeRow row = filled(plan, 11, 22, 33);

    NETW_CHECK_EQ(
        write_snapshot_row(stepping(0b1000), BASE_TICK, plan, row, &base)
            .size(),
        0
    );

    SnapshotHeader got;
    CodeRow into;
    CHECK_FALSE(read_snapshot_row(
        PackedByteArray(),
        BASE_TICK,
        plan,
        got,
        into,
        nullptr,
        nullptr
    ));
}

TEST_CASE("[Networked][Repl][Hosted] a row with residue is refused whole") {
    const WirePlan plan = triple();
    const CodeRow base = filled(plan, 7, 8, 9);
    const CodeRow row = filled(plan, 11, 22, 33);
    PackedByteArray bytes
        = write_snapshot_row(stepping(0b011), BASE_TICK, plan, row, &base);
    NETW_REQUIRE_EQ(bytes.size() > 0, true);
    bytes.push_back(0xAB);

    SnapshotHeader got;
    CodeRow into;
    CHECK_FALSE(
        read_snapshot_row(bytes, BASE_TICK, plan, got, into, &base, nullptr)
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] a truncated row is refused rather than read "
    "short"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray whole
        = write_snapshot_row(absolute(), BASE_TICK, plan, row, nullptr);
    NETW_REQUIRE_EQ(whole.size() > 2, true);

    PackedByteArray cut;
    for (int64_t at = 0; at < whole.size() - 1; ++at) {
        cut.push_back(whole[at]);
    }

    SnapshotHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(
        read_snapshot_row(cut, BASE_TICK, plan, got, into, nullptr, nullptr)
    );

    CHECK(into.equals(CodeRow::for_plan(plan)));
}

godot::LocalVector<WindowSample> window(
    const WirePlan &p_plan,
    int64_t p_oldest,
    uint32_t p_count
) {
    godot::LocalVector<WindowSample> out;
    for (uint32_t at = 0; at < p_count; ++at) {
        WindowSample sample;
        sample.tick = p_oldest + int64_t(at);
        sample.row = filled(p_plan, 10 + at, 20 + at, 30 + at);
        out.push_back(sample);
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame round trips every tick it "
    "repeats, and the ticks come back absolute against the datagram's"
) {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> samples = window(plan, 39, 3);
    const PackedByteArray bytes
        = write_window_frame(window_header(0), BASE_TICK, plan, samples);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;
    REQUIRE(read_window_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, read));

    NETW_CHECK_EQ(got.mask, plan.full_mask());
    REQUIRE(read.size() == 3);
    for (uint32_t at = 0; at < read.size(); ++at) {
        NETW_CHECK_EQ(read[at].tick, samples[at].tick);
        CHECK(read[at].row.equals(samples[at].row));
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame costs one row per tick it "
    "carries"
) {
    const WirePlan plan = triple();
    const PackedByteArray one = write_window_frame(
        window_header(0),
        BASE_TICK,
        plan,
        window(plan, 41, 1)
    );
    const PackedByteArray three = write_window_frame(
        window_header(0),
        BASE_TICK,
        plan,
        window(plan, 39, 3)
    );
    REQUIRE(one.size() > 0);
    CHECK(three.size() > one.size());
}

TEST_CASE("[Networked][Repl][Hosted] an empty window is no frame at all") {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> none;
    CHECK(
        write_window_frame(window_header(0), BASE_TICK, plan, none).is_empty()
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window authored ahead of the datagram keeps "
    "its own ticks, because a stamped lane writes for the tick that applies "
    "the value rather than for the tick that carries it"
) {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> ahead
        = window(plan, BASE_TICK + 1, 2);
    const PackedByteArray bytes
        = write_window_frame(window_header(0), BASE_TICK, plan, ahead);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;
    REQUIRE(read_window_frame(bytes, BASE_TICK, ANY_LIFE, plan, got, read));

    NETW_CHECK_EQ(got.tick, BASE_TICK + 2);
    REQUIRE(read.size() == 2);
    for (uint32_t at = 0; at < read.size(); ++at) {
        NETW_CHECK_EQ(read[at].tick, ahead[at].tick);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame on a datagram with no tick is "
    "no frame at all, because every sample it holds is an age below one"
) {
    const WirePlan plan = triple();
    CHECK(write_window_frame(window_header(0), -1, plan, window(plan, 39, 3))
              .is_empty());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame with residue or a short tail is "
    "refused whole"
) {
    const WirePlan plan = triple();
    const PackedByteArray bytes = write_window_frame(
        window_header(0),
        BASE_TICK,
        plan,
        window(plan, 39, 3)
    );
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;

    PackedByteArray longer = bytes;
    longer.push_back(0);
    CHECK_FALSE(
        read_window_frame(longer, BASE_TICK, ANY_LIFE, plan, got, read)
    );

    PackedByteArray shorter = bytes;
    shorter.resize(bytes.size() - 1);
    CHECK_FALSE(
        read_window_frame(shorter, BASE_TICK, ANY_LIFE, plan, got, read)
    );
    NETW_CHECK_EQ(read.size(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an unquantized composite column rides as a "
    "row, because three strided elements reach a stream one wide run cannot"
) {
    SchemaRecord record;
    record.name = godot::StringName("Placed");
    SchemaCore::append_column(
        &record,
        godot::StringName("pos"),
        SchemaCore::VECTOR3,
        1
    );
    SchemaCore::fix(&record);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());

    CodeRow row = CodeRow::for_plan(plan);
    REQUIRE(row.write_bits(0, 32, 0x3f800000ULL));
    REQUIRE(row.write_bits(32, 32, 0x40000000ULL));
    REQUIRE(row.write_bits(64, 32, 0x40400000ULL));

    const PackedByteArray bytes
        = write_snapshot_row(absolute(), BASE_TICK, plan, row, nullptr);
    REQUIRE(bytes.size() > 0);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(bytes, BASE_TICK, plan, got, into, nullptr, nullptr),
        true
    );
    CHECK(into.equals(row));
}

} // namespace TestNetwReplRowFrame

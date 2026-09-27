#include "support/netw_test.h"
#include "support/send_drive.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/row_frame.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestNetwReplRowLadder {

using godot::PackedByteArray;
using netw::SchemaCore;
using netw::repl::mask_ladders;
using netw::repl::read_snapshot_row;
using netw::repl::RowFrameHeader;
using netw::repl::RowOffer;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::repl::SnapshotHeader;
using netw::repl::SnapshotRefusal;
using netw::repl::write_snapshot_row;
using netw::table::DeltaMode;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::CodeRow;
using netw::wire::Delivery;
using netw::wire::WirePlan;
using netw::wire::WireRegistry;
using netw_test::accept_streams;
using netw_test::drive_send;

constexpr int64_t BASE_TICK = 41;
constexpr int64_t ANY_LIFE = -1;
constexpr uint64_t TOKEN = 77;
constexpr uint8_t LIVE_CHANNEL = 46;
constexpr int LIVE_PEER = 5;

SchemaRecord stepping_record(DeltaMode p_delta) {
    SchemaRecord record;
    record.name = StringName("Stepping");
    SchemaCore::append_column(&record, StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("y"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("flag"), SchemaCore::BOOL, 1);
    record.at(0)->delta = p_delta;
    record.at(1)->delta = p_delta;
    record.at(2)->delta = p_delta;
    SchemaCore::fix(&record);
    return record;
}

WirePlan laddered_triple() {
    return WirePlan::compile(stepping_record(DeltaMode::LADDER));
}

WireRegistry live_registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = LIVE_CHANNEL;
    decl.name = StringName("ladder_live");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

Array triple_of(int64_t x, int64_t y, bool flag) {
    Array out;
    out.push_back(x);
    out.push_back(y);
    out.push_back(flag);
    return out;
}

RowOffer live_offer(const SchemaRecord &p_schema, const Array &p_values) {
    RowOffer out;
    out.route = 21;
    out.comp = 2;
    out.channel = LIVE_CHANNEL;
    out.schema = &p_schema;
    out.values = p_values;
    out.recipients.push_back(LIVE_PEER);
    out.masked = true;
    return out;
}

CodeRow filled(const WirePlan &p_plan, uint64_t a, uint64_t b, uint64_t c) {
    CodeRow row = CodeRow::for_plan(p_plan);
    row.write(p_plan.column(0), 0, a);
    row.write(p_plan.column(1), 0, b);
    row.write(p_plan.column(2), 0, c);
    return row;
}

RowFrameHeader window_header(uint64_t p_mask) {
    RowFrameHeader out;
    out.life = 0;
    out.reconcile_ack = -1;
    out.mask = p_mask;
    return out;
}

SnapshotHeader absolute(uint64_t p_revision) {
    SnapshotHeader out;
    out.token = TOKEN;
    out.revision = p_revision;
    out.distance = 0;
    out.reconcile_ack = -1;
    out.tick = -1;
    return out;
}

SnapshotHeader stepping(uint64_t p_revision, uint64_t p_mask) {
    SnapshotHeader out = absolute(p_revision);
    out.distance = 1;
    out.mask = p_mask;
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD1 a step against the named baseline decodes "
    "to the value the sender held, and costs less than the whole code"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow base = filled(plan, 10000, 20000, 1);
    const CodeRow moved = filled(plan, 10003, 19997, 1);

    const PackedByteArray stepped = write_snapshot_row(
        stepping(2, plan.full_mask()),
        BASE_TICK,
        plan,
        moved,
        &base
    );
    const PackedByteArray whole
        = write_snapshot_row(absolute(1), BASE_TICK, plan, moved, nullptr);
    NETW_REQUIRE_EQ(stepped.size() > 0, true);
    NETW_REQUIRE_EQ(whole.size() > 0, true);
    const bool ladder_is_smaller = stepped.size() < whole.size();
    CHECK(ladder_is_smaller);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(stepped, BASE_TICK, plan, got, into, &base, nullptr),
        true
    );
    CHECK(into.equals(moved));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD2 the receiver's own held row is not the "
    "baseline, and a second row after a change still decodes right"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow confirmed = filled(plan, 10000, 20000, 1);
    const CodeRow first = filled(plan, 10004, 20000, 1);
    const CodeRow second = filled(plan, 10009, 20000, 1);

    SnapshotHeader got;
    CodeRow held;
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            write_snapshot_row(
                stepping(2, plan.full_mask()),
                BASE_TICK,
                plan,
                first,
                &confirmed
            ),
            BASE_TICK,
            plan,
            got,
            held,
            &confirmed,
            nullptr
        ),
        true
    );
    CHECK(held.equals(first));

    NETW_REQUIRE_EQ(
        read_snapshot_row(
            write_snapshot_row(
                stepping(3, plan.full_mask()),
                BASE_TICK,
                plan,
                second,
                &confirmed
            ),
            BASE_TICK,
            plan,
            got,
            held,
            &confirmed,
            nullptr
        ),
        true
    );
    CHECK(held.equals(second));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD3 a row naming a baseline the receiver does "
    "not hold is refused whole and says why"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow base = filled(plan, 10000, 20000, 1);
    const CodeRow moved = filled(plan, 10003, 20000, 1);

    const PackedByteArray stepped = write_snapshot_row(
        stepping(2, plan.full_mask()),
        BASE_TICK,
        plan,
        moved,
        &base
    );
    NETW_REQUIRE_EQ(stepped.size() > 0, true);

    SnapshotHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    const CodeRow untouched = into;
    SnapshotRefusal why = SnapshotRefusal::NONE;
    const bool refused = !read_snapshot_row(
        stepped,
        BASE_TICK,
        plan,
        got,
        into,
        nullptr,
        &why
    );
    CHECK(refused);
    const bool named_the_baseline = why == SnapshotRefusal::BASELINE_UNKNOWN;
    CHECK(named_the_baseline);
    CHECK(into.equals(untouched));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD4 a bucket wider than the step needed is "
    "refused, because the encoding is canonical"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow base = filled(plan, 10000, 20000, 1);
    const CodeRow moved = filled(plan, 10001, 20000, 1);

    const PackedByteArray canonical = write_snapshot_row(
        stepping(2, plan.full_mask()),
        BASE_TICK,
        plan,
        moved,
        &base
    );
    NETW_REQUIRE_EQ(canonical.size() > 0, true);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            canonical,
            BASE_TICK,
            plan,
            got,
            into,
            &base,
            nullptr
        ),
        true
    );

    netw::wire::WriteStream loose;
    uint64_t token = TOKEN;
    uint64_t revision = 2;
    uint64_t distance = 1;
    uint64_t mask = plan.full_mask();
    bool no_ack = false;
    bool no_tick = false;
    uint64_t wide_selector = 2;
    uint64_t wide_body = 2;
    uint64_t tight_selector = 1;
    uint64_t zero_step = 0;
    uint64_t flag_code = 1;
    NETW_REQUIRE_EQ(loose.varuint(token, 10), true);
    NETW_REQUIRE_EQ(loose.varuint(revision, 10), true);
    NETW_REQUIRE_EQ(
        loose.bits(distance, netw::repl::SNAPSHOT_DISTANCE_BITS),
        true
    );
    NETW_REQUIRE_EQ(loose.bool1(no_ack), true);
    NETW_REQUIRE_EQ(loose.bool1(no_tick), true);
    NETW_REQUIRE_EQ(loose.bits(mask, int(plan.mask_width())), true);
    NETW_REQUIRE_EQ(
        loose.bits(wide_selector, netw::wire::LADDER_SELECTOR_BITS),
        true
    );
    NETW_REQUIRE_EQ(loose.bits(wide_body, 8), true);
    NETW_REQUIRE_EQ(
        loose.bits(tight_selector, netw::wire::LADDER_SELECTOR_BITS),
        true
    );
    NETW_REQUIRE_EQ(loose.bits(zero_step, 4), true);
    NETW_REQUIRE_EQ(loose.bits(flag_code, 1), true);
    NETW_REQUIRE_EQ(loose.align_verify(), true);

    CodeRow spoiled = CodeRow::for_plan(plan);
    const bool refused_the_loose_bucket = !read_snapshot_row(
        loose.to_bytes(),
        BASE_TICK,
        plan,
        got,
        spoiled,
        &base,
        nullptr
    );
    CHECK(refused_the_loose_bucket);
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD5 a column of one bit carries no selector, "
    "so a plan of only narrow columns spends no baseline step"
) {
    SchemaRecord record;
    record.name = StringName("Flags");
    SchemaCore::append_column(&record, StringName("a"), SchemaCore::BOOL, 1);
    SchemaCore::append_column(&record, StringName("b"), SchemaCore::BOOL, 1);
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    SchemaCore::fix(&record);
    const WirePlan plan = WirePlan::compile(record);
    NETW_REQUIRE_EQ(plan.valid(), true);

    const bool mask_carries_no_ladder = !mask_ladders(plan, plan.full_mask());
    CHECK(mask_carries_no_ladder);

    CodeRow row = CodeRow::for_plan(plan);
    row.write(plan.column(0), 0, 1);
    row.write(plan.column(1), 0, 0);

    const PackedByteArray named = write_snapshot_row(
        stepping(2, plan.full_mask()),
        BASE_TICK,
        plan,
        row,
        &row
    );
    NETW_REQUIRE_EQ(named.size() > 0, true);

    SnapshotHeader got;
    CodeRow into;
    NETW_REQUIRE_EQ(
        read_snapshot_row(named, BASE_TICK, plan, got, into, &row, nullptr),
        true
    );
    CHECK(into.equals(row));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD9 a window steps each sample from the one "
    "before it and writes the oldest whole"
) {
    const WirePlan plan = laddered_triple();
    LocalVector<netw::repl::WindowSample> samples;
    for (int64_t at = 0; at < 4; ++at) {
        netw::repl::WindowSample sample;
        sample.tick = 100 + at;
        sample.row = filled(plan, uint64_t(9000 + at * 3), 500, 1);
        samples.push_back(sample);
    }

    const PackedByteArray stepped
        = netw::repl::write_window_frame(window_header(0), 99, plan, samples);
    NETW_REQUIRE_EQ(stepped.size() > 0, true);

    RowFrameHeader got;
    LocalVector<netw::repl::WindowSample> back;
    NETW_REQUIRE_EQ(
        netw::repl::read_window_frame(stepped, 99, ANY_LIFE, plan, got, back),
        true
    );
    NETW_CHECK_EQ(int64_t(back.size()), int64_t(4));
    for (uint32_t at = 0; at < back.size(); ++at) {
        NETW_CHECK_EQ(back[at].tick, samples[at].tick);
        CHECK(back[at].row.equals(samples[at].row));
    }

    LocalVector<netw::repl::WindowSample> alone;
    alone.push_back(samples[0]);
    const PackedByteArray one
        = netw::repl::write_window_frame(window_header(0), 99, plan, alone);
    const int64_t per_extra_sample
        = (int64_t(stepped.size()) - int64_t(one.size()));
    const bool three_steps_cost_less_than_three_whole_rows
        = per_extra_sample < 3 * 5;
    CHECK(three_steps_cost_less_than_three_whole_rows);
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD6 a row the send pass composes against a "
    "confirmed snapshot steps its moved columns, and the same delta over a "
    "plan that declares no ladder costs more"
) {
    const WireRegistry reg = live_registry();
    const SchemaRecord laddered = stepping_record(DeltaMode::LADDER);
    const WirePlan plan = WirePlan::compile(laddered);
    const WirePlan whole_plan
        = WirePlan::compile(stepping_record(DeltaMode::FULL));
    NETW_REQUIRE_EQ(plan.valid() && whole_plan.valid(), true);

    SessionSend session;
    LocalVector<RowOffer> first;
    first.push_back(live_offer(laddered, triple_of(10000, 20000, true)));
    const SessionResult one = drive_send(session, reg, first, 100000, 1);
    NETW_REQUIRE_EQ(int64_t(one.sends.size()), int64_t(1));
    const CodeRow confirmed = one.sends[0].row;
    accept_streams(session, first, LIVE_PEER);

    LocalVector<RowOffer> second;
    second.push_back(live_offer(laddered, triple_of(10003, 19997, true)));
    const SessionResult two = drive_send(session, reg, second, 100000, 2);
    NETW_REQUIRE_EQ(int64_t(two.sends.size()), int64_t(1));
    const netw::repl::RowSend &stepped = two.sends[0];
    CHECK(stepped.masked);
    NETW_CHECK_EQ(stepped.mask, uint64_t(0b011));

    SnapshotHeader same;
    same.token = stepped.token;
    same.revision = stepped.revision;
    same.distance = stepped.revision - one.sends[0].revision;
    same.mask = stepped.mask;
    const PackedByteArray unstepped
        = write_snapshot_row(same, 0, whole_plan, stepped.row, &confirmed);
    NETW_REQUIRE_EQ(unstepped.size() > 0, true);
    const bool the_ladder_saved_bytes = stepped.bytes.size() < unstepped.size();
    CHECK(the_ladder_saved_bytes);

    SnapshotHeader got;
    CodeRow landed;
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            stepped.bytes,
            0,
            plan,
            got,
            landed,
            &confirmed,
            nullptr
        ),
        true
    );
    CHECK(landed.equals(stepped.row));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD7 a lane holding no confirmed snapshot writes "
    "every column whole, however many revisions it has already exposed"
) {
    const WireRegistry reg = live_registry();
    const SchemaRecord laddered = stepping_record(DeltaMode::LADDER);
    const WirePlan plan = WirePlan::compile(laddered);

    SessionSend session;
    LocalVector<RowOffer> first;
    first.push_back(live_offer(laddered, triple_of(10000, 20000, true)));
    const SessionResult one = drive_send(session, reg, first, 100000, 1);
    NETW_REQUIRE_EQ(int64_t(one.sends.size()), int64_t(1));
    CHECK_FALSE(one.sends[0].masked);
    NETW_CHECK_EQ(one.sends[0].mask, plan.full_mask());

    SnapshotHeader got;
    CodeRow landed;
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            one.sends[0].bytes,
            0,
            plan,
            got,
            landed,
            nullptr,
            nullptr
        ),
        true
    );
    CHECK(landed.equals(one.sends[0].row));

    LocalVector<RowOffer> second;
    second.push_back(live_offer(laddered, triple_of(10003, 19997, true)));
    const SessionResult two = drive_send(session, reg, second, 100000, 2);
    NETW_REQUIRE_EQ(int64_t(two.sends.size()), int64_t(1));
    CHECK_FALSE(two.sends[0].masked);
    NETW_CHECK_EQ(two.sends[0].mask, plan.full_mask());
    NETW_REQUIRE_EQ(
        read_snapshot_row(
            two.sends[0].bytes,
            0,
            plan,
            got,
            landed,
            nullptr,
            nullptr
        ),
        true
    );
    CHECK(landed.equals(two.sends[0].row));
}

} // namespace TestNetwReplRowLadder

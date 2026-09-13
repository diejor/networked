#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/repl/baseline_ring.hpp"
#include "netw/repl/retained_lane.hpp"
#include "netw/repl/row_frame.hpp"

using namespace godot;

namespace TestNetwReplRowLadder {

using godot::PackedByteArray;
using netw::SchemaCore;
using netw::repl::BaselineRing;
using netw::repl::mask_ladders;
using netw::repl::read_row_frame;
using netw::repl::RowBaseline;
using netw::repl::RowBaselineSource;
using netw::repl::RowFrameHeader;
using netw::repl::RowRefusal;
using netw::repl::write_row_frame;
using netw::table::DeltaMode;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

constexpr int64_t BASE_TICK = 41;
constexpr int64_t ANY_LIFE = -1;

WirePlan laddered_triple() {
    SchemaRecord record;
    record.name = StringName("Stepping");
    SchemaCore::append_column(&record, StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("y"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("flag"), SchemaCore::BOOL, 1);
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    record.at(2)->delta = DeltaMode::LADDER;
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

RowFrameHeader header(uint64_t p_mask) {
    RowFrameHeader out;
    out.life = 0;
    out.reconcile_ack = -1;
    out.mask = p_mask;
    return out;
}

RowBaseline naming(const CodeRow &p_row, uint8_t p_low) {
    RowBaseline out;
    out.row = &p_row;
    out.low = p_low;
    return out;
}

RowBaselineSource arriving(const BaselineRing &p_ring, int64_t p_seq) {
    RowBaselineSource out;
    out.ring = &p_ring;
    out.seq = p_seq;
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD1 a step against the named baseline decodes "
    "to the value the sender held, and costs less than the whole code"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow base = filled(plan, 10000, 20000, 1);
    const CodeRow moved = filled(plan, 10003, 19997, 1);

    BaselineRing ring;
    ring.record(9, base);

    const PackedByteArray stepped = write_row_frame(
        header(plan.full_mask()),
        BASE_TICK,
        plan,
        moved,
        naming(base, 9)
    );
    const PackedByteArray whole
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, moved);
    REQUIRE(stepped.size() > 0);
    REQUIRE(whole.size() > 0);
    const bool ladder_is_smaller = stepped.size() < whole.size();
    CHECK(ladder_is_smaller);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(
        stepped,
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        into,
        arriving(ring, 10)
    ));
    CHECK(into.equals(moved));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD2 the receiver's own held row is not the "
    "baseline, and a second frame after a change still decodes right"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow confirmed = filled(plan, 10000, 20000, 1);
    const CodeRow first = filled(plan, 10004, 20000, 1);
    const CodeRow second = filled(plan, 10009, 20000, 1);

    BaselineRing ring;
    ring.record(20, confirmed);

    RowFrameHeader got;
    CodeRow held = CodeRow::for_plan(plan);
    held.copy_from(confirmed);
    REQUIRE(read_row_frame(
        write_row_frame(
            header(plan.full_mask()),
            BASE_TICK,
            plan,
            first,
            naming(confirmed, 20)
        ),
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        held,
        arriving(ring, 21)
    ));
    CHECK(held.equals(first));
    ring.record(21, held);

    REQUIRE(read_row_frame(
        write_row_frame(
            header(plan.full_mask()),
            BASE_TICK,
            plan,
            second,
            naming(confirmed, 20)
        ),
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        held,
        arriving(ring, 22)
    ));
    CHECK(held.equals(second));
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD3 a frame naming a baseline the receiver "
    "does not hold is refused whole and says why"
) {
    const WirePlan plan = laddered_triple();
    const CodeRow base = filled(plan, 10000, 20000, 1);
    const CodeRow moved = filled(plan, 10003, 20000, 1);

    BaselineRing ring;
    ring.record(9, base);

    const PackedByteArray stepped = write_row_frame(
        header(plan.full_mask()),
        BASE_TICK,
        plan,
        moved,
        naming(base, 7)
    );
    REQUIRE(stepped.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    const CodeRow untouched = into;
    RowRefusal why = RowRefusal::NONE;
    const bool refused = !read_row_frame(
        stepped,
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        into,
        arriving(ring, 10),
        &why
    );
    CHECK(refused);
    const bool named_the_baseline = why == RowRefusal::BASELINE_UNKNOWN;
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

    BaselineRing ring;
    ring.record(9, base);

    const PackedByteArray canonical = write_row_frame(
        header(plan.full_mask()),
        BASE_TICK,
        plan,
        moved,
        naming(base, 9)
    );
    REQUIRE(canonical.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(
        canonical,
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        into,
        arriving(ring, 10)
    ));

    netw::wire::WriteStream loose;
    uint64_t life = 0;
    uint64_t mask = plan.full_mask();
    bool no_ack = false;
    bool no_tick = false;
    bool has_base = true;
    uint64_t base_low = 9;
    uint64_t wide_selector = 2;
    uint64_t wide_body = 2;
    uint64_t tight_selector = 1;
    uint64_t zero_step = 0;
    uint64_t flag_code = 1;
    REQUIRE(loose.bits(life, netw::repl::ROW_LIFE_BITS));
    REQUIRE(loose.bits(mask, int(plan.mask_width())));
    REQUIRE(loose.bool1(no_ack));
    REQUIRE(loose.bool1(no_tick));
    REQUIRE(loose.bool1(has_base));
    REQUIRE(loose.bits(base_low, 8));
    REQUIRE(loose.bits(wide_selector, netw::wire::LADDER_SELECTOR_BITS));
    REQUIRE(loose.bits(wide_body, 8));
    REQUIRE(loose.bits(tight_selector, netw::wire::LADDER_SELECTOR_BITS));
    REQUIRE(loose.bits(zero_step, 4));
    REQUIRE(loose.bits(flag_code, 1));
    REQUIRE(loose.align_verify());

    CodeRow spoiled = CodeRow::for_plan(plan);
    const bool refused_the_loose_bucket = !read_row_frame(
        loose.to_bytes(),
        BASE_TICK,
        ANY_LIFE,
        plan,
        got,
        spoiled,
        arriving(ring, 10)
    );
    CHECK(refused_the_loose_bucket);
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD5 a column of one bit carries no selector, "
    "so a plan of only narrow columns spends no baseline name"
) {
    SchemaRecord record;
    record.name = StringName("Flags");
    SchemaCore::append_column(&record, StringName("a"), SchemaCore::BOOL, 1);
    SchemaCore::append_column(&record, StringName("b"), SchemaCore::BOOL, 1);
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    SchemaCore::fix(&record);
    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());

    const bool mask_carries_no_ladder = !mask_ladders(plan, plan.full_mask());
    CHECK(mask_carries_no_ladder);

    CodeRow row = CodeRow::for_plan(plan);
    row.write(plan.column(0), 0, 1);
    row.write(plan.column(1), 0, 0);

    const PackedByteArray named = write_row_frame(
        header(plan.full_mask()),
        BASE_TICK,
        plan,
        row,
        naming(row, 3)
    );
    const PackedByteArray bare
        = write_row_frame(header(plan.full_mask()), BASE_TICK, plan, row);
    REQUIRE(named.size() > 0);
    const bool a_narrow_plan_pays_nothing = named == bare;
    CHECK(a_narrow_plan_pays_nothing);
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD6 the ring holds sixty four consecutive "
    "seqs and forgets what falls behind them"
) {
    const WirePlan plan = laddered_triple();
    BaselineRing ring;
    for (int64_t at = 0; at < 80; ++at) {
        ring.record(uint16_t(at), filled(plan, uint64_t(at), 0, 0));
    }
    NETW_CHECK_EQ(int64_t(ring.count()), int64_t(BaselineRing::DEPTH));

    const bool holds_the_newest = ring.resolve(79, 79) != nullptr;
    CHECK(holds_the_newest);
    const bool holds_the_oldest_kept = ring.resolve(79, 16) != nullptr;
    CHECK(holds_the_oldest_kept);
    const bool forgot_what_fell_behind = ring.resolve(79, 15) == nullptr;
    CHECK(forgot_what_fell_behind);
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD7 a baseline whose low byte aliases one the "
    "ring never held resolves to nothing rather than to the wrong row"
) {
    const WirePlan plan = laddered_triple();
    BaselineRing ring;
    ring.record(300, filled(plan, 5, 0, 0));

    const bool the_one_it_holds = ring.resolve(300, uint8_t(300)) != nullptr;
    CHECK(the_one_it_holds);
    const bool the_alias_two_fifty_six_back
        = ring.resolve(556, uint8_t(300)) == nullptr;
    CHECK(the_alias_two_fifty_six_back);
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
        = netw::repl::write_window_frame(header(0), 99, plan, samples);
    REQUIRE(stepped.size() > 0);

    RowFrameHeader got;
    LocalVector<netw::repl::WindowSample> back;
    REQUIRE(
        netw::repl::read_window_frame(stepped, 99, ANY_LIFE, plan, got, back)
    );
    NETW_CHECK_EQ(int64_t(back.size()), int64_t(4));
    for (uint32_t at = 0; at < back.size(); ++at) {
        NETW_CHECK_EQ(back[at].tick, samples[at].tick);
        CHECK(back[at].row.equals(samples[at].row));
    }

    LocalVector<netw::repl::WindowSample> alone;
    alone.push_back(samples[0]);
    const PackedByteArray one
        = netw::repl::write_window_frame(header(0), 99, plan, alone);
    const int64_t per_extra_sample
        = (int64_t(stepped.size()) - int64_t(one.size()));
    const bool three_steps_cost_less_than_three_whole_rows
        = per_extra_sample < 3 * 5;
    CHECK(three_steps_cost_less_than_three_whole_rows);
}

TEST_CASE(
    "[Networked][Repl][Hosted] LD10 the reliable lane names its baseline by "
    "order, so the first send is whole and the next one steps"
) {
    SchemaRecord record;
    record.name = StringName("Retained");
    SchemaCore::append_column(&record, StringName("a"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("b"), SchemaCore::I16, 1);
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    SchemaCore::fix(&record);
    const WirePlan plan = WirePlan::compile(record);

    netw::repl::RetainedLane lane = netw::repl::RetainedLane::open(plan);
    CodeRow first = CodeRow::for_plan(plan);
    first.write(plan.column(0), 0, 4000);
    first.write(plan.column(1), 0, 5000);
    CodeRow next = CodeRow::for_plan(plan);
    next.write(plan.column(0), 0, 4002);
    next.write(plan.column(1), 0, 4998);

    const netw::repl::RetainedLane::Delivery opening = lane.send(7, first);
    const bool the_first_send_names_nothing = !opening.steps_from_baseline;
    CHECK(the_first_send_names_nothing);

    RowBaseline none;
    none.naming = netw::repl::BaselineNaming::BY_ORDER;
    const PackedByteArray whole
        = write_row_frame(header(opening.mask), BASE_TICK, plan, first, none);
    REQUIRE(whole.size() > 0);

    const netw::repl::RetainedLane::Delivery second = lane.send(7, next);
    const bool the_second_send_steps = second.steps_from_baseline;
    CHECK(the_second_send_steps);

    RowBaseline ordered;
    ordered.naming = netw::repl::BaselineNaming::BY_ORDER;
    ordered.row = &second.ordered_baseline;
    const PackedByteArray stepped
        = write_row_frame(header(second.mask), BASE_TICK, plan, next, ordered);
    REQUIRE(stepped.size() > 0);
    const bool the_step_is_smaller = stepped.size() < whole.size();
    CHECK(the_step_is_smaller);

    RowBaselineSource by_order;
    by_order.naming = netw::repl::BaselineNaming::BY_ORDER;
    RowFrameHeader got;
    CodeRow held = CodeRow::for_plan(plan);
    REQUIRE(
        read_row_frame(whole, BASE_TICK, ANY_LIFE, plan, got, held, by_order)
    );
    CHECK(held.equals(first));
    REQUIRE(
        read_row_frame(stepped, BASE_TICK, ANY_LIFE, plan, got, held, by_order)
    );
    CHECK(held.equals(next));
}

TEST_CASE(
    "[Networked][Repl][Hosted][Conformance] the hand-built laddered row "
    "transcribed from WIRE.md 9.5 is the row the writer emits, byte for byte"
) {
    SchemaRecord record;
    record.name = StringName("Pair");
    SchemaCore::append_column(&record, StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(&record, StringName("y"), SchemaCore::I16, 1);
    record.at(0)->delta = DeltaMode::LADDER;
    record.at(1)->delta = DeltaMode::LADDER;
    SchemaCore::fix(&record);
    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());

    CodeRow base = CodeRow::for_plan(plan);
    base.write(plan.column(0), 0, 1000);
    base.write(plan.column(1), 0, 2000);
    CodeRow moved = CodeRow::for_plan(plan);
    moved.write(plan.column(0), 0, 1003);
    moved.write(plan.column(1), 0, 1995);

    RowFrameHeader sent;
    sent.life = 5;
    sent.mask = 0b11;
    sent.reconcile_ack = -1;
    sent.tick = -1;

    PackedByteArray transcribed;
    transcribed.push_back(0x35);
    transcribed.push_back(0x55);
    transcribed.push_back(0xB2);
    transcribed.push_back(0x12);

    const PackedByteArray written
        = write_row_frame(sent, BASE_TICK, plan, moved, naming(base, 42));
    const bool same = written == transcribed;
    CHECK(same);

    BaselineRing ring;
    ring.record(42, base);
    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(
        transcribed,
        BASE_TICK,
        5,
        plan,
        got,
        into,
        arriving(ring, 45)
    ));
    NETW_CHECK_EQ(got.life, 5);
    NETW_CHECK_EQ(got.mask, uint64_t(0b11));
    NETW_CHECK_EQ(got.reconcile_ack, int64_t(-1));
    NETW_CHECK_EQ(got.tick, int64_t(-1));
    NETW_CHECK_EQ(into.read(plan.column(0), 0), 1003);
    NETW_CHECK_EQ(into.read(plan.column(1), 0), 1995);
}

} // namespace TestNetwReplRowLadder

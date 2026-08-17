// A masked row on the wire and back.
//
// The mask is load-bearing rather than advisory: a reader takes bits only where
// the mask says to, so a mask the two sides disagree about does not mislabel a
// column, it desynchronizes the stream and every column after it. Most of these
// exist to pin that.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/repl/row_frame.hpp"
#include "netw/table/schema_core.hpp"

namespace TestNetwReplRowFrame {

using godot::PackedByteArray;
using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::repl::read_row_frame;
using netw::repl::read_window_frame;
using netw::repl::WindowSample;
using netw::repl::write_window_frame;
using netw::repl::RowFrameHeader;
using netw::repl::read_plain_frame;
using netw::repl::write_plain_frame;
using netw::repl::write_row_frame;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

WirePlan triple() {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Triple");
    SchemaCore::append_column(record, godot::StringName("x"), SchemaCore::I16, 1);
    SchemaCore::append_column(record, godot::StringName("y"), SchemaCore::I16, 1);
    SchemaCore::append_column(record, godot::StringName("z"), SchemaCore::I16, 1);
    SchemaCore::fix(record);
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
    out.route = 300;
    out.comp = 4;
    out.channel = 19;
    out.tick = 41;
    out.reconcile_ack = -1;
    out.mask = p_mask;
    return out;
}

TEST_CASE("[Networked][Repl][Hosted] a frame round trips its header") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray bytes
        = write_row_frame(header(plan.full_mask()), plan, row);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_row_frame(bytes, plan, got, into));

    NETW_CHECK_EQ(got.route, 300);
    NETW_CHECK_EQ(got.comp, 4);
    NETW_CHECK_EQ(got.channel, 19);
    NETW_CHECK_EQ(got.tick, 41);
    NETW_CHECK_EQ(got.reconcile_ack, -1);
    NETW_CHECK_EQ(got.mask, plan.full_mask());
    CHECK(into.equals(row));
}

TEST_CASE(
    "[Networked][Repl][Hosted] only the masked columns ride, and the rest are "
    "left as the receiver had them"
) {
    const WirePlan plan = triple();
    const CodeRow fresh = filled(plan, 11, 22, 33);

    // Column 1 alone moved.
    const PackedByteArray bytes = write_row_frame(header(0b010), plan, fresh);
    REQUIRE(bytes.size() > 0);

    CodeRow held = filled(plan, 7, 8, 9);
    RowFrameHeader got;
    REQUIRE(read_row_frame(bytes, plan, got, held));

    // The masked column takes the sender's value and the others keep the
    // receiver's, which is what makes a masked frame applicable to the row a
    // receiver already holds rather than a whole-row replacement.
    NETW_CHECK_EQ(held.read(plan.column(0), 0), 7);
    NETW_CHECK_EQ(held.read(plan.column(1), 0), 22);
    NETW_CHECK_EQ(held.read(plan.column(2), 0), 9);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a narrower mask is a shorter frame"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    const int64_t whole
        = write_row_frame(header(plan.full_mask()), plan, row).size();
    const int64_t one = write_row_frame(header(0b001), plan, row).size();

    // The masked lane's entire reason. If a narrower mask cost the same bytes
    // there would be nothing to gain by computing one.
    CHECK(one < whole);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an empty mask is no frame at all"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    // A caught-up peer costs the pass nothing, so there is no such thing as a
    // frame carrying no columns.
    NETW_CHECK_EQ(write_row_frame(header(0), plan, row).size(), 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(read_row_frame(PackedByteArray(), plan, got, into));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a mask naming a column the plan lacks is refused"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    // Bit 3 is past this plan's three columns. Writing it would produce a
    // frame whose reader walks off the end of the plan.
    NETW_CHECK_EQ(write_row_frame(header(0b1000), plan, row).size(), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a frame with residue is refused whole"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    PackedByteArray bytes = write_row_frame(header(0b011), plan, row);
    REQUIRE(bytes.size() > 0);
    bytes.push_back(0xAB);

    // A reader that stopped early would accept a frame carrying bytes it never
    // accounted for, and a sender could hide anything after a short row.
    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(read_row_frame(bytes, plan, got, into));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a truncated frame is refused rather than read "
    "short"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray whole
        = write_row_frame(header(plan.full_mask()), plan, row);
    REQUIRE(whole.size() > 2);

    PackedByteArray cut;
    for (int64_t at = 0; at < whole.size() - 1; ++at) {
        cut.push_back(whole[at]);
    }

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(read_row_frame(cut, plan, got, into));

    // And the row it was handed is untouched, so a refused frame cannot leave
    // a receiver holding half a sender's state.
    CHECK(into.equals(CodeRow::for_plan(plan)));
}

TEST_CASE("[Networked][Repl][Hosted] a plain frame round trips every column") {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray bytes = write_plain_frame(header(0), plan, row);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    REQUIRE(read_plain_frame(bytes, plan, got, into));

    NETW_CHECK_EQ(got.route, 300);
    NETW_CHECK_EQ(got.tick, 41);
    CHECK(into.equals(row));

    // A plain frame carries no mask, so the reader answers the plan's full one
    // rather than leaving a caller to guess what it holds.
    NETW_CHECK_EQ(got.mask, plan.full_mask());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a plain frame is smaller than the masked frame "
    "that says the same thing"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);

    const int64_t masked
        = write_row_frame(header(plan.full_mask()), plan, row).size();
    const int64_t plain = write_plain_frame(header(0), plan, row).size();

    // The only reason a second frame shape exists. A plain lane sends every
    // column to everyone, so the mask would be the same full word on every
    // frame of the commonest lane, and paying for it there is the saving worth
    // a second shape.
    CHECK(plain <= masked);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a plain frame is refused on residue and on "
    "truncation"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray whole = write_plain_frame(header(0), plan, row);
    REQUIRE(whole.size() > 2);

    PackedByteArray extra = whole;
    extra.push_back(0xAB);
    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    CHECK_FALSE(read_plain_frame(extra, plan, got, into));

    PackedByteArray cut;
    for (int64_t at = 0; at < whole.size() - 1; ++at) {
        cut.push_back(whole[at]);
    }
    CHECK_FALSE(read_plain_frame(cut, plan, got, into));
}

TEST_CASE(
    "[Networked][Repl][Hosted] the two frame shapes are not interchangeable"
) {
    const WirePlan plan = triple();
    const CodeRow row = filled(plan, 11, 22, 33);
    const PackedByteArray plain = write_plain_frame(header(0), plan, row);

    // Which shape a reader is holding is decided by the channel, and reading a
    // plain frame as a masked one takes the first columns' bits as a mask. It
    // must not answer a row: a shape confusion that produced values would be a
    // desynchronization nobody could see.
    RowFrameHeader got;
    CodeRow into = CodeRow::for_plan(plan);
    const bool read_as_masked = read_row_frame(plain, plan, got, into);
    if (read_as_masked) {
        // If it parses at all it must not claim to be the row that was sent.
        CHECK_FALSE(into.equals(row));
    }
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
    "repeats, and the ticks come back absolute"
) {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> samples = window(plan, 39, 3);
    const PackedByteArray bytes = write_window_frame(header(0), plan, samples);
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;
    REQUIRE(read_window_frame(bytes, plan, got, read));

    NETW_CHECK_EQ(got.route, 300);
    NETW_CHECK_EQ(got.tick, 41);
    REQUIRE(read.size() == 3);
    // Ages ride, ticks arrive. A reader that answered ages would hand the
    // caller a number that means nothing outside this frame.
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
    const PackedByteArray one = write_window_frame(header(0), plan, window(plan, 41, 1));
    const PackedByteArray three = write_window_frame(header(0), plan, window(plan, 39, 3));
    REQUIRE(one.size() > 0);
    CHECK(three.size() > one.size());
}

TEST_CASE(
    "[Networked][Repl][Hosted] an empty window is no frame at all"
) {
    const WirePlan plan = triple();
    const godot::LocalVector<WindowSample> none;
    CHECK(write_window_frame(header(0), plan, none).is_empty());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a sample newer than the frame is refused, "
    "because a negative age reconstructs a tick nobody had"
) {
    const WirePlan plan = triple();
    godot::LocalVector<WindowSample> ahead;
    WindowSample sample;
    sample.tick = 42;
    sample.row = filled(plan, 1, 2, 3);
    ahead.push_back(sample);
    CHECK(write_window_frame(header(0), plan, ahead).is_empty());
}

TEST_CASE(
    "[Networked][Repl][Hosted] a window frame with residue or a short tail is "
    "refused whole"
) {
    const WirePlan plan = triple();
    const PackedByteArray bytes = write_window_frame(header(0), plan, window(plan, 39, 3));
    REQUIRE(bytes.size() > 0);

    RowFrameHeader got;
    godot::LocalVector<WindowSample> read;

    PackedByteArray longer = bytes;
    longer.push_back(0);
    CHECK_FALSE(read_window_frame(longer, plan, got, read));

    PackedByteArray shorter = bytes;
    shorter.resize(bytes.size() - 1);
    CHECK_FALSE(read_window_frame(shorter, plan, got, read));
    // A refusal answers nothing, so a caller cannot step the ticks it did read
    // before the frame ran out.
    NETW_CHECK_EQ(read.size(), 0);
}

} // namespace TestNetwReplRowFrame

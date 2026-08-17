// A code row against the property the whole store rests on: two rows of one
// plan are comparable by arithmetic, and the comparison names the columns that
// moved rather than the words that differ.
//
// The straddling cases are the ones that matter. A column is a run of bits at
// whatever offset the columns before it left, so runs cross word boundaries as
// a matter of course, and a reader that only handled the aligned case would
// pass every small schema and corrupt the first wide one.

#include "support/netw_test.h"

#include <cstdint>

#include "netw/table/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace TestNetwWireCodeRow {

using godot::Ref;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::ColumnPlan;
using netw::wire::WirePlan;

Ref<SchemaRecord> sealed(const godot::StringName &name, int count, int type) {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = name;
    for (int index = 0; index < count; ++index) {
        SchemaCore::append_column(
            record,
            godot::StringName(
                godot::String("c") + godot::String::num_int64(index)
            ),
            type,
            1
        );
    }
    SchemaCore::fix(record);
    return record;
}

Ref<SchemaRecord> sealed_at(const godot::StringName &name, int count, int bits);

TEST_CASE("[Networked][Wire][Hosted] a code round trips at its column width") {
    const WirePlan plan = WirePlan::compile(sealed("Row", 3, SchemaCore::I16));
    REQUIRE(plan.valid());
    CodeRow row = CodeRow::for_plan(plan);

    REQUIRE(row.write(plan.column(0), 0, 1));
    REQUIRE(row.write(plan.column(1), 0, 65535));
    REQUIRE(row.write(plan.column(2), 0, 4096));
    NETW_CHECK_EQ(row.read(plan.column(0), 0), 1);
    NETW_CHECK_EQ(row.read(plan.column(1), 0), 65535);
    NETW_CHECK_EQ(row.read(plan.column(2), 0), 4096);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a wide column round trips as packed bits"
) {
    const WirePlan plan
        = WirePlan::compile(sealed("WideRow", 1, SchemaCore::VECTOR4));
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(0).width, 128);
    CodeRow row = CodeRow::for_plan(plan);
    REQUIRE(row.write_bits(0, 64, 0xfedcba9876543210ULL));
    REQUIRE(row.write_bits(64, 64, 0x0123456789abcdefULL));

    const godot::PackedByteArray bytes = row.to_bytes();
    NETW_CHECK_EQ(bytes.size(), 16);
    const CodeRow restored = CodeRow::from_bytes(plan, bytes);
    REQUIRE(restored.valid_for(plan));
    NETW_CHECK_EQ(restored.read_bits(0, 64), 0xfedcba9876543210ULL);
    NETW_CHECK_EQ(restored.read_bits(64, 64), 0x0123456789abcdefULL);
    CHECK(row.equals(restored));

    CodeRow changed = restored;
    REQUIRE(changed.write_bits(95, 7, 0x55));
    NETW_CHECK_EQ(CodeRow::changed_mask(plan, row, changed), 1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] packed row import rejects size and dirty pad"
) {
    const WirePlan plan = WirePlan::compile(sealed_at("PaddedRow", 1, 7));
    REQUIRE(plan.valid());

    godot::PackedByteArray short_row;
    CHECK(CodeRow::from_bytes(plan, short_row).is_empty());

    godot::PackedByteArray dirty_pad;
    dirty_pad.push_back(0x80);
    CHECK(CodeRow::from_bytes(plan, dirty_pad).is_empty());

    godot::PackedByteArray clean;
    clean.push_back(0x7f);
    CHECK(CodeRow::from_bytes(plan, clean).valid_for(plan));
}

// Seals `count` columns each quantized to `bits`, so a width that does not
// divide 64 puts columns across word boundaries on purpose.
Ref<SchemaRecord> sealed_at(
    const godot::StringName &name,
    int count,
    int bits
) {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = name;
    Ref<netw::NetwQuantizeBits> codec;
    codec.instantiate();
    codec->set_bit_count(bits);
    for (int index = 0; index < count; ++index) {
        SchemaCore::append_column(
            record,
            godot::StringName(
                godot::String("q") + godot::String::num_int64(index)
            ),
            SchemaCore::F32,
            1
        );
        SchemaCore::assign_quantizer(record, index, codec);
    }
    SchemaCore::fix(record);
    return record;
}

TEST_CASE("[Networked][Wire][Hosted] a column that straddles a word survives") {
    // Seven bits does not divide 64, so column 9 spans bits 63 to 69 and every
    // column after it sits at a different offset within its word. A width that
    // divided 64 would exercise only the aligned path.
    const WirePlan plan = WirePlan::compile(sealed_at("Straddle", 20, 7));
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(9).offset, 63);
    NETW_CHECK_EQ(plan.row_bits(), 140);
    CodeRow row = CodeRow::for_plan(plan);

    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        REQUIRE(row.write(plan.column(index), 0, 0x55 & 0x7F));
    }
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        NETW_CHECK_EQ(row.read(plan.column(index), 0), 0x55 & 0x7F);
    }

    // A straddling column must not be reported unchanged when only the half
    // living in the second word moved.
    CodeRow moved = row;
    REQUIRE(moved.write(plan.column(9), 0, 0x7F));
    NETW_CHECK_EQ(CodeRow::changed_mask(plan, row, moved), uint64_t(1) << 9);
}

TEST_CASE("[Networked][Wire][Hosted] a write stays inside its own column") {
    const WirePlan plan
        = WirePlan::compile(sealed("Neighbours", 4, SchemaCore::I8));
    REQUIRE(plan.valid());
    CodeRow row = CodeRow::for_plan(plan);

    REQUIRE(row.write(plan.column(0), 0, 0xFF));
    REQUIRE(row.write(plan.column(2), 0, 0xFF));
    NETW_CHECK_EQ(row.read(plan.column(1), 0), 0);
    NETW_CHECK_EQ(row.read(plan.column(3), 0), 0);

    // Overwriting with a smaller value must clear the bits the old one set.
    REQUIRE(row.write(plan.column(0), 0, 1));
    NETW_CHECK_EQ(row.read(plan.column(0), 0), 1);
    NETW_CHECK_EQ(row.read(plan.column(1), 0), 0);
}

TEST_CASE("[Networked][Wire][Hosted] a code wider than its column is refused") {
    const WirePlan plan
        = WirePlan::compile(sealed("Narrow", 2, SchemaCore::I8));
    REQUIRE(plan.valid());
    CodeRow row = CodeRow::for_plan(plan);

    CHECK_FALSE(row.write(plan.column(0), 0, 256));
    NETW_CHECK_EQ(row.read(plan.column(0), 0), 0);
    NETW_CHECK_EQ(row.read(plan.column(1), 0), 0);
}

TEST_CASE("[Networked][Wire][Hosted] a mask names the column that moved") {
    const WirePlan plan
        = WirePlan::compile(sealed("Moved", 5, SchemaCore::I16));
    REQUIRE(plan.valid());
    CodeRow before = CodeRow::for_plan(plan);
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        REQUIRE(before.write(plan.column(index), 0, 100 + index));
    }
    CodeRow after = before;

    NETW_CHECK_EQ(CodeRow::changed_mask(plan, before, after), 0);

    REQUIRE(after.write(plan.column(3), 0, 999));
    NETW_CHECK_EQ(CodeRow::changed_mask(plan, before, after), 1 << 3);

    REQUIRE(after.write(plan.column(0), 0, 7));
    NETW_CHECK_EQ(CodeRow::changed_mask(plan, before, after), (1 << 3) | 1);
}

TEST_CASE("[Networked][Wire][Hosted] a mask does not blame a column's word") {
    // Four 16-bit columns share one word. A mask built by XOR-ing words would
    // report every column in that word when one of them moved.
    const WirePlan plan
        = WirePlan::compile(sealed("Shared", 4, SchemaCore::I16));
    REQUIRE(plan.valid());
    CodeRow before = CodeRow::for_plan(plan);
    CodeRow after = CodeRow::for_plan(plan);
    REQUIRE(after.write(plan.column(2), 0, 5));

    NETW_CHECK_EQ(CodeRow::changed_mask(plan, before, after), 1 << 2);
}

TEST_CASE("[Networked][Wire][Hosted] a strided column moves as one column") {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = "Strided";
    SchemaCore::append_column(record, "solo", SchemaCore::I16, 1);
    SchemaCore::append_column(record, "many", SchemaCore::I16, 4);
    REQUIRE(SchemaCore::fix(record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    CodeRow before = CodeRow::for_plan(plan);
    CodeRow after = CodeRow::for_plan(plan);

    // The third element of the second column, which is one bit of one mask.
    REQUIRE(after.write(plan.column(1), 2, 77));
    NETW_CHECK_EQ(CodeRow::changed_mask(plan, before, after), 1 << 1);
    NETW_CHECK_EQ(after.read(plan.column(1), 2), 77);
    NETW_CHECK_EQ(after.read(plan.column(1), 1), 0);
}

TEST_CASE("[Networked][Wire][Hosted] an element past the stride is refused") {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = "Bounded";
    SchemaCore::append_column(record, "pair", SchemaCore::I16, 2);
    REQUIRE(SchemaCore::fix(record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    CodeRow row = CodeRow::for_plan(plan);

    CHECK_FALSE(row.write(plan.column(0), 2, 1));
    CHECK_FALSE(row.write(plan.column(0), -1, 1));
    NETW_CHECK_EQ(row.read(plan.column(0), 2), 0);
}

TEST_CASE("[Networked][Wire][Hosted] an invalid plan yields an empty row") {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = "Loose";
    SchemaCore::append_column(record, "anything", SchemaCore::VARIANT, 1);
    REQUIRE(SchemaCore::fix(record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE_FALSE(plan.valid());
    const CodeRow row = CodeRow::for_plan(plan);
    CHECK(row.is_empty());
}

TEST_CASE(
    "[Networked][Wire][Hosted] a schema wider than the mask has no plan"
) {
    // The mask is one word, so a plan that admitted a 65th column would mask
    // it short and report it unchanged forever.
    CHECK(WirePlan::compile(sealed("Wide", 64, SchemaCore::BOOL)).valid());
    CHECK_FALSE(
        WirePlan::compile(sealed("Wider", 65, SchemaCore::BOOL)).valid()
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] a diff it cannot take names every column, not "
    "none"
) {
    const WirePlan plan
        = WirePlan::compile(sealed("Foreign", 4, SchemaCore::I16));
    REQUIRE(plan.valid());
    CodeRow good = CodeRow::for_plan(plan);
    REQUIRE(good.write(plan.column(0), 0, 11));
    CodeRow foreign;

    // A row that is not this plan's cannot be diffed, and the answer decides
    // what the lane does about it. Zero reads as a caught-up peer and costs
    // the pass nothing, so a row the sender cannot interpret would strand the
    // receiver silently and for good. Every column is the answer the baseline
    // book already gives a peer whose baseline it does not hold: when the diff
    // is unknown, send the row.
    NETW_CHECK_EQ(
        CodeRow::changed_mask(plan, foreign, good),
        plan.full_mask()
    );
    NETW_CHECK_EQ(
        CodeRow::changed_mask(plan, good, foreign),
        plan.full_mask()
    );
}

} // namespace TestNetwWireCodeRow

// What the apply boundary refuses, and the fixed point it has to reach.
//
// Decoding is where a receiver decides what the sender meant, and it has no
// second opinion available: there are no type tags on the wire, so a row read
// against the wrong declaration produces values rather than an error. The
// guards here are the only thing standing between a mismatched declaration and
// a confident wrong number in the game.
//
// The last law is the one the masked lane rests on. A masked send diffs
// against what the sender believes the receiver holds, so sender and receiver
// have to agree about what a row MEANS after one trip through the grid. If
// re-encoding a decoded row moved it, the two would drift by one grid step per
// pass and the diff would name columns that never changed.

#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/quantize.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/value_row.hpp"

namespace TestNetwWireApplyRefusal {

using godot::Array;
using godot::Ref;
using netw::NetwQuantizeBits;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::decode_scalar_row;
using netw::wire::encode_scalar_row;
using netw::wire::WirePlan;

Ref<SchemaRecord> scalar(int type, int stride = 1) {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Probe");
    SchemaCore::append_column(record, godot::StringName("a"), type, stride);
    SchemaCore::fix(record);
    return record;
}

Ref<SchemaRecord> gridded(double low, double high, int bits) {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Gridded");
    SchemaCore::append_column(
        record,
        godot::StringName("axis"),
        SchemaCore::F32,
        1
    );
    Ref<NetwQuantizeBits> packer;
    packer.instantiate();
    packer->bits(bits);
    packer->limits(low, high);
    record->at(0)->quantizer = packer;
    SchemaCore::fix(record);
    return record;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a row that is not the schema's is refused"
) {
    const Ref<SchemaRecord> narrow = scalar(SchemaCore::I32);
    const Ref<SchemaRecord> wide = scalar(SchemaCore::I64);

    CodeRow row = CodeRow::for_plan(WirePlan::compile(narrow));
    Array values;
    values.push_back(1);
    REQUIRE(encode_scalar_row(narrow, values, row));

    Array out;
    out.push_back(godot::StringName("sentinel"));
    CHECK_FALSE(decode_scalar_row(wide, row, out));

    // The caller's array is left as it was. A partly written one would be a
    // set of values that never existed together on the sender.
    NETW_CHECK_EQ(out.size(), 1);
    CHECK(godot::String(out[0]) == godot::String("sentinel"));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a strided schema has no scalar apply"
) {
    const Ref<SchemaRecord> schema = scalar(SchemaCore::I32, 4);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));
    Array out;
    CHECK_FALSE(decode_scalar_row(schema, row, out));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] an empty row is not a row of zeros"
) {
    const Ref<SchemaRecord> schema = scalar(SchemaCore::I32);
    CodeRow empty;
    Array out;

    // A row nobody built decodes to nothing rather than to a plausible zero.
    // Zero is a value a column can legitimately hold, so answering it here
    // would hand the game a reading it never received.
    CHECK_FALSE(decode_scalar_row(schema, empty, out));
    NETW_CHECK_EQ(out.size(), 0);
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a decoded row re-encodes to the row it "
    "came from"
) {
    const Ref<SchemaRecord> schema = gridded(-512.0, 512.0, 16);
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());

    // Values deliberately off the grid, since a value already on it would
    // reach the fixed point without the round trip proving anything.
    const double offered[] = { 0.0, 1.0 / 3.0, -7.77, 511.4, -511.9 };
    for (double value : offered) {
        CodeRow first = CodeRow::for_plan(plan);
        Array values;
        values.push_back(value);
        REQUIRE(encode_scalar_row(schema, values, first));

        Array decoded;
        REQUIRE(decode_scalar_row(schema, first, decoded));
        REQUIRE(decoded.size() == 1);

        CodeRow second = CodeRow::for_plan(plan);
        REQUIRE(encode_scalar_row(schema, decoded, second));

        // One trip through the grid reaches the fixed point, so sender and
        // receiver agree about what the row means from the first pass. Without
        // this the masked diff would name a column every pass and the two
        // would walk apart one step at a time.
        CHECK(first.equals(second));

        Array again;
        REQUIRE(decode_scalar_row(schema, second, again));
        CHECK(double(again[0]) == double(decoded[0]));
    }
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a value past its grid's limits still "
    "reaches the fixed point"
) {
    const Ref<SchemaRecord> schema = gridded(-4.0, 4.0, 8);
    const WirePlan plan = WirePlan::compile(schema);

    // A caller can offer a value outside the range it declared. Whatever the
    // grid does with it, sender and receiver have to do the SAME thing, or the
    // clamp itself becomes a source of drift.
    for (double value : { -9.0, 9.0 }) {
        CodeRow first = CodeRow::for_plan(plan);
        Array values;
        values.push_back(value);
        REQUIRE(encode_scalar_row(schema, values, first));

        Array decoded;
        REQUIRE(decode_scalar_row(schema, first, decoded));

        CodeRow second = CodeRow::for_plan(plan);
        REQUIRE(encode_scalar_row(schema, decoded, second));
        CHECK(first.equals(second));
    }
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a decode that fails partway writes none of "
    "the row"
) {
    // Three columns, and the middle one carries a quantizer that cannot answer
    // for its declared type. The refusal is therefore discovered AFTER the
    // first column has already been read, which is the case a top-of-function
    // guard does not reach.
    Ref<SchemaRecord> schema;
    schema.instantiate();
    schema->name = godot::StringName("Partial");
    SchemaCore::append_column(schema, godot::StringName("a"), SchemaCore::I32, 1);
    SchemaCore::append_column(schema, godot::StringName("b"), SchemaCore::BOOL, 1);
    SchemaCore::append_column(schema, godot::StringName("c"), SchemaCore::I32, 1);

    Ref<NetwQuantizeBits> packer;
    packer.instantiate();
    packer->bits(8);
    packer->limits(0.0, 1.0);
    REQUIRE_FALSE(packer->supports_type(godot::Variant::BOOL));
    schema->at(1)->quantizer = packer;
    SchemaCore::fix(schema);

    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());
    CodeRow row = CodeRow::for_plan(plan);

    Array out;
    out.push_back(godot::StringName("sentinel"));
    ERR_PRINT_OFF;
    CHECK_FALSE(decode_scalar_row(schema, row, out));
    ERR_PRINT_ON;

    // The caller keeps exactly what it had. A row applied up to the column
    // that failed would put a state on the receiver that never existed on the
    // sender, and nothing later would correct it: the sender believes it sent
    // a whole row and diffs against that belief.
    NETW_CHECK_EQ(out.size(), 1);
    CHECK(godot::String(out[0]) == godot::String("sentinel"));
}

} // namespace TestNetwWireApplyRefusal

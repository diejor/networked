// What the gather boundary refuses, and why each refusal is a refusal rather
// than a best effort.
//
// `encode_scalar_row` is the one place a Godot value becomes code-row
// currency, and everything below it reads codes without re-deriving what they
// meant. So a value this boundary lets through wrongly is not caught later: it
// is transmitted, decoded against the same declaration, and lands on the
// receiver as a confident wrong number. Every guard here answers false and
// writes nothing, because a partially gathered row is the one outcome that
// would put a torn value on the wire.
//
// The sibling suite covers what the boundary ACCEPTS. This covers what it must
// not, which is the half a shadow of this stage rests on: a shadow can only
// report that two implementations agreed, and two implementations that both
// accept a malformed row agree about nothing worth having.

#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/quantize.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/value_row.hpp"

namespace TestNetwWireGatherRefusal {

using godot::Array;
using godot::Ref;
using netw::NetwQuantizeBits;
using netw::SchemaCore;
using netw::SchemaColumn;
using netw::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::encode_scalar_row;
using netw::wire::WirePlan;

Ref<SchemaRecord> sealed(int type, int stride = 1) {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = godot::StringName("Probe");
    SchemaCore::append_column(record, godot::StringName("a"), type, stride);
    SchemaCore::fix(record);
    return record;
}

Array one(const godot::Variant &value) {
    Array out;
    out.push_back(value);
    return out;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a row with the wrong number of values is "
    "refused"
) {
    const Ref<SchemaRecord> schema = sealed(SchemaCore::I32);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));

    Array none;
    ERR_PRINT_OFF;
    CHECK_FALSE(encode_scalar_row(schema, none, row));

    Array two;
    two.push_back(1);
    two.push_back(2);
    CHECK_FALSE(encode_scalar_row(schema, two, row));
    ERR_PRINT_ON;

    CHECK(encode_scalar_row(schema, one(7), row));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a value that is not its declared type is "
    "refused rather than coerced"
) {
    const Ref<SchemaRecord> schema = sealed(SchemaCore::I32);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));

    ERR_PRINT_OFF;
    CHECK_FALSE(encode_scalar_row(schema, one(godot::Vector2(1, 2)), row));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a quantizer that does not support its "
    "column's type is refused"
) {
    Ref<SchemaRecord> schema;
    schema.instantiate();
    schema->name = godot::StringName("Probe");
    SchemaCore::append_column(
        schema,
        godot::StringName("flag"),
        SchemaCore::BOOL,
        1
    );
    Ref<NetwQuantizeBits> packer;
    packer.instantiate();
    packer->bits(8);
    packer->limits(0.0, 1.0);
    // A bit packer grids a real number. Handing it a bool is a declaration
    // error, and the boundary is where it has to surface: below here the row
    // is codes and nothing remembers what they were made from.
    REQUIRE_FALSE(packer->supports_type(godot::Variant::BOOL));
    schema->at(0)->quantizer = packer;
    SchemaCore::fix(schema);

    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));
    ERR_PRINT_OFF;
    CHECK_FALSE(encode_scalar_row(schema, one(true), row));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a strided column has no scalar gather"
) {
    const Ref<SchemaRecord> schema = sealed(SchemaCore::I32, 4);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));

    // One Variant does not declare how its elements are indexed, so a strided
    // column's gather belongs to the column store that owns that mapping. The
    // refusal is what keeps the two from both claiming it.
    ERR_PRINT_OFF;
    CHECK_FALSE(encode_scalar_row(schema, one(1), row));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a self-describing column has no plan and "
    "so no row"
) {
    const Ref<SchemaRecord> schema = sealed(SchemaCore::VARIANT);
    const WirePlan plan = WirePlan::compile(schema);

    // A fixed-width plan cannot hold a column whose width is a property of the
    // value. The plan says so first, and the gather has to agree rather than
    // inventing a size.
    CHECK_FALSE(plan.valid());

    CodeRow row;
    ERR_PRINT_OFF;
    CHECK_FALSE(encode_scalar_row(schema, one(godot::Variant(7)), row));
    ERR_PRINT_ON;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a refused gather leaves the row it was "
    "handed alone"
) {
    const Ref<SchemaRecord> schema = sealed(SchemaCore::I32);
    const WirePlan plan = WirePlan::compile(schema);
    CodeRow row = CodeRow::for_plan(plan);

    REQUIRE(encode_scalar_row(schema, one(1234), row));
    const uint64_t kept = row.read(plan.column(0), 0);

    // The row a caller hands in is usually the one it will send. A refusal
    // that half-wrote it would put a torn value on the wire under a mask that
    // says the column moved.
    ERR_PRINT_OFF;
    CHECK_FALSE(encode_scalar_row(schema, one(godot::Vector2(1, 2)), row));
    ERR_PRINT_ON;
    NETW_CHECK_EQ(row.read(plan.column(0), 0), kept);
}

} // namespace TestNetwWireGatherRefusal

#include "support/fault_log.h"

#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace TestNetwWireGatherRefusal {

using godot::Array;
using godot::Ref;
using netw::NetwQuantizeScalar;
using netw::SchemaCore;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::encode_scalar_row;
using netw::wire::WirePlan;

SchemaRecord sealed(int type, int stride = 1) {
    SchemaRecord record;
    record.name = godot::StringName("Probe");
    SchemaCore::append_column(&record, godot::StringName("a"), type, stride);
    SchemaCore::fix(&record);
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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][wire] Condition \"!scalar_schema(p_schema, plan) ||"
        " p_values.size() != p_schema.column_count()\" is true. "
        "Returning: false. The row's values do not match its "
        "schema. Pass one value for each column.",
        2
    );

    SchemaRecord schema = sealed(SchemaCore::I32);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));

    Array none;
    CHECK_FALSE(encode_scalar_row(schema, none, row));

    Array two;
    two.push_back(1);
    two.push_back(2);
    CHECK_FALSE(encode_scalar_row(schema, two, row));

    CHECK(encode_scalar_row(schema, one(7), row));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a value that is not its declared type is "
    "refused rather than coerced"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][wire] Condition "
        "\"!Variant::can_convert_strict(p_value.get_type(), "
        "expected)\" is true. Returning: false. Column 0 got a value"
        " of the wrong type. Write the type its schema declares.",
        1
    );

    SchemaRecord schema = sealed(SchemaCore::I32);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));
    CHECK_FALSE(encode_scalar_row(schema, one(godot::Vector2(1, 2)), row));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a quantizer that does not support its "
    "column's type is refused"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][wire] Condition "
        "\"!p_column.quantizer->supports_type(expected)\" is true. "
        "Returning: false. Column 0 has a quantizer that does not "
        "support its type. Use a quantizer made for that type.",
        1
    );

    SchemaRecord schema;
    schema.name = godot::StringName("Probe");
    SchemaCore::append_column(
        &schema,
        godot::StringName("flag"),
        SchemaCore::BOOL,
        1
    );
    Ref<NetwQuantizeScalar> packer;
    packer.instantiate();
    packer->bits(8);
    packer->limits(0.0, 1.0);
    REQUIRE_FALSE(packer->supports_type(godot::Variant::BOOL));
    schema.at(0)->quantizer = packer;
    SchemaCore::fix(&schema);

    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));
    CHECK_FALSE(encode_scalar_row(schema, one(true), row));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a strided column has no scalar gather"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][wire] Condition \"!scalar_schema(p_schema, plan) ||"
        " p_values.size() != p_schema.column_count()\" is true. "
        "Returning: false. The row's values do not match its "
        "schema. Pass one value for each column.",
        1
    );

    SchemaRecord schema = sealed(SchemaCore::I32, 4);
    CodeRow row = CodeRow::for_plan(WirePlan::compile(schema));
    CHECK_FALSE(encode_scalar_row(schema, one(1), row));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a self-describing column carries the "
    "value it is handed and refuses one no row can hold"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][wire] Condition \"!textual || "
        "SchemaCore::validate_storable(p_value) != OK\" is true. "
        "Returning: false. Column 0 got a value that cannot be "
        "sent. Use a plain value, not an Object.",
        1
    );

    SchemaRecord schema = sealed(SchemaCore::VARIANT);
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());

    godot::PackedVector2Array ink;
    ink.push_back(godot::Vector2(1, 2));
    ink.push_back(godot::Vector2(3, 4));
    CodeRow row;
    REQUIRE(encode_scalar_row(schema, one(ink), row));
    Array decoded;
    REQUIRE(netw::wire::decode_scalar_row(schema, row, decoded));
    REQUIRE(decoded.size() == 1);
    CHECK((decoded[0] == godot::Variant(ink)));

    Ref<NetwQuantizeScalar> object;
    object.instantiate();
    CHECK_FALSE(encode_scalar_row(schema, one(object), row));

    CHECK_FALSE(WirePlan::compile(sealed(SchemaCore::VARIANT, 2)).valid());
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a refused gather leaves the row it was "
    "handed alone"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][wire] Condition "
        "\"!Variant::can_convert_strict(p_value.get_type(), "
        "expected)\" is true. Returning: false. Column 0 got a value"
        " of the wrong type. Write the type its schema declares.",
        1
    );

    SchemaRecord schema = sealed(SchemaCore::I32);
    const WirePlan plan = WirePlan::compile(schema);
    CodeRow row = CodeRow::for_plan(plan);

    REQUIRE(encode_scalar_row(schema, one(1234), row));
    const uint64_t kept = row.read(plan.column(0), 0);
    CHECK_FALSE(encode_scalar_row(schema, one(godot::Vector2(1, 2)), row));
    NETW_CHECK_EQ(row.read(plan.column(0), 0), kept);
}

} // namespace TestNetwWireGatherRefusal

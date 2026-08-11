// The only Variant boundary in the native wire path: a sealed scalar schema
// quantizes once into a packed CodeRow and decodes without type tags.

#include "support/netw_test.h"

#include <cmath>

#include "netw/quantize.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/value_row.hpp"

namespace TestNetwWireValueRow {

using namespace godot;
using netw::NetwQuantizeBits;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

Ref<SchemaRecord> record_of(const StringName &p_name) {
    Ref<SchemaRecord> record;
    record.instantiate();
    record->name = p_name;
    return record;
}

TEST_CASE(
    "[Networked][WireValue][Hosted] raw scalars and wide vectors share one row"
) {
    const Ref<SchemaRecord> schema = record_of("RawInput");
    SchemaCore::append_column(schema, "gear", SchemaCore::I8, 1);
    SchemaCore::append_column(schema, "throttle", SchemaCore::F32, 1);
    SchemaCore::append_column(schema, "axes", SchemaCore::VECTOR4, 1);
    REQUIRE(SchemaCore::fix(schema) == Error::OK);
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.row_bits(), 168);

    Array values;
    values.push_back(-5);
    values.push_back(0.25);
    values.push_back(Vector4(1.0, -2.0, 3.5, -4.25));
    CodeRow row;
    REQUIRE(netw::wire::encode_scalar_row(schema, values, row));
    REQUIRE(row.valid_for(plan));
    NETW_CHECK_EQ(row.to_bytes().size(), 21);

    Array decoded;
    REQUIRE(netw::wire::decode_scalar_row(schema, row, decoded));
    NETW_CHECK_EQ(int64_t(decoded[0]), -5);
    CHECK(std::abs(double(decoded[1]) - 0.25) < 0.000001);
    CHECK(Vector4(decoded[2]) == Vector4(1.0, -2.0, 3.5, -4.25));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] quantizers define the row codes once"
) {
    const Ref<SchemaRecord> schema = record_of("QuantizedInput");
    SchemaCore::append_column(schema, "move", SchemaCore::VECTOR2, 1);
    Ref<NetwQuantizeBits> quantizer;
    quantizer.instantiate();
    quantizer->set_bit_count(8);
    SchemaCore::assign_quantizer(schema, 0, quantizer);
    SchemaCore::append_column(schema, "jump", SchemaCore::BOOL, 1);
    REQUIRE(SchemaCore::fix(schema) == Error::OK);
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.row_bits(), 17);

    Array values;
    values.push_back(Vector2(0.5, -0.5));
    values.push_back(true);
    CodeRow first;
    CodeRow second;
    REQUIRE(netw::wire::encode_scalar_row(schema, values, first));
    REQUIRE(netw::wire::encode_scalar_row(schema, values, second));
    CHECK(first.equals(second));
    NETW_CHECK_EQ(first.to_bytes().size(), 3);

    Array decoded;
    REQUIRE(netw::wire::decode_scalar_row(schema, first, decoded));
    const Vector2 move = decoded[0];
    CHECK(std::abs(move.x - 0.5) < 0.01);
    CHECK(std::abs(move.y + 0.5) < 0.01);
    CHECK(bool(decoded[1]));
}

TEST_CASE(
    "[Networked][WireValue][Hosted] a row is meaningful only beside its plan"
) {
    const Ref<SchemaRecord> narrow = record_of("Narrow");
    SchemaCore::append_column(narrow, "value", SchemaCore::I8, 1);
    REQUIRE(SchemaCore::fix(narrow) == Error::OK);
    Array values;
    values.push_back(7);
    CodeRow row;
    REQUIRE(netw::wire::encode_scalar_row(narrow, values, row));

    const Ref<SchemaRecord> wide = record_of("Wide");
    SchemaCore::append_column(wide, "value", SchemaCore::I16, 1);
    REQUIRE(SchemaCore::fix(wide) == Error::OK);
    Array unchanged;
    unchanged.push_back("sentinel");
    CHECK_FALSE(netw::wire::decode_scalar_row(wide, row, unchanged));
    NETW_CHECK_EQ(unchanged.size(), 1);
    CHECK(String(unchanged[0]) == "sentinel");
}

} // namespace TestNetwWireValueRow

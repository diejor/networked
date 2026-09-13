#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/schema_core.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/stream.hpp"

using namespace godot;

namespace TestNetwWirePlan {

using godot::Ref;
using netw::SchemaCore;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::ColumnPlan;
using netw::wire::DeltaMode;
using netw::wire::MeasureStream;
using netw::wire::WirePlan;
using netw::wire::WriteStream;

SchemaRecord record_of(const godot::StringName &name) {
    SchemaRecord made;
    made.name = name;
    return made;
}

void add(
    SchemaRecord &record,
    const godot::StringName &key,
    int type,
    int stride = 1
) {
    SchemaCore::append_column(&record, key, type, stride);
}

int64_t spend(const WirePlan &plan) {
    MeasureStream measurer;
    uint64_t value = 0;
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        const ColumnPlan &slot = plan.column(index);
        for (int element = 0; element < slot.stride; ++element) {
            measurer.bits(value, slot.width);
        }
    }
    return measurer.bit_length();
}

TEST_CASE("[Networked][Wire][Hosted] a plan derives a width from each type") {
    SchemaRecord record = record_of("Shapes");
    add(record, "a", SchemaCore::BOOL);
    add(record, "b", SchemaCore::I16);
    add(record, "c", SchemaCore::VECTOR3);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column_count(), 3);
    NETW_CHECK_EQ(plan.column(0).width, 1);
    NETW_CHECK_EQ(plan.column(1).width, 16);
    NETW_CHECK_EQ(plan.column(2).width, 32);
    NETW_CHECK_EQ(plan.column(2).stride, 3);
    NETW_CHECK_EQ(plan.row_bits(), 113);
    NETW_CHECK_EQ(plan.mask_width(), 3);
}

TEST_CASE("[Networked][Wire][Hosted] a quantizer decides its column's width") {
    SchemaRecord record = record_of("Quantized");
    add(record, "raw", SchemaCore::F32);
    add(record, "packed", SchemaCore::F32);
    Ref<netw::NetwQuantizeScalar> codec;
    codec.instantiate();
    codec->set_bit_count(11);
    SchemaCore::assign_quantizer(&record, 1, codec);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(0).width, 32);
    NETW_CHECK_EQ(plan.column(1).width, 11);
    NETW_CHECK_EQ(plan.row_bits(), 43);
}

TEST_CASE("[Networked][Wire][Hosted] a strided column is N elements wide") {
    SchemaRecord record = record_of("Strided");
    add(record, "one", SchemaCore::I32, 1);
    add(record, "four", SchemaCore::I32, 4);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(1).stride, 4);
    NETW_CHECK_EQ(plan.column(1).bits(), 128);
    NETW_CHECK_EQ(plan.row_bits(), 160);
}

TEST_CASE("[Networked][Wire][Hosted] the plan spends what it says it spends") {
    SchemaRecord record = record_of("Priced");
    add(record, "flag", SchemaCore::BOOL);
    add(record, "pos", SchemaCore::VECTOR2);
    add(record, "ids", SchemaCore::I16, 3);
    Ref<netw::NetwQuantizeScalar> codec;
    codec.instantiate();
    codec->set_bit_count(9);
    SchemaCore::assign_quantizer(&record, 1, codec);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.row_bits(), 67);
    NETW_CHECK_EQ(spend(plan), 67);
}

TEST_CASE("[Networked][Wire][Hosted] a self-describing column has no plan") {
    SchemaRecord record = record_of("Loose");
    add(record, "solid", SchemaCore::I32);
    add(record, "anything", SchemaCore::VARIANT);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    CHECK_FALSE(plan.valid());
    NETW_CHECK_EQ(plan.row_bits(), 0);
}

TEST_CASE("[Networked][Wire][Hosted] an unsealed record has no plan") {
    SchemaRecord record = record_of("Open");
    add(record, "a", SchemaCore::I32);

    CHECK_FALSE(WirePlan::compile(record).valid());
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);
    CHECK(WirePlan::compile(record).valid());
}

TEST_CASE(
    "[Networked][Wire][Hosted] a plan carries the column's declared mode"
) {
    SchemaRecord record = record_of("Modes");
    add(record, "a", SchemaCore::I32);
    add(record, "b", SchemaCore::F32);
    record.at(1)->delta = DeltaMode::LADDER;
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    CHECK(plan.column(0).delta == DeltaMode::FULL);
    CHECK(plan.column(1).delta == DeltaMode::LADDER);
    const bool wide_one_ladders = plan.column(1).laddered();
    CHECK(wide_one_ladders);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a column of four bits or fewer never ladders"
) {
    SchemaRecord record = record_of("Narrow");
    add(record, "flag", SchemaCore::BOOL);
    record.at(0)->delta = DeltaMode::LADDER;
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    CHECK(plan.column(0).width == 1);
    const bool narrow_one_ladders = plan.column(0).laddered();
    CHECK_FALSE(narrow_one_ladders);
}

TEST_CASE("[Networked][Wire][Hosted] two peers on one record get one plan") {
    SchemaRecord here = record_of("Shared");
    add(here, "pos", SchemaCore::VECTOR2);
    add(here, "hp", SchemaCore::U8);
    REQUIRE(SchemaCore::fix(&here) == godot::Error::OK);

    SchemaRecord there = record_of("Shared");
    add(there, "pos", SchemaCore::VECTOR2);
    add(there, "hp", SchemaCore::U8);
    REQUIRE(SchemaCore::fix(&there) == godot::Error::OK);

    const WirePlan mine = WirePlan::compile(here);
    const WirePlan yours = WirePlan::compile(there);
    NETW_CHECK_EQ(mine.row_bits(), yours.row_bits());
    NETW_CHECK_EQ(mine.column_count(), yours.column_count());
    NETW_CHECK_EQ(here.shape_hash, there.shape_hash);
}

TEST_CASE(
    "[Networked][Wire][Hosted] hand-built prose frame decodes against schema "
    "plan"
) {
    SchemaRecord record = record_of("ReferenceFrame");
    add(record, "flags", SchemaCore::BOOL);
    add(record, "pos_x", SchemaCore::F32);
    add(record, "pos_y", SchemaCore::F32);

    Ref<netw::NetwQuantizeScalar> q_x;
    q_x.instantiate();
    q_x->set_bit_count(11);
    SchemaCore::assign_quantizer(&record, 1, q_x);

    Ref<netw::NetwQuantizeScalar> q_y;
    q_y.instantiate();
    q_y->set_bit_count(11);
    SchemaCore::assign_quantizer(&record, 2, q_y);

    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.row_bits(), 23);

    netw::wire::WriteStream writer;
    bool flag_in = true;
    uint64_t x_in = 1024;
    uint64_t y_in = 512;

    REQUIRE(writer.bool1(flag_in));
    REQUIRE(writer.bits(x_in, plan.column(1).width));
    REQUIRE(writer.bits(y_in, plan.column(2).width));
    REQUIRE(writer.align_verify());

    godot::PackedByteArray bytes = writer.to_bytes();
    REQUIRE(bytes.size() == 3);
    NETW_CHECK_EQ(bytes[0], 0x01);
    NETW_CHECK_EQ(bytes[1], 0x08);
    NETW_CHECK_EQ(bytes[2], 0x20);

    netw::wire::ReadStream reader(bytes);

    bool flag_val = false;
    uint64_t x_val = 0;
    uint64_t y_val = 0;

    REQUIRE(reader.bool1(flag_val));
    REQUIRE(reader.bits(x_val, plan.column(1).width));
    REQUIRE(reader.bits(y_val, plan.column(2).width));
    REQUIRE(reader.align_verify());

    CHECK(flag_val);
    NETW_CHECK_EQ(x_val, 1024);
    NETW_CHECK_EQ(y_val, 512);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an unquantized composite is its scalar element "
    "repeated, because no element wider than 64 bits can reach the stream"
) {
    SchemaRecord record = record_of("Composites");
    add(record, "two", SchemaCore::VECTOR2);
    add(record, "three", SchemaCore::VECTOR3);
    add(record, "four", SchemaCore::VECTOR4);
    add(record, "tint", SchemaCore::COLOR);
    add(record, "spin", SchemaCore::QUATERNION);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        NETW_CHECK_EQ(plan.column(index).width, 32);
    }
    NETW_CHECK_EQ(plan.column(0).stride, 2);
    NETW_CHECK_EQ(plan.column(1).stride, 3);
    NETW_CHECK_EQ(plan.column(2).stride, 4);
    NETW_CHECK_EQ(plan.column(3).stride, 4);
    NETW_CHECK_EQ(plan.column(4).stride, 4);
    NETW_CHECK_EQ(plan.row_bits(), 544);
    NETW_CHECK_EQ(spend(plan), 544);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a declared stride multiplies a composite's "
    "elements, so an array of three vectors is nine elements and not three"
) {
    SchemaRecord record = record_of("StridedComposite");
    add(record, "path", SchemaCore::VECTOR3, 3);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(0).width, 32);
    NETW_CHECK_EQ(plan.column(0).stride, 9);
    NETW_CHECK_EQ(plan.row_bits(), 288);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ENTITY column is thirty two bits, because a "
    "route plus one is what it carries and null is the zero"
) {
    SchemaRecord record = record_of("Referrer");
    add(record, "target", SchemaCore::ENTITY);
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column(0).width, 32);
    NETW_CHECK_EQ(plan.column(0).stride, 1);
    NETW_CHECK_EQ(plan.row_bits(), 32);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a record past the column limit refuses to "
    "seal, so an author learns at authoring time and not at send time"
) {
    SchemaRecord record = record_of("TooWide");
    for (int index = 0; index <= SchemaCore::MAX_COLUMNS; ++index) {
        add(record,
            godot::StringName(String::num_int64(index)),
            SchemaCore::U8);
    }

    const bool refused = SchemaCore::fix(&record) != godot::Error::OK;
    CHECK(refused);
    CHECK_FALSE(record.sealed);
    CHECK_FALSE(WirePlan::compile(record).valid());
}

TEST_CASE(
    "[Networked][Wire][Hosted] a record at the column limit seals and plans, "
    "so the limit admits the widest mask a row can carry"
) {
    SchemaRecord record = record_of("WidestMask");
    for (int index = 0; index < SchemaCore::MAX_COLUMNS; ++index) {
        add(record,
            godot::StringName(String::num_int64(index)),
            SchemaCore::U8);
    }
    REQUIRE(SchemaCore::fix(&record) == godot::Error::OK);

    const WirePlan plan = WirePlan::compile(record);
    REQUIRE(plan.valid());
    NETW_CHECK_EQ(plan.column_count(), SchemaCore::MAX_COLUMNS);
    CHECK(plan.full_mask() == ~uint64_t(0));
}

} // namespace TestNetwWirePlan

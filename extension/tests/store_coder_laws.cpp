#include "support/netw_test.h"

#include <cstdint>
#include <cstring>

#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/store/coder.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace TestNetwStoreCoder {

using netw::SchemaCore;
using netw::store::Coder;
using netw::store::ColumnCoder;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

struct Spec {
    int type = SchemaCore::F32;
    int quantizer = 0;
};

constexpr int RAW = 0;
constexpr int SCALAR = 1;
constexpr int TURN = 2;

SchemaRecord record_of(const Spec &p_spec) {
    SchemaRecord record;
    SchemaColumn column;
    column.key = StringName("value");
    column.type = p_spec.type;
    if (p_spec.quantizer == SCALAR) {
        Ref<netw::NetwQuantizeScalar> grid;
        grid.instantiate();
        grid->set_bit_count(p_spec.type == SchemaCore::VECTOR3 ? 20 : 10);
        grid->set_min_limit(-3.0);
        grid->set_max_limit(5.0);
        column.quantizer = grid;
    } else if (p_spec.quantizer == TURN) {
        Ref<netw::NetwQuantizeQuaternion> turn;
        turn.instantiate();
        turn->set_bit_count(16);
        column.quantizer = turn;
    }
    record.columns.push_back(column);
    SchemaCore::fix(&record);
    return record;
}

Array samples_of(int p_type) {
    Array out;
    switch (p_type) {
        case SchemaCore::F32:
        case SchemaCore::F64:
            out.push_back(0.0);
            out.push_back(1.0 / 3.0);
            out.push_back(-2.71828);
            out.push_back(9.5);
            out.push_back(-1e30);
            return out;
        case SchemaCore::BOOL:
            out.push_back(true);
            out.push_back(false);
            return out;
        case SchemaCore::VECTOR2:
            out.push_back(Vector2(0.125, -7.0));
            out.push_back(Vector2(1.0 / 3.0, 4.99));
            return out;
        case SchemaCore::VECTOR3:
            out.push_back(Vector3(0.001, -2.5, 4.75));
            out.push_back(Vector3(-9.0, 1.0 / 7.0, 0.0));
            return out;
        case SchemaCore::VECTOR4:
            out.push_back(Vector4(1.5, -0.25, 3.0, 1.0 / 9.0));
            return out;
        case SchemaCore::COLOR:
            out.push_back(Color(0.2, 0.4, 0.6, 1.0 / 3.0));
            return out;
        case SchemaCore::QUATERNION:
            out.push_back(Quaternion());
            out.push_back(
                Quaternion(Vector3(0.3, 0.9, -0.1).normalized(), 2.1)
            );
            out.push_back(Quaternion(-0.5, 0.5, -0.5, -0.5));
            return out;
        default:
            out.push_back(int64_t(0));
            out.push_back(int64_t(5));
            out.push_back(int64_t(-3));
            out.push_back(int64_t(300));
            out.push_back(int64_t(-70000));
            return out;
    }
}

bool agrees(const Spec &p_spec, const Variant &p_value) {
    const SchemaRecord record = record_of(p_spec);
    const WirePlan plan = WirePlan::compile(record);
    const SchemaColumn &column = *record.at(0);
    const ColumnCoder coder = netw::store::coder_of(column, plan.column(0));
    if (coder.kind == Coder::VARIANT) {
        return false;
    }
    CodeRow coded = CodeRow::for_plan(plan);
    const bool encoded = netw::wire::encode_scalar_column(
        column,
        plan.column(0),
        0,
        p_value,
        coded
    );
    uint8_t exact[32] = {};
    uint64_t typed[4] = {0, 0, 0, 0};
    const bool typed_encoded
        = netw::store::typed_from_variant(coder, p_value, exact)
        && netw::store::code_value(coder, exact, typed);
    if (encoded != typed_encoded) {
        return false;
    }
    if (!encoded) {
        return true;
    }
    for (int element = 0; element < coder.slot.stride; ++element) {
        if (coded.read(coder.slot, element) != typed[element]) {
            return false;
        }
    }
    PackedInt64Array scratch;
    Variant decoded;
    if (!netw::wire::decode_scalar_column(
            column,
            plan.column(0),
            0,
            coded,
            scratch,
            decoded
        )) {
        return false;
    }
    uint8_t through_variant[32] = {};
    uint8_t canonical[32] = {};
    if (!netw::store::typed_from_variant(coder, decoded, through_variant)) {
        return false;
    }
    netw::store::canonical_value(coder, typed, canonical);
    const uint32_t size = netw::store::value_bytes_of(p_spec.type);
    return memcmp(through_variant, canonical, size) == 0;
}

TEST_CASE(
    "[Networked][Store][Hosted] SC1 a typed column codes and decodes every "
    "fixed-width value exactly as the column codec does, raw and through the "
    "stock quantizers"
) {
    const Spec specs[] = {
        {SchemaCore::F32, RAW},        {SchemaCore::F64, RAW},
        {SchemaCore::I8, RAW},         {SchemaCore::U8, RAW},
        {SchemaCore::I16, RAW},        {SchemaCore::U16, RAW},
        {SchemaCore::I32, RAW},        {SchemaCore::I64, RAW},
        {SchemaCore::BOOL, RAW},       {SchemaCore::ENTITY, RAW},
        {SchemaCore::VECTOR2, RAW},    {SchemaCore::VECTOR3, RAW},
        {SchemaCore::VECTOR4, RAW},    {SchemaCore::COLOR, RAW},
        {SchemaCore::QUATERNION, RAW}, {SchemaCore::F64, SCALAR},
        {SchemaCore::F32, SCALAR},     {SchemaCore::VECTOR2, SCALAR},
        {SchemaCore::VECTOR3, SCALAR}, {SchemaCore::QUATERNION, TURN},
    };
    int checked = 0;
    int differed = 0;
    for (const Spec &spec : specs) {
        const Array values = samples_of(spec.type);
        for (int64_t at = 0; at < values.size(); ++at) {
            differed += agrees(spec, values[at]) ? 0 : 1;
            checked += 1;
        }
    }
    NETW_CHECK_EQ(checked, 73);
    NETW_CHECK_EQ(differed, 0);
}

#if defined(NETW_TIER_HOSTED)

using netw::store::Family;
using netw::store::RowAddress;
using netw::store::Source;
using netw::store::Table;
using netw::store::World;

bool holds_codes_of(const Table &p_table, uint32_t p_row, const Vector3 &p_at) {
    CodeRow expected = CodeRow::for_plan(p_table.plan);
    if (!netw::wire::encode_scalar_column(
            *p_table.schema.at(0),
            p_table.plan.column(0),
            0,
            p_at,
            expected
        )) {
        return false;
    }
    const uint64_t *held = p_table.codes_of(p_row);
    for (uint32_t word = 0; word < expected.word_count(); ++word) {
        if (held[word] != expected.word_data()[word]) {
            return false;
        }
    }
    return true;
}

TEST_CASE(
    "[Networked][Store] SC2 a value moving inside one bin is read and not "
    "selected, a value leaving it is selected with the codes the column "
    "codec gives, and the pull never runs the column codec"
) {
    Node3D *src = memnew(Node3D);
    SchemaRecord record;
    SchemaColumn where;
    where.key = StringName("position");
    where.type = SchemaCore::VECTOR3;
    Ref<netw::NetwQuantizeScalar> grid;
    grid.instantiate();
    grid->set_bit_count(12);
    grid->set_min_limit(-8.0);
    grid->set_max_limit(8.0);
    where.quantizer = grid;
    record.columns.push_back(where);
    SchemaCore::fix(&record);

    World world;
    const uint32_t table = world.table_for(1, Family::VOLATILE, record);
    Table &held = world.table(table);
    REQUIRE(held.typed);
    held.resolve_ports(src);
    const RowAddress at
        = world.add_row(table, 10, 0, uint64_t(src->get_instance_id()), 0);
    world.place(at, 5, 1);
    held.sources[at.row] = uint8_t(Source::LOCAL);
    const World::PullWork opened = world.work;

    const Vector3 first(1.001, -2.002, 0.5003);
    src->set_position(first);
    world.pull(table, 1, false);
    CHECK(bool((held.flags[at.row] & Table::DIRTY) != 0));
    CHECK(holds_codes_of(held, at.row, first));

    const Vector3 nudged = first + Vector3(0.0002, -0.0002, 0.0002);
    src->set_position(nudged);
    world.pull(table, 2, false);
    CHECK(bool((held.flags[at.row] & Table::DIRTY) == 0));
    NETW_CHECK_EQ(held.changed_ticks[at.row], int64_t(1));
    CHECK(holds_codes_of(held, at.row, first));

    const Vector3 moved = first + Vector3(0.01, 0.0, -0.01);
    src->set_position(moved);
    world.pull(table, 3, false);
    CHECK(bool((held.flags[at.row] & Table::DIRTY) != 0));
    NETW_CHECK_EQ(held.changed_ticks[at.row], int64_t(3));
    CHECK(holds_codes_of(held, at.row, moved));

    NETW_CHECK_EQ(world.work.rows - opened.rows, int64_t(3));
    NETW_CHECK_EQ(world.work.changed - opened.changed, int64_t(2));
    NETW_CHECK_EQ(world.work.coded - opened.coded, int64_t(0));
    memdelete(src);
}

#endif

} // namespace TestNetwStoreCoder

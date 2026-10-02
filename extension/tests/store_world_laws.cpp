#include "support/netw_test.h"

#include <cstdint>

#include "godot/physics_body.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace TestNetwStoreWorld {

using netw::SchemaCore;
using netw::store::EntityRef;
using netw::store::Family;
using netw::store::RowAddress;
using netw::store::Source;
using netw::store::Table;
using netw::store::World;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;

SchemaRecord sealed_schema() {
    SchemaRecord record;
    SchemaColumn where;
    where.key = StringName("where");
    where.type = SchemaCore::VECTOR3;
    Ref<netw::NetwQuantizeScalar> grid;
    grid.instantiate();
    grid->set_bit_count(12);
    grid->set_min_limit(-8.0);
    grid->set_max_limit(8.0);
    where.quantizer = grid;
    record.columns.push_back(where);
    SchemaColumn awake;
    awake.key = StringName("awake");
    awake.type = SchemaCore::BOOL;
    record.columns.push_back(awake);
    SchemaColumn label;
    label.key = StringName("label");
    label.type = SchemaCore::STRING;
    record.columns.push_back(label);
    SchemaCore::fix(&record);
    return record;
}

RowAddress placed(
    World &p_world,
    uint32_t p_table,
    uint64_t p_owner,
    int64_t p_route,
    int64_t p_epoch
) {
    const RowAddress at = p_world.add_row(p_table, p_owner, 0, p_owner, 0);
    p_world.place(at, p_route, p_epoch);
    return at;
}

EntityRef ref_at(const World &p_world, const RowAddress &p_at) {
    return p_world.ref_of(p_world.table(p_at.table).slots[p_at.row]);
}

void fill(Table &p_table, uint32_t p_row, int64_t p_seed, int64_t p_tick) {
    const Vector3 where(double(p_seed), -double(p_seed), 0.5);
    const bool awake = (p_seed & 1) != 0;
    const String label = String("row") + String::num_int64(p_seed);
    p_table.write_value(p_row, 0, where);
    p_table.write_value(p_row, 1, awake);
    p_table.scratch.clear();
    p_table.encode(0, where, p_table.scratch);
    p_table.encode(1, awake, p_table.scratch);
    p_table.encode(2, label, p_table.scratch);
    p_table.settle(p_row, p_table.scratch, p_tick);
    p_table.sources[p_row] = uint8_t(Source::LOCAL);
    p_table.comps[p_row] = uint8_t(p_seed);
}

TEST_CASE(
    "[Networked][Store][Hosted] W1 a slot is reused only after its rows are "
    "gone, and a reference to its old life fails once its generation moves"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, sealed_schema());
    const RowAddress first = placed(world, table, 10, 5, 1);
    const EntityRef held = ref_at(world, first);
    REQUIRE(world.holds(held));

    const RowAddress second = placed(world, table, 11, 6, 1);
    CHECK(bool(ref_at(world, second).slot != held.slot));
    CHECK(world.holds(held));

    world.remove_row(world.row_of(10, 0));
    CHECK_FALSE(world.holds(held));
    NETW_CHECK_EQ(world.live_slot_count(), 1u);

    const RowAddress third = placed(world, table, 12, 7, 1);
    const EntityRef reused = ref_at(world, third);
    NETW_CHECK_EQ(reused.slot, held.slot);
    NETW_CHECK_EQ(reused.gen, held.gen + 1);
    CHECK(world.holds(reused));
    CHECK_FALSE(world.holds(held));
}

TEST_CASE(
    "[Networked][Store][Hosted] W2 a revived route takes a new slot while "
    "its previous life still holds a row, and the route resolves to the new "
    "life from then on"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, sealed_schema());
    const RowAddress old_life = placed(world, table, 10, 5, 1);
    const EntityRef old_ref = ref_at(world, old_life);

    const RowAddress new_life = placed(world, table, 11, 5, 2);
    const EntityRef new_ref = ref_at(world, new_life);
    CHECK(bool(new_ref.slot != old_ref.slot));
    CHECK(world.holds(old_ref));
    CHECK(world.holds(new_ref));
    NETW_CHECK_EQ(world.find(5, 2).slot, new_ref.slot);
    NETW_CHECK_EQ(world.epoch_of(old_ref.slot), int64_t(1));
    NETW_CHECK_EQ(world.epoch_of(new_ref.slot), int64_t(2));

    world.remove_row(world.row_of(10, 0));
    CHECK_FALSE(world.holds(old_ref));
    CHECK(world.holds(new_ref));
    NETW_CHECK_EQ(world.find(5, 2).slot, new_ref.slot);
}

TEST_CASE(
    "[Networked][Store][Hosted] W3 a swap-remove moves every per-row array "
    "of the last row into the hole, and the owner's address follows it"
) {
    World world;
    const uint32_t table
        = world.table_for(1, Family::VOLATILE, sealed_schema());
    Table &held = world.table(table);
    for (int64_t seed = 1; seed <= 3; ++seed) {
        const RowAddress at
            = placed(world, table, uint64_t(seed), seed * 10, 1);
        fill(held, at.row, seed, seed * 100);
    }
    const uint32_t last_slot = held.slots[2];
    uint64_t last_codes[8] = {};
    for (uint32_t word = 0; word < held.words && word < 8; ++word) {
        last_codes[word] = held.codes_of(2)[word];
    }

    world.remove_row(world.row_of(1, 0));

    NETW_CHECK_EQ(held.row_count(), 2u);
    NETW_CHECK_EQ(held.owners[0], uint64_t(3));
    NETW_CHECK_EQ(held.nodes[0], uint64_t(3));
    NETW_CHECK_EQ(held.routes[0], int64_t(30));
    NETW_CHECK_EQ(held.slots[0], last_slot);
    NETW_CHECK_EQ(int(held.comps[0]), 3);
    NETW_CHECK_EQ(held.changed_ticks[0], int64_t(300));
    NETW_CHECK_EQ(int(held.sources[0]), int(Source::LOCAL));
    CHECK(bool((held.flags[0] & Table::DIRTY) != 0));
    CHECK(bool(Vector3(held.read_value(0, 0)) == Vector3(3.0, -3.0, 0.5)));
    CHECK(bool(held.read_value(0, 1)));
    CHECK(bool(String(held.read_value(0, 2)) == String("row3")));
    for (uint32_t word = 0; word < held.words && word < 8; ++word) {
        NETW_CHECK_EQ(held.codes_of(0)[word], last_codes[word]);
    }
    const RowAddress followed = world.row_of(3, 0);
    NETW_CHECK_EQ(followed.table, table);
    NETW_CHECK_EQ(followed.row, 0u);
    NETW_CHECK_EQ(world.row_of(2, 0).row, 1u);
}

TEST_CASE(
    "[Networked][Store][Hosted] W5 a row whose node lacks a declared property "
    "is never read and stays ungathered, so the send walk offers nothing"
) {
    Node2D *src = memnew(Node2D);
    SchemaRecord record;
    SchemaColumn missing;
    missing.key = StringName("nonexistent");
    missing.type = SchemaCore::VARIANT;
    record.columns.push_back(missing);
    SchemaCore::fix(&record);

    World world;
    const uint32_t table = world.table_for(1, Family::VOLATILE, record);
    Table &held = world.table(table);
    held.resolve_ports(src);
    const RowAddress at
        = world.add_row(table, 10, 0, uint64_t(src->get_instance_id()), 0);
    world.place(at, 5, 1);
    held.sources[at.row] = uint8_t(Source::LOCAL);
    world.pull(table, 1, false);

    CHECK(bool((held.flags[at.row] & Table::UNGATHERED) != 0));
    CHECK(bool((held.flags[at.row] & Table::PULLED) == 0));
    memdelete(src);
}

Vector3 decoded_position(const Table &p_table, uint32_t p_row) {
    CodeRow codes = CodeRow::for_plan(p_table.plan);
    codes.assign(
        p_table.plan,
        p_table.codes_of(p_row),
        p_table.carried_of(p_row)
    );
    PackedInt64Array scratch;
    Variant value;
    netw::wire::decode_scalar_column(
        *p_table.schema.at(0),
        p_table.plan.column(0),
        0,
        codes,
        scratch,
        value
    );
    return value;
}

void check_close(const Vector3 &p_a, const Vector3 &p_b, double p_eps) {
    NETW_CHECK_CLOSE(p_a.x, p_b.x, p_eps);
    NETW_CHECK_CLOSE(p_a.y, p_b.y, p_eps);
    NETW_CHECK_CLOSE(p_a.z, p_b.z, p_eps);
}

TEST_CASE(
    "[Networked][Store][Hosted] W6 a pulled row keeps its exact value live "
    "and records the canonical row, so a move inside one bin appends nothing "
    "and the history decodes equal to the row's codes"
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
    REQUIRE(held.scalar);
    held.resolve_ports(src);
    const RowAddress at
        = world.add_row(table, 10, 0, uint64_t(src->get_instance_id()), 0);
    world.place(at, 5, 1);
    held.sources[at.row] = uint8_t(Source::LOCAL);

    const Vector3 first(1.001, -2.002, 0.5003);
    src->set_position(first);
    world.pull(table, 1, false);
    NETW_CHECK_EQ(held.history_count(at.row), 1u);
    const Vector3 canonical = decoded_position(held, at.row);
    CHECK(first.distance_to(canonical) > 1e-4);
    check_close(Vector3(held.read_value(at.row, 0)), first, 1e-12);
    check_close(Vector3(held.history_value(at.row, 0, 0)), canonical, 1e-12);

    const Vector3 nudged = canonical + Vector3(0.0003, -0.0003, 0.0003);
    src->set_position(nudged);
    world.pull(table, 2, false);
    NETW_CHECK_EQ(held.history_count(at.row), 1u);
    NETW_CHECK_EQ(held.history_tick(at.row, 0), int64_t(1));
    check_close(Vector3(held.read_value(at.row, 0)), nudged, 1e-12);
    check_close(decoded_position(held, at.row), canonical, 1e-12);
    check_close(Vector3(held.history_value(at.row, 0, 0)), canonical, 1e-12);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Store][Hosted][SceneTree] W7 a body is probed exactly "
    "while a sourced row reads it, and each edge into or out of that set "
    "reports the body once so its sleep is followed again"
) {
    RigidBody3D *body = memnew(RigidBody3D);
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
    world.table(table).resolve_ports(body);
    REQUIRE(world.table(table).body_columns == 1);
    REQUIRE(world.table(table).scalar);
    const uint64_t node = uint64_t(body->get_instance_id());
    const RowAddress at = world.add_row(table, 10, 0, node, 0);
    world.place(at, 5, 1);

    CHECK_FALSE(world.probes(node));
    NETW_CHECK_EQ(world.reprobed.size(), 0);

    const auto reported_once = [&world, node]() {
        const bool once
            = world.reprobed.size() == 1 && world.reprobed[0] == node;
        world.reprobed.clear();
        return once;
    };

    world.set_source(at, Source::LOCAL);
    CHECK(world.probes(node));
    CHECK(reported_once());

    world.set_source(at, Source::REMOTE);
    CHECK(world.probes(node));
    NETW_CHECK_EQ(world.reprobed.size(), 0);

    world.set_source(at, Source::NONE);
    CHECK_FALSE(world.probes(node));
    CHECK(reported_once());

    world.set_source(at, Source::LOCAL);
    world.reprobed.clear();
    world.remove_row(at);
    CHECK_FALSE(world.probes(node));
    CHECK(reported_once());
    memdelete(body);
}

} // namespace TestNetwStoreWorld

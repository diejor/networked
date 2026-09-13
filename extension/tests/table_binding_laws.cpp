#include "support/netw_test.h"

#include "support/netw_recorder.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/schema_core.hpp"
#include "netw/table/core.hpp"

namespace TestTableBinding {

using namespace godot;
using netw::NetwMultiplayer;
using netw::SchemaCore;
using netw_test::Recorder;

Ref<NetwMultiplayer> session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

TEST_CASE(
    "[Networked][Table][Hosted] TB1 a zero-column schema is a tag: it seals "
    "with a wire hash of its own and its table carries membership alone"
) {
    const Ref<NetwMultiplayer> core = session();
    const RID schema = core->schema_create("Tag");
    NETW_CHECK_EQ(core->schema_seal(schema), OK);
    NETW_CHECK_EQ(core->schema_get_column_count(schema), 0);
    NETW_CHECK_ORDER(core->schema_get_hash(schema), 0, !=);

    const RID table = core->table_create(schema);
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(
        core->table_get_wire_hash(table),
        core->schema_get_hash(schema)
    );

    PackedInt64Array routes;
    routes.push_back(11);
    routes.push_back(12);
    NETW_CHECK_EQ(core->table_write_routes(table, routes), OK);
    NETW_CHECK_EQ(core->table_commit(table), OK);
    NETW_CHECK_EQ(core->table_read_routes(table).size(), 2);
}

TEST_CASE(
    "[Networked][Table][Hosted] TB2 a schema holding the self-describing "
    "tier cannot become a table, because variable width has no row budget"
) {
    const Ref<NetwMultiplayer> core = session();
    const RID schema = core->schema_create("Loose");
    core->schema_add_column(schema, "blob", NetwMultiplayer::COLUMN_VARIANT, 1);
    NETW_CHECK_EQ(core->schema_seal(schema), OK);

    NETW_CHECK_EQ(int(core->table_create(schema).is_valid()), 0);
    NETW_CHECK_EQ(int(core->table_find("Loose").is_valid()), 0);
}

TEST_CASE(
    "[Networked][Table][Hosted] TB3 a table param is local configuration "
    "rather than shape, so setting one never moves the wire hash"
) {
    const Ref<NetwMultiplayer> core = session();
    const RID schema = core->schema_create("Params");
    core->schema_add_column(schema, "hp", NetwMultiplayer::COLUMN_U16, 1);
    core->schema_seal(schema);
    const RID table = core->table_create(schema);
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    const int before = core->table_get_wire_hash(table);

    core->table_set_param(table, NetwMultiplayer::TABLE_PARAM_RELIABLE, true);

    CHECK(core->get_table_core()->is_reliable(table));
    NETW_CHECK_EQ(core->table_get_wire_hash(table), before);
    NETW_CHECK_EQ(core->schema_get_hash(schema), before);
}

TEST_CASE(
    "[Networked][Table][Hosted] TB4 every table verb answers a handle that "
    "names no table rather than reaching into a null record"
) {
    const Ref<NetwMultiplayer> core = session();
    const RID absent;

    NETW_CHECK_EQ(core->table_get_wire_hash(absent), 0);
    NETW_CHECK_EQ(core->table_get_row(absent, 7), -1);
    NETW_CHECK_EQ(core->table_get_tick(absent), -1);
    NETW_CHECK_EQ(core->table_read_routes(absent).size(), 0);
    NETW_CHECK_EQ(core->table_read_births(absent).size(), 0);
    NETW_CHECK_EQ(core->table_read_deaths(absent).size(), 0);
    CHECK(core->table_read_column(absent, 0).get_type() == Variant::NIL);
    NETW_CHECK_EQ(
        core->table_write_routes(absent, PackedInt64Array()),
        ERR_DOES_NOT_EXIST
    );
    NETW_CHECK_EQ(
        core->table_write_column(absent, 0, PackedInt32Array()),
        ERR_DOES_NOT_EXIST
    );
    NETW_CHECK_EQ(core->table_commit(absent), ERR_DOES_NOT_EXIST);
}

TEST_CASE(
    "[Networked][Table][Hosted] TB6 a wave of claimed routes is one "
    "consecutive range, so a table's rows address in the order they came"
) {
    const Ref<NetwMultiplayer> core = session();
    const PackedInt64Array routes = core->liveness_claim_routes(3);

    NETW_CHECK_EQ(routes.size(), 3);
    if (routes.size() != 3) {
        return;
    }
    NETW_CHECK_ORDER(routes[0], 0, >);
    NETW_CHECK_EQ(routes[1], routes[0] + 1);
    NETW_CHECK_EQ(routes[2], routes[1] + 1);

    const PackedInt64Array later = core->liveness_claim_routes(1);
    NETW_CHECK_EQ(later.size(), 1);
    if (later.size() != 1) {
        return;
    }
    NETW_CHECK_ORDER(later[0], routes[2], >);
}

TEST_CASE(
    "[Networked][Table][Hosted] TB7 a wave of claimed rows costs no per-row "
    "signal and carries no node, which is what cohorts exist to buy"
) {
    const Ref<NetwMultiplayer> core = session();
    Vector<StringName> watched;
    watched.push_back(StringName("entity_live"));
    const Recorder recorder(core.ptr(), watched);

    const PackedInt64Array routes = core->liveness_claim_routes(50);

    NETW_CHECK_EQ(routes.size(), 50);
    NETW_CHECK_EQ(recorder.count(StringName("entity_live")), 0);
    if (routes.size() != 50) {
        return;
    }
    const RID entity = core->entity_from_route(routes[0]);
    NETW_CHECK_EQ(int(entity.is_valid()), 1);
    NETW_CHECK_EQ(
        core->entity_get_state(entity),
        int(NetwMultiplayer::ENTITY_STATE_LIVE)
    );
    CHECK(core->entity_get_node(entity) == nullptr);
    NETW_CHECK_EQ(core->entity_get_route(entity), routes[0]);
}

TEST_CASE(
    "[Networked][Table][Hosted] TB8 session teardown drops the rows and the "
    "pending removals and keeps the declaration, so re-entry finds its tables"
) {
    const Ref<NetwMultiplayer> core = session();
    const RID schema = core->schema_create("Torn");
    core->schema_add_column(schema, "hp", NetwMultiplayer::COLUMN_U16, 1);
    core->schema_seal(schema);
    const RID table = core->table_create(schema);
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    const PackedInt64Array routes = core->liveness_claim_routes(2);
    PackedInt32Array hp;
    hp.push_back(3);
    hp.push_back(4);
    core->table_write_routes(table, routes);
    core->table_write_column(table, 0, hp);
    NETW_CHECK_EQ(core->table_commit(table), OK);
    core->liveness_release_routes(routes);
    NETW_CHECK_ORDER(core->get_table_core()->lifecycle_removals().size(), 0, >);

    core->clear_flat_family_state();

    NETW_CHECK_EQ(core->get_table_core()->lifecycle_removals().size(), 0);
    NETW_CHECK_EQ(core->table_read_routes(table).size(), 0);
    CHECK(bool(core->table_find("Torn") == table));
    NETW_CHECK_EQ(core->schema_get_column_count(schema), 1);
}

} // namespace TestTableBinding

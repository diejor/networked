#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/persist/snapshot.hpp"
#include "netw/schema_core.hpp"
#include "netw/table/core.hpp"

namespace TestTableSnapshot {

using namespace godot;
using netw::NetwMultiplayer;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::table::Core;

struct World {
    Ref<NetwMultiplayer> session;
    RID schema;
    RID table;

    World(int p_version = 1) {
        session.instantiate();
        schema = session->schema_create("mobs");
        session->schema_add_column(
            schema,
            "where",
            NetwMultiplayer::COLUMN_VECTOR2,
            1
        );
        session->schema_add_column(schema, "hp", NetwMultiplayer::COLUMN_I64, 1);
        session->get_schema_core()->set_storage_version(schema, p_version);
        session->schema_seal(schema);
        table = session->table_create(schema);
    }

    Core *core() const {
        return session->get_table_core().ptr();
    }

    const SchemaRecord *record() const {
        return session->get_schema_core()->record_of(schema);
    }
};

PackedInt64Array routes_of(int64_t p_first, int p_count) {
    PackedInt64Array out;
    for (int at = 0; at < p_count; ++at) {
        out.push_back(p_first + at);
    }
    return out;
}

LocalVector<Variant> two_columns(int p_rows) {
    PackedVector2Array where;
    PackedInt64Array hp;
    for (int at = 0; at < p_rows; ++at) {
        where.push_back(Vector2(at, at * 2));
        hp.push_back(100 + at);
    }
    LocalVector<Variant> out;
    out.push_back(where);
    out.push_back(hp);
    return out;
}

TEST_CASE(
    "[Networked][Table][Hosted] TS1 a replacement publishes every row and "
    "column together"
) {
    World world;
    NETW_CHECK_EQ(
        world.core()->replace_rows(world.table, routes_of(10, 3), two_columns(3), 7),
        OK
    );
    NETW_CHECK_EQ(world.core()->read_routes(world.table).size(), 3);
    NETW_CHECK_EQ(world.core()->row_of(world.table, 11), 1);
    const PackedInt64Array hp = world.core()->read_column(world.table, 1);
    NETW_CHECK_EQ(int(hp[2]), 102);
}

TEST_CASE(
    "[Networked][Table][Hosted] TS2 a replacement whose LAST column is wrong "
    "changes no column and no route"
) {
    World world;
    world.core()->replace_rows(world.table, routes_of(10, 3), two_columns(3), 7);
    const PackedInt64Array before_routes = world.core()->read_routes(world.table);
    const PackedVector2Array before_where
        = world.core()->read_column(world.table, 0);

    LocalVector<Variant> bad = two_columns(2);
    bad[0] = PackedVector2Array(two_columns(2)[0]);
    LocalVector<Variant> mixed;
    mixed.push_back(two_columns(2)[0]);
    mixed.push_back(PackedInt64Array());
    NETW_CHECK_EQ(
        world.core()->replace_rows(world.table, routes_of(20, 2), mixed, 8),
        ERR_INVALID_DATA
    );

    NETW_CHECK_EQ(world.core()->read_routes(world.table).size(), before_routes.size());
    NETW_CHECK_EQ(int(world.core()->read_routes(world.table)[0]), 10);
    const PackedVector2Array after_where
        = world.core()->read_column(world.table, 0);
    CHECK(bool(after_where[0] == before_where[0]));
}

TEST_CASE(
    "[Networked][Table][Hosted] TS3 a replacement refuses a repeated or "
    "absent route before it touches anything"
) {
    World world;
    PackedInt64Array repeated;
    repeated.push_back(10);
    repeated.push_back(10);
    NETW_CHECK_EQ(
        world.core()->replace_rows(world.table, repeated, two_columns(2), 7),
        ERR_INVALID_DATA
    );

    PackedInt64Array absent;
    absent.push_back(0);
    absent.push_back(11);
    NETW_CHECK_EQ(
        world.core()->replace_rows(world.table, absent, two_columns(2), 7),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(world.core()->read_routes(world.table).size(), 0);
}

TEST_CASE(
    "[Networked][Table][Hosted] TS4 a snapshot roundtrips every committed "
    "value at full precision"
) {
    World world;
    const PackedStringArray ids = []() {
        PackedStringArray out;
        out.push_back("a");
        out.push_back("b");
        out.push_back("c");
        return out;
    }();
    const LocalVector<Variant> columns = two_columns(3);
    const Dictionary sealed = netw::persist::seal_snapshot(
        *world.record(),
        1,
        ids,
        columns
    );

    PackedStringArray read_ids;
    LocalVector<Variant> read_columns;
    String detail;
    NETW_CHECK_EQ(
        netw::persist::open_snapshot(
            *world.session->get_schema_core(),
            world.schema,
            sealed,
            read_ids,
            read_columns,
            detail
        ),
        OK
    );
    NETW_CHECK_EQ(read_ids.size(), 3);
    CHECK(bool(read_ids[2] == String("c")));
    NETW_CHECK_EQ(int(read_columns.size()), 2);
    const PackedVector2Array where = read_columns[0];
    CHECK(bool(where[1] == Vector2(1, 2)));
}

TEST_CASE(
    "[Networked][Table][Hosted] TS5 a snapshot whose ids disagree with its "
    "rows is refused"
) {
    World world;
    PackedStringArray short_ids;
    short_ids.push_back("a");
    const Dictionary sealed = netw::persist::seal_snapshot(
        *world.record(),
        1,
        short_ids,
        two_columns(3)
    );

    PackedStringArray read_ids;
    LocalVector<Variant> read_columns;
    String detail;
    NETW_CHECK_EQ(
        netw::persist::open_snapshot(
            *world.session->get_schema_core(),
            world.schema,
            sealed,
            read_ids,
            read_columns,
            detail
        ),
        ERR_INVALID_DATA
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] TS6 a duplicate or empty durable id is "
    "refused before a route is claimed"
) {
    PackedStringArray repeated;
    repeated.push_back("a");
    repeated.push_back("a");
    String detail;
    NETW_CHECK_EQ(
        netw::persist::validate_ids(repeated, 2, detail),
        ERR_INVALID_DATA
    );

    PackedStringArray blank;
    blank.push_back("a");
    blank.push_back("");
    NETW_CHECK_EQ(
        netw::persist::validate_ids(blank, 2, detail),
        ERR_INVALID_DATA
    );

    PackedStringArray good;
    good.push_back("a");
    good.push_back("b");
    NETW_CHECK_EQ(netw::persist::validate_ids(good, 2, detail), OK);
}

TEST_CASE(
    "[Networked][Table][Hosted] TS7 a snapshot saved under a newer storage "
    "version is refused and a missing migration step fails the load"
) {
    World ahead(3);
    PackedStringArray ids;
    ids.push_back("a");
    const Dictionary sealed = netw::persist::seal_snapshot(
        *ahead.record(),
        3,
        ids,
        two_columns(1)
    );

    World behind(1);
    PackedStringArray read_ids;
    LocalVector<Variant> read_columns;
    String detail;
    NETW_CHECK_EQ(
        netw::persist::open_snapshot(
            *behind.session->get_schema_core(),
            behind.schema,
            sealed,
            read_ids,
            read_columns,
            detail
        ),
        ERR_FILE_UNRECOGNIZED
    );

    const Dictionary older = netw::persist::seal_snapshot(
        *behind.record(),
        1,
        ids,
        two_columns(1)
    );
    World forward(2);
    NETW_CHECK_EQ(
        netw::persist::open_snapshot(
            *forward.session->get_schema_core(),
            forward.schema,
            older,
            read_ids,
            read_columns,
            detail
        ),
        ERR_UNCONFIGURED
    );
}

} // namespace TestTableSnapshot

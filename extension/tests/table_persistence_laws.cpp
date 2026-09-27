#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/persist/database.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"
#include "netw/session_core.hpp"
#include "netw/table/core.hpp"

namespace TestTablePersistence {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::persist::MemoryConnection;
namespace schema_model = netw::schema_model;

const int GAME_ROUTES = 50;

struct World {
    Ref<NetwMultiplayer> session;
    Ref<MemoryConnection> connection;
    RID database;
    RID schema;
    RID table;

    explicit World(const String &p_store) {
        schema_model::clear();
        netw::persist::forget_stores();
        session.instantiate();
        schema = session->schema_create("mobs");
        session->schema_add_column(
            schema,
            "where",
            NetwMultiplayer::COLUMN_VECTOR2,
            1
        );
        session
            ->schema_add_column(schema, "hp", NetwMultiplayer::COLUMN_I64, 1);
        session->schema_seal(schema);
        table = session->table_create(schema);
        session->liveness_claim_routes(GAME_ROUTES);

        database = session->get_databases()->create("saves");
        connection = MemoryConnection::opened(p_store, "slot1");
        session->get_databases()
            ->open(database, "slot1", NetwPromise::resolved(connection));
    }

    ~World() {
        schema_model::clear();
        netw::persist::forget_stores();
    }

    Error commit(RID p_table, int64_t p_first, int p_rows, int64_t p_hp) {
        PackedInt64Array routes;
        PackedVector2Array where;
        PackedInt64Array hp;
        for (int at = 0; at < p_rows; ++at) {
            routes.push_back(p_first + at);
            where.push_back(Vector2(at, at * 2));
            hp.push_back(p_hp + at);
        }
        session->table_write_routes(p_table, routes);
        session->table_write_column(p_table, 0, where);
        session->table_write_column(p_table, 1, hp);
        return session->table_commit(p_table);
    }

    Error save(const PackedStringArray &p_ids, const char *p_key = "forest") {
        const Ref<NetwPromise> saved
            = session->table_save(table, database, p_key, p_ids);
        if (!saved->get_is_settled()) {
            return ERR_BUSY;
        }
        return saved->get_is_failed() ? saved->get_code()
                                      : Error(int(saved->get_result()));
    }

    Dictionary load(const char *p_key = "forest") {
        const Ref<NetwPromise> loaded
            = session->table_load(table, database, p_key);
        return loaded->get_result();
    }

    netw::table::Core *core() const {
        return session->get_table_core().ptr();
    }
};

PackedStringArray ids_of(int p_count) {
    PackedStringArray out;
    for (int at = 0; at < p_count; ++at) {
        out.push_back(String("mob_") + String::num_int64(at));
    }
    return out;
}

TEST_CASE(
    "[Networked][Table][Hosted] TP1 a loaded snapshot maps every saved id "
    "onto a fresh route and carries its row's values there"
) {
    World world("tp1");
    NETW_CHECK_EQ(world.commit(world.table, 10, 3, 100), OK);
    NETW_CHECK_EQ(world.save(ids_of(3)), OK);

    const Dictionary loaded = world.load();
    NETW_CHECK_EQ(int(loaded["error"]), int(OK));
    CHECK(bool(loaded["found"]));
    const PackedStringArray loaded_ids = loaded["ids"];
    const PackedInt64Array loaded_routes = loaded["routes"];
    NETW_CHECK_EQ(loaded_ids.size(), 3);
    NETW_CHECK_EQ(loaded_routes.size(), 3);

    const PackedInt64Array routes = loaded_routes;
    const PackedInt64Array committed
        = world.session->table_read_routes(world.table);
    CHECK(bool(committed == routes));
    const PackedInt64Array hp
        = world.session->table_read_column(world.table, 1);
    const PackedStringArray ids = loaded_ids;
    for (int at = 0; at < routes.size(); ++at) {
        CHECK(bool(routes[at] > GAME_ROUTES));
        CHECK(bool(ids[at] == String("mob_") + String::num_int64(at)));
        NETW_CHECK_EQ(int(hp[at]), 100 + at);
    }
    CHECK(bool(routes[0] != routes[1]));
    CHECK(bool(routes[1] != routes[2]));
}

TEST_CASE(
    "[Networked][Table][Hosted] TP2 a commit made while a load is reading "
    "refuses the replacement, and the committed rows stand"
) {
    World world("tp2");
    world.commit(world.table, 10, 3, 100);
    NETW_CHECK_EQ(world.save(ids_of(3)), OK);

    world.connection->defer(true);
    const Ref<NetwPromise> loading
        = world.session->table_load(world.table, world.database, "forest");
    CHECK_FALSE(loading->get_is_settled());
    NETW_CHECK_EQ(world.commit(world.table, 20, 1, 7), OK);
    world.connection->release();

    REQUIRE(loading->get_is_settled());
    const Dictionary refused = loading->get_result();
    NETW_CHECK_EQ(int(refused["error"]), int(ERR_BUSY));
    CHECK_FALSE(bool(refused["found"]));
    const PackedInt64Array standing
        = world.session->table_read_routes(world.table);
    NETW_CHECK_EQ(standing.size(), 1);
    NETW_CHECK_EQ(int(standing[0]), 20);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP3 a key nobody saved answers found false at "
    "OK and leaves the table as it was"
) {
    World world("tp3");
    world.commit(world.table, 10, 2, 100);

    const Dictionary missed = world.load("nowhere");
    NETW_CHECK_EQ(int(missed["error"]), int(OK));
    CHECK_FALSE(bool(missed["found"]));
    NETW_CHECK_EQ(world.session->table_read_routes(world.table).size(), 2);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP4 a save refuses ids that are empty, "
    "repeated or miscounted before the backend is asked anything"
) {
    World world("tp4");
    world.commit(world.table, 10, 2, 100);
    world.connection->defer(true);

    PackedStringArray repeated;
    repeated.push_back("a");
    repeated.push_back("a");
    NETW_CHECK_EQ(int(world.save(repeated)), int(ERR_INVALID_DATA));

    PackedStringArray blank;
    blank.push_back("a");
    blank.push_back("");
    NETW_CHECK_EQ(int(world.save(blank)), int(ERR_INVALID_DATA));

    NETW_CHECK_EQ(int(world.save(ids_of(3))), int(ERR_INVALID_DATA));
    NETW_CHECK_EQ(world.connection->withheld_count(), 0);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP5 a snapshot of zero rows is stored, and "
    "loading it empties the table"
) {
    World world("tp5");
    NETW_CHECK_EQ(world.save(PackedStringArray()), OK);
    world.commit(world.table, 10, 2, 100);

    const Dictionary loaded = world.load();
    CHECK(bool(loaded["found"]));
    NETW_CHECK_EQ(world.session->table_read_routes(world.table).size(), 0);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP6 a load releases the routes an earlier load "
    "handed out, except one another table still holds, and never a route "
    "the game wrote"
) {
    World world("tp6");
    world.commit(world.table, 10, 2, 100);
    NETW_CHECK_EQ(world.save(ids_of(2)), OK);

    const Dictionary first = world.load();
    const PackedInt64Array earlier = first["routes"];
    REQUIRE(bool(earlier.size() == 2));

    const RID herd = world.session->schema_create("herd");
    world.session
        ->schema_add_column(herd, "where", NetwMultiplayer::COLUMN_VECTOR2, 1);
    world.session
        ->schema_add_column(herd, "hp", NetwMultiplayer::COLUMN_I64, 1);
    world.session->schema_seal(herd);
    const RID other = world.session->table_create(herd);
    REQUIRE(bool(other != world.table));
    NETW_CHECK_EQ(world.commit(other, earlier[0], 1, 5), OK);

    const Dictionary second = world.load();
    CHECK(bool(second["found"]));

    CHECK_FALSE(world.core()->is_tombstoned(10));
    CHECK_FALSE(world.core()->is_tombstoned(11));
    CHECK_FALSE(world.core()->is_tombstoned(earlier[0]));
    CHECK(world.core()->is_tombstoned(earlier[1]));
    NETW_CHECK_EQ(world.core()->row_of(other, earlier[0]), 0);
}

TEST_CASE(
    "[Networked][Table][Hosted] TP7 a peer holding no session authority "
    "neither saves nor loads a table"
) {
    World world("tp7");
    world.commit(world.table, 10, 2, 100);
    world.session->session_plane().set_role(netw::SessionCore::ROLE_CLIENT);

    NETW_CHECK_EQ(int(world.save(ids_of(2))), int(ERR_UNAUTHORIZED));
    const Dictionary refused = world.load();
    NETW_CHECK_EQ(int(refused["error"]), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(world.session->table_read_routes(world.table).size(), 2);
}

} // namespace TestTablePersistence

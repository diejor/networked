#include "support/netw_test.h"

#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/persist/database.hpp"
#include "netw/persist/envelope.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_core.hpp"

namespace TestDatabaseCore {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::SchemaCore;
using netw::persist::Databases;
using netw::persist::MemoryConnection;
using netw::persist::State;

struct World {
    Ref<NetwMultiplayer> session;
    Databases *plane = nullptr;
    RID database;
    RID schema;
    Ref<MemoryConnection> connection;

    World(const String &p_root, int p_version = 1) {
        session.instantiate();
        plane = session->get_databases();
        database = plane->create("saves");
        schema = session->schema_create("players");
        session->schema_add_column(
            schema,
            "gold",
            NetwMultiplayer::COLUMN_I64,
            1
        );
        session->schema_add_column(
            schema,
            "where",
            NetwMultiplayer::COLUMN_VECTOR2,
            1
        );
        session->get_schema_core()->set_storage_version(schema, p_version);
        session->schema_seal(schema);
        connection = MemoryConnection::opened(p_root, "slot1");
    }

    Error open() {
        const Ref<NetwPromise> opened = plane->open(
            database,
            "slot1",
            NetwPromise::resolved(connection)
        );
        return opened->get_is_failed() ? opened->get_code() : OK;
    }
};

Dictionary row(int64_t p_gold, const Vector2 &p_where) {
    Dictionary out;
    out["gold"] = p_gold;
    out["where"] = p_where;
    return out;
}

Dictionary read_of(const Ref<NetwPromise> &p_promise) {
    return p_promise->get_result();
}

Dictionary name_the_hero(const Dictionary &p_row) {
    Dictionary out = p_row.duplicate(true);
    out["title"] = String("nameless");
    return out;
}

TEST_CASE(
    "[Networked][Table][Hosted] DB1 a database admits no work until its open "
    "has settled"
) {
    World world("db1");
    NETW_CHECK_EQ(int(world.plane->state_of(world.database)), int(State::CLOSED));

    const Ref<NetwPromise> refused
        = world.plane->read(world.database, world.schema, "hero");
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(refused->get_code(), ERR_UNCONFIGURED);

    Ref<NetwPromise> connecting;
    connecting.instantiate();
    const Ref<NetwPromise> opening
        = world.plane->open(world.database, "slot1", connecting);
    NETW_CHECK_EQ(
        int(world.plane->state_of(world.database)),
        int(State::OPENING)
    );
    CHECK_FALSE(opening->get_is_settled());
    NETW_CHECK_EQ(
        world.plane->read(world.database, world.schema, "hero")->get_code(),
        ERR_UNCONFIGURED
    );

    connecting->resolve(world.connection);
    NETW_CHECK_EQ(int(world.plane->state_of(world.database)), int(State::OPEN));
    CHECK(opening->get_is_completed());
    CHECK(bool(world.plane->slot_of(world.database) == StringName("slot1")));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB2 a second open of the same slot shares one "
    "result and a different slot is refused"
) {
    World world("db2");
    Ref<NetwPromise> connecting;
    connecting.instantiate();
    const Ref<NetwPromise> first
        = world.plane->open(world.database, "slot1", connecting);
    const Ref<NetwPromise> again
        = world.plane->open(world.database, "slot1", connecting);
    CHECK(first == again);

    const Ref<NetwPromise> other
        = world.plane->open(world.database, "slot2", connecting);
    CHECK(other->get_is_failed());
    NETW_CHECK_EQ(other->get_code(), ERR_BUSY);

    connecting->resolve(world.connection);
    NETW_CHECK_EQ(
        world.plane->open(world.database, "slot1", connecting)->get_code(),
        OK
    );
    NETW_CHECK_EQ(
        world.plane->open(world.database, "slot2", connecting)->get_code(),
        ERR_BUSY
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] DB3 an open that fails leaves the database "
    "closed and retryable"
) {
    World world("db3");
    Ref<NetwPromise> connecting;
    connecting.instantiate();
    const Ref<NetwPromise> opening
        = world.plane->open(world.database, "slot1", connecting);
    connecting->reject(ERR_FILE_CANT_OPEN, "no such directory");

    CHECK(opening->get_is_failed());
    NETW_CHECK_EQ(opening->get_code(), ERR_FILE_CANT_OPEN);
    NETW_CHECK_EQ(
        int(world.plane->state_of(world.database)),
        int(State::CLOSED)
    );
    NETW_CHECK_EQ(world.open(), OK);
    NETW_CHECK_EQ(int(world.plane->state_of(world.database)), int(State::OPEN));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB4 a written record reads back, and a record "
    "never written is a miss rather than a failure"
) {
    World world("db4");
    NETW_CHECK_EQ(world.open(), OK);

    const Ref<NetwPromise> written = world.plane->write(
        world.database,
        world.schema,
        "hero",
        row(12, Vector2(3, 4))
    );
    NETW_CHECK_EQ(int(written->get_result()), int(OK));

    const Dictionary found = read_of(
        world.plane->read(world.database, world.schema, "hero")
    );
    NETW_CHECK_EQ(int(found["error"]), OK);
    CHECK(bool(found["found"]));
    const Dictionary found_values = found["values"];
    NETW_CHECK_EQ(int64_t(found_values["gold"]), int64_t(12));
    CHECK(bool(Vector2(found_values["where"]) == Vector2(3, 4)));

    const Dictionary absent = read_of(
        world.plane->read(world.database, world.schema, "nobody")
    );
    NETW_CHECK_EQ(int(absent["error"]), OK);
    CHECK_FALSE(bool(absent["found"]));
    CHECK(Dictionary(absent["values"]).is_empty());
}

TEST_CASE(
    "[Networked][Table][Hosted] DB5 an unreadable backend is a failure and "
    "never an absence"
) {
    World world("db5");
    NETW_CHECK_EQ(world.open(), OK);
    world.connection->fail_next(ERR_FILE_CANT_READ);

    const Dictionary answered = read_of(
        world.plane->read(world.database, world.schema, "hero")
    );
    NETW_CHECK_EQ(int(answered["error"]), ERR_FILE_CANT_READ);
    CHECK_FALSE(bool(answered["found"]));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB6 a write refuses an incomplete or "
    "mistyped row before the backend sees it"
) {
    World world("db6");
    NETW_CHECK_EQ(world.open(), OK);

    Dictionary missing;
    missing["gold"] = 1;
    const Ref<NetwPromise> short_row = world.plane->write(
        world.database,
        world.schema,
        "hero",
        missing
    );
    CHECK(short_row->get_is_failed());

    Dictionary mistyped = row(1, Vector2());
    mistyped["where"] = 7;
    CHECK(world.plane
              ->write(world.database, world.schema, "hero", mistyped)
              ->get_is_failed());

    Dictionary extra = row(1, Vector2());
    extra["unknown"] = 1;
    CHECK(world.plane->write(world.database, world.schema, "hero", extra)
              ->get_is_failed());

    CHECK_FALSE(
        bool(read_of(world.plane->read(world.database, world.schema, "hero"))
                 ["found"])
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] DB7 a patch keeps the fields it does not "
    "name and refuses a record that is not there"
) {
    World world("db7");
    NETW_CHECK_EQ(world.open(), OK);
    world.plane->write(world.database, world.schema, "hero", row(12, Vector2(3, 4)));

    Dictionary just_gold;
    just_gold["gold"] = 99;
    const Variant patched
        = world.plane->patch(world.database, world.schema, "hero", just_gold)
              ->get_result();
    NETW_CHECK_EQ(int(patched.get_type()), int(Variant::INT));
    NETW_CHECK_EQ(int(patched), int(OK));
    const Dictionary after = read_of(
        world.plane->read(world.database, world.schema, "hero")
    );
    const Dictionary after_values = after["values"];
    NETW_CHECK_EQ(int64_t(after_values["gold"]), int64_t(99));
    CHECK(bool(Vector2(after_values["where"]) == Vector2(3, 4)));

    const Variant missed
        = world.plane->patch(world.database, world.schema, "ghost", just_gold)
              ->get_result();
    NETW_CHECK_EQ(int(missed.get_type()), int(Variant::INT));
    NETW_CHECK_EQ(int(missed), int(ERR_DOES_NOT_EXIST));

    Dictionary unknown;
    unknown["nothing"] = 1;
    CHECK(world.plane
              ->patch(world.database, world.schema, "hero", unknown)
              ->get_is_failed());
}

TEST_CASE(
    "[Networked][Table][Hosted] DB7b a patch the backend refuses settles the "
    "refusal's Error the way a write does, whether its read or its write was "
    "refused"
) {
    World world("db7b");
    NETW_CHECK_EQ(world.open(), OK);
    world.plane->write(world.database, world.schema, "hero", row(12, Vector2()));
    Dictionary just_gold;
    just_gold["gold"] = 99;

    world.connection->fail_next(ERR_FILE_CANT_READ);
    const Ref<NetwPromise> unread
        = world.plane->patch(world.database, world.schema, "hero", just_gold);
    CHECK(unread->get_is_completed());
    NETW_CHECK_EQ(int(unread->get_result().get_type()), int(Variant::INT));
    NETW_CHECK_EQ(int(unread->get_result()), int(ERR_FILE_CANT_READ));

    world.connection->defer(true);
    const Ref<NetwPromise> unwritten
        = world.plane->patch(world.database, world.schema, "hero", just_gold);
    world.connection->defer(false);
    world.connection->fail_next(ERR_FILE_CANT_WRITE);
    world.connection->release();
    CHECK(unwritten->get_is_completed());
    NETW_CHECK_EQ(int(unwritten->get_result().get_type()), int(Variant::INT));
    NETW_CHECK_EQ(int(unwritten->get_result()), int(ERR_FILE_CANT_WRITE));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB8 an erase is idempotent and removes what a "
    "read would have found"
) {
    World world("db8");
    NETW_CHECK_EQ(world.open(), OK);
    world.plane->write(world.database, world.schema, "hero", row(12, Vector2()));

    NETW_CHECK_EQ(
        int(world.plane->erase(world.database, world.schema, "hero")
                ->get_result()),
        int(OK)
    );
    CHECK_FALSE(
        bool(read_of(world.plane->read(world.database, world.schema, "hero"))
                 ["found"])
    );
    NETW_CHECK_EQ(
        int(world.plane->erase(world.database, world.schema, "hero")
                ->get_result()),
        int(OK)
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] DB9 two operations on one address settle in "
    "the order they were admitted, however long the first takes"
) {
    World world("db9");
    NETW_CHECK_EQ(world.open(), OK);
    world.connection->defer(true);

    const Ref<NetwPromise> first = world.plane->write(
        world.database,
        world.schema,
        "hero",
        row(1, Vector2())
    );
    const Ref<NetwPromise> second = world.plane->write(
        world.database,
        world.schema,
        "hero",
        row(2, Vector2())
    );
    NETW_CHECK_EQ(world.connection->withheld_count(), 1);
    CHECK_FALSE(first->get_is_settled());
    CHECK_FALSE(second->get_is_settled());

    world.connection->release();
    CHECK(first->get_is_settled());
    CHECK_FALSE(second->get_is_settled());
    NETW_CHECK_EQ(world.connection->withheld_count(), 1);

    world.connection->release();
    CHECK(second->get_is_settled());

    world.connection->defer(false);
    const Dictionary hero_values = Dictionary(
        read_of(world.plane->read(world.database, world.schema, "hero"))
    )["values"];
    NETW_CHECK_EQ(int64_t(hero_values["gold"]), int64_t(2));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB10 two addresses do not wait for each other"
) {
    World world("db10");
    NETW_CHECK_EQ(world.open(), OK);
    world.connection->defer(true);

    world.plane->write(world.database, world.schema, "one", row(1, Vector2()));
    world.plane->write(world.database, world.schema, "two", row(2, Vector2()));
    NETW_CHECK_EQ(world.connection->withheld_count(), 2);
}

TEST_CASE(
    "[Networked][Table][Hosted] DB11 a value submitted is the value written, "
    "whatever the caller does to its Dictionary afterwards"
) {
    World world("db11");
    NETW_CHECK_EQ(world.open(), OK);
    world.connection->defer(true);

    world.plane->write(world.database, world.schema, "hero", row(0, Vector2()));

    Dictionary values = row(1, Vector2());
    const Ref<NetwPromise> queued
        = world.plane->write(world.database, world.schema, "hero", values);
    NETW_CHECK_EQ(world.connection->withheld_count(), 1);
    values["gold"] = 999;

    world.connection->release();
    world.connection->release();
    NETW_CHECK_EQ(int(queued->get_result()), int(OK));

    world.connection->defer(false);
    const Dictionary hero_values = Dictionary(
        read_of(world.plane->read(world.database, world.schema, "hero"))
    )["values"];
    NETW_CHECK_EQ(int64_t(hero_values["gold"]), int64_t(1));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB12 a batch answers one outcome per "
    "operation, in the order it was given them"
) {
    World world("db12");
    NETW_CHECK_EQ(world.open(), OK);
    const SchemaCore *core = world.session->get_schema_core();
    const netw::SchemaRecord *schema = core->record_of(world.schema);

    Array operations;
    Dictionary replace;
    replace["kind"] = "replace";
    replace["address"] = netw::persist::address_of(
        netw::persist::Kind::RECORD,
        schema->name,
        "one"
    );
    replace["envelope"]
        = netw::persist::seal_record(*schema, 1, row(5, Vector2()));
    operations.push_back(replace);

    Dictionary removal;
    removal["kind"] = "erase";
    removal["address"] = netw::persist::address_of(
        netw::persist::Kind::RECORD,
        schema->name,
        "two"
    );
    operations.push_back(removal);

    const Dictionary answered
        = world.plane->submit(world.database, operations)->get_result();
    NETW_CHECK_EQ(int(answered["error"]), OK);
    NETW_CHECK_EQ(PackedInt32Array(answered["errors"]).size(), 2);
    NETW_CHECK_EQ(PackedByteArray(answered["uncertain"]).size(), 2);
    const Dictionary one_values = Dictionary(
        read_of(world.plane->read(world.database, world.schema, "one"))
    )["values"];
    NETW_CHECK_EQ(int64_t(one_values["gold"]), int64_t(5));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB13 a scan reads pages and its cursor "
    "continues where the page stopped"
) {
    World world("db13");
    NETW_CHECK_EQ(world.open(), OK);
    world.plane->write(world.database, world.schema, "a", row(1, Vector2()));
    world.plane->write(world.database, world.schema, "b", row(2, Vector2()));
    world.plane->write(world.database, world.schema, "c", row(3, Vector2()));

    const Dictionary first
        = world.plane
              ->scan(world.database, world.schema, Dictionary(), "", 2)
              ->get_result();
    NETW_CHECK_EQ(int(first["error"]), OK);
    NETW_CHECK_EQ(Array(first["records"]).size(), 2);
    CHECK_FALSE(String(first["cursor"]).is_empty());

    const Dictionary next
        = world.plane
              ->scan(
                  world.database,
                  world.schema,
                  Dictionary(),
                  String(first["cursor"]),
                  2
              )
              ->get_result();
    NETW_CHECK_EQ(Array(next["records"]).size(), 1);
    CHECK(String(next["cursor"]).is_empty());
}

TEST_CASE(
    "[Networked][Table][Hosted] DB14 a close waits for the work already "
    "admitted and then advances the slot generation"
) {
    World world("db14");
    NETW_CHECK_EQ(world.open(), OK);
    const int64_t before = world.plane->generation_of(world.database);
    world.connection->defer(true);

    const Ref<NetwPromise> written
        = world.plane->write(world.database, world.schema, "hero", row(1, Vector2()));
    const Ref<NetwPromise> closing = world.plane->close(world.database);
    NETW_CHECK_EQ(
        int(world.plane->state_of(world.database)),
        int(State::CLOSING)
    );
    CHECK_FALSE(closing->get_is_settled());

    world.connection->release();
    CHECK(written->get_is_settled());
    CHECK(closing->get_is_completed());
    NETW_CHECK_EQ(
        int(world.plane->state_of(world.database)),
        int(State::CLOSED)
    );
    NETW_CHECK_ORDER(world.plane->generation_of(world.database), before, >);
    CHECK(bool(world.plane->slot_of(world.database) == StringName()));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB15 a flush settles once the work admitted "
    "before it has, and an idle database flushes at once"
) {
    World world("db15");
    NETW_CHECK_EQ(world.open(), OK);
    CHECK(world.plane->flush(world.database)->get_is_completed());

    world.connection->defer(true);
    world.plane->write(world.database, world.schema, "hero", row(1, Vector2()));
    const Ref<NetwPromise> flushing = world.plane->flush(world.database);
    CHECK_FALSE(flushing->get_is_settled());
    world.connection->release();
    CHECK(flushing->get_is_completed());
}

TEST_CASE(
    "[Networked][Table][Hosted] DB16 two databases of the same name in two "
    "sessions hold their own state"
) {
    World here("db16");
    World there("db16");
    NETW_CHECK_EQ(here.open(), OK);
    NETW_CHECK_EQ(there.open(), OK);
    CHECK(here.database != there.database);

    here.plane->write(here.database, here.schema, "hero", row(7, Vector2()));
    here.plane->close(here.database);
    NETW_CHECK_EQ(
        int(there.plane->state_of(there.database)),
        int(State::OPEN)
    );
    const Dictionary hero_values = Dictionary(
        read_of(there.plane->read(there.database, there.schema, "hero"))
    )["values"];
    NETW_CHECK_EQ(int64_t(hero_values["gold"]), int64_t(7));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB17 a record written under an older storage "
    "version is migrated on the way out, and a missing step fails the read"
) {
    {
        World old("db17");
        NETW_CHECK_EQ(old.open(), OK);
        old.plane
            ->write(old.database, old.schema, "hero", row(5, Vector2(1, 2)));
    }

    Ref<NetwMultiplayer> session;
    session.instantiate();
    Databases *plane = session->get_databases();
    const RID database = plane->create("saves");
    const RID schema = session->schema_create("players");
    session->schema_add_column(schema, "gold", NetwMultiplayer::COLUMN_I64, 1);
    session->schema_add_column(
        schema,
        "where",
        NetwMultiplayer::COLUMN_VECTOR2,
        1
    );
    session->get_schema_core()->set_storage_version(schema, 2);
    session->schema_seal(schema);

    const Ref<MemoryConnection> connection
        = MemoryConnection::opened("db17", "slot1");
    plane->open(database, "slot1", NetwPromise::resolved(connection));

    const Dictionary unstepped
        = plane->read(database, schema, "hero")->get_result();
    NETW_CHECK_EQ(int(unstepped["error"]), ERR_UNCONFIGURED);
    CHECK_FALSE(bool(unstepped["found"]));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB17b a declared migration step runs on the "
    "way out and the stored record keeps its old version"
) {
    {
        World old("db17b");
        NETW_CHECK_EQ(old.open(), OK);
        old.plane
            ->write(old.database, old.schema, "hero", row(5, Vector2(1, 2)));
    }

    Ref<NetwMultiplayer> session;
    session.instantiate();
    Databases *plane = session->get_databases();
    const RID database = plane->create("saves");
    const RID schema = session->schema_create("players");
    session->schema_add_column(schema, "gold", NetwMultiplayer::COLUMN_I64, 1);
    session->schema_add_column(
        schema,
        "where",
        NetwMultiplayer::COLUMN_VECTOR2,
        1
    );
    session->schema_add_column(schema, "title", NetwMultiplayer::COLUMN_STRING, 1);
    SchemaCore *core = session->get_schema_core();
    core->set_storage_version(schema, 2);
    core->add_migration(schema, 1, callable_mp_static(&name_the_hero));
    session->schema_seal(schema);

    const Ref<MemoryConnection> connection
        = MemoryConnection::opened("db17b", "slot1");
    plane->open(database, "slot1", NetwPromise::resolved(connection));

    const Dictionary stepped
        = plane->read(database, schema, "hero")->get_result();
    NETW_CHECK_EQ(int(stepped["error"]), OK);
    CHECK(bool(stepped["found"]));
    const Dictionary stepped_values = stepped["values"];
    CHECK(bool(String(stepped_values["title"]) == String("nameless")));
    NETW_CHECK_EQ(int64_t(stepped_values["gold"]), int64_t(5));

    const Dictionary again
        = plane->read(database, schema, "hero")->get_result();
    NETW_CHECK_EQ(int(again["error"]), OK);
    const Dictionary again_values = again["values"];
    CHECK(bool(String(again_values["title"]) == String("nameless")));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB18 a record saved under a newer storage "
    "version fails its read and is left untouched"
) {
    {
        World ahead("db18", 3);
        NETW_CHECK_EQ(ahead.open(), OK);
        ahead.plane
            ->write(ahead.database, ahead.schema, "hero", row(5, Vector2()));
    }
    World behind("db18", 1);
    NETW_CHECK_EQ(behind.open(), OK);
    const Dictionary answered = read_of(
        behind.plane->read(behind.database, behind.schema, "hero")
    );
    NETW_CHECK_EQ(int(answered["error"]), ERR_FILE_UNRECOGNIZED);
    CHECK_FALSE(bool(answered["found"]));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB19 a returned Dictionary is the caller's "
    "own copy and cannot reach stored state"
) {
    World world("db19");
    NETW_CHECK_EQ(world.open(), OK);
    world.plane->write(world.database, world.schema, "hero", row(1, Vector2()));

    Dictionary answered = read_of(
        world.plane->read(world.database, world.schema, "hero")
    );
    Dictionary values = answered["values"];
    values["gold"] = 404;

    const Dictionary hero_values = Dictionary(
        read_of(world.plane->read(world.database, world.schema, "hero"))
    )["values"];
    NETW_CHECK_EQ(int64_t(hero_values["gold"]), int64_t(1));
}

TEST_CASE(
    "[Networked][Table][Hosted] DB20 a single write whose outcome is "
    "uncertain never settles OK"
) {
    World world("db20");
    NETW_CHECK_EQ(world.open(), OK);
    world.plane->write(world.database, world.schema, "hero", row(1, Vector2()));
    Dictionary just_gold;
    just_gold["gold"] = 99;

    world.connection->doubt_next(OK);
    NETW_CHECK_EQ(
        int(world.plane
                ->write(world.database, world.schema, "hero", row(2, Vector2()))
                ->get_result()),
        int(ERR_UNAVAILABLE)
    );

    world.connection->doubt_next(OK);
    NETW_CHECK_EQ(
        int(world.plane
                ->patch(world.database, world.schema, "hero", just_gold)
                ->get_result()),
        int(ERR_UNAVAILABLE)
    );

    world.connection->doubt_next(OK);
    NETW_CHECK_EQ(
        int(world.plane->erase(world.database, world.schema, "hero")
                ->get_result()),
        int(ERR_UNAVAILABLE)
    );

    world.connection->doubt_next(ERR_TIMEOUT);
    NETW_CHECK_EQ(
        int(world.plane
                ->write(world.database, world.schema, "hero", row(3, Vector2()))
                ->get_result()),
        int(ERR_TIMEOUT)
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] DB21 a close that overtakes an open fails the "
    "open, and the connection that arrives late is closed and never adopted"
) {
    World world("db21");
    Ref<NetwPromise> connecting;
    connecting.instantiate();
    const Ref<NetwPromise> opening
        = world.plane->open(world.database, "slot1", connecting);

    const Ref<NetwPromise> closing = world.plane->close(world.database);
    CHECK(closing->get_is_completed());
    CHECK(opening->get_is_failed());
    NETW_CHECK_EQ(opening->get_code(), ERR_UNAVAILABLE);
    NETW_CHECK_EQ(
        int(world.plane->state_of(world.database)),
        int(State::CLOSED)
    );

    connecting->resolve(world.connection);
    NETW_CHECK_EQ(
        int(world.plane->state_of(world.database)),
        int(State::CLOSED)
    );
    CHECK(world.plane->connection_of(world.database).is_null());
    NETW_CHECK_EQ(world.connection->close_count(), 1);

    NETW_CHECK_EQ(world.open(), OK);
    NETW_CHECK_EQ(int(world.plane->state_of(world.database)), int(State::OPEN));
}

} // namespace TestDatabaseCore

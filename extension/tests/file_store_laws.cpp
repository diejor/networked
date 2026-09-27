#include "support/netw_test.h"

#include "godot/file_access.hpp"
#include "godot/file_system.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/database.hpp"
#include "netw/persist/envelope.hpp"
#include "netw/persist/file_store.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"

namespace TestFileStoreLaws {

using namespace godot;
using netw::FileSystemDatabase;
using netw::NetwDatabase;
using netw::NetwDatabaseConfig;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwSchema;
using netw::SchemaCore;
using netw::SchemaRecord;
using netw::persist::Databases;
using netw::persist::FileConnection;
using netw::persist::FileStore;
using netw::persist::Kind;
namespace schema_model = netw::schema_model;

String scratch_root(const String &p_name) {
    const String root = String("user://netw_file_store_laws/") + p_name;
    DirAccess::make_dir_recursive_absolute(netw::gd::globalized(root));
    return root;
}

struct World {
    String root;
    Ref<NetwMultiplayer> session;
    Databases *plane = nullptr;
    RID database;
    RID schema;
    Ref<FileConnection> connection;

    explicit World(const char *p_name) {
        root = scratch_root(p_name);
        session.instantiate();
        plane = session->get_databases();
        database = plane->create("saves");
        schema = session->schema_create("players");
        session
            ->schema_add_column(schema, "gold", NetwMultiplayer::COLUMN_I64, 1);
        session->schema_seal(schema);
        connection = FileConnection::opened(root, "slot1");
    }

    const SchemaRecord *record() const {
        return session->get_schema_core()->record_of(schema);
    }

    Error open() {
        const Ref<NetwPromise> opened
            = plane->open(database, "slot1", NetwPromise::resolved(connection));
        return opened->get_is_failed() ? opened->get_code() : OK;
    }
};

void scrub(const char *p_name) {
    const String root = scratch_root(p_name);
    FileStore(root).erase_slot("slot1");
    FileStore(root).erase_slot("slot2");
}

Dictionary row(int64_t p_gold) {
    Dictionary out;
    out["gold"] = p_gold;
    return out;
}

Dictionary sealed(const World &p_world, int64_t p_gold) {
    return netw::persist::seal_record(*p_world.record(), 1, row(p_gold));
}

Dictionary address(const World &p_world, Kind p_kind, const String &p_key) {
    return netw::persist::address_of(p_kind, p_world.record()->name, p_key);
}

Dictionary reply_of(const Ref<NetwPromise> &p_promise) {
    REQUIRE(p_promise.is_valid());
    REQUIRE(p_promise->get_is_completed());
    return Dictionary(p_promise->get_result());
}

void write_text(const String &p_path, const String &p_text) {
    DirAccess::make_dir_recursive_absolute(p_path.get_base_dir());
    Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
    REQUIRE(file.is_valid());
    file->store_string(p_text);
    file->close();
}

TEST_CASE(
    "[Networked][Table][Hosted] FS1 a record written by one connection is read "
    "back by a connection built over the same root after it is gone"
) {
    scrub("fs1");
    {
        World world("fs1");
        NETW_CHECK_EQ(world.open(), OK);
        world.plane->write(world.database, world.schema, "hero", row(7));
    }

    World again("fs1");
    NETW_CHECK_EQ(again.open(), OK);
    const Dictionary answered
        = again.plane->read(again.database, again.schema, "hero")->get_result();
    NETW_CHECK_EQ(int(answered["error"]), OK);
    CHECK(bool(answered["found"]));
    NETW_CHECK_EQ(int64_t(Dictionary(answered["values"])["gold"]), int64_t(7));
}

TEST_CASE(
    "[Networked][Table][Hosted] FS2 a replacement that fails before its rename "
    "leaves the record it was about to overwrite readable"
) {
    scrub("fs2");
    World world("fs2");
    FileStore store(world.root);
    const Dictionary at = address(world, Kind::RECORD, "hero");
    NETW_CHECK_EQ(store.replace("slot1", at, sealed(world, 3)), OK);

    const String staging = store.staging_path_of("slot1", at);
    NETW_CHECK_EQ(DirAccess::make_dir_recursive_absolute(staging), OK);

    const Error refused = store.replace("slot1", at, sealed(world, 99));
    NETW_CHECK_EQ(refused, ERR_FILE_CANT_WRITE);

    Dictionary held;
    bool found = false;
    NETW_CHECK_EQ(store.read("slot1", at, held, found), OK);
    CHECK(found);
    Dictionary values;
    String detail;
    NETW_CHECK_EQ(
        netw::persist::open_record(
            *world.session->get_schema_core(),
            world.schema,
            held,
            values,
            detail
        ),
        OK
    );
    NETW_CHECK_EQ(int64_t(values["gold"]), int64_t(3));

    DirAccess::remove_absolute(staging);
}

TEST_CASE(
    "[Networked][Table][Hosted] FS3 a key naming a parent directory, a "
    "separator or a null byte lands inside the root and nowhere else"
) {
    scrub("fs3");
    World world("fs3");
    FileStore store(world.root);

    PackedStringArray keys;
    keys.push_back("../../escaped");
    keys.push_back("..");
    keys.push_back("a/b\\c");
    keys.push_back(String("a") + String::chr(0) + String("b"));

    for (int at = 0; at < keys.size(); ++at) {
        const Dictionary here = address(world, Kind::RECORD, keys[at]);
        const String path = store.path_of("slot1", here);
        CHECK(path.begins_with(world.root + "/"));
        NETW_CHECK_EQ(path.simplify_path() == path, true);
        NETW_CHECK_EQ(path.find(".."), -1);
        NETW_CHECK_EQ(store.replace("slot1", here, sealed(world, at)), OK);
        CHECK(netw::gd::file_exists(path));

        Dictionary held;
        bool found = false;
        NETW_CHECK_EQ(store.read("slot1", here, held, found), OK);
        CHECK(found);
    }

    CHECK_FALSE(
        netw::gd::file_exists(
            String("user://netw_file_store_laws/escaped.netwrec")
        )
    );
    CHECK_FALSE(netw::gd::file_exists(String("user://escaped.netwrec")));
}

TEST_CASE(
    "[Networked][Table][Hosted] FS4 a record and a table snapshot spelled with "
    "the same key are two files, because kind is part of the address"
) {
    scrub("fs4");
    World world("fs4");
    FileStore store(world.root);
    const Dictionary record = address(world, Kind::RECORD, "hero");
    const Dictionary snapshot = address(world, Kind::SNAPSHOT, "hero");

    NETW_CHECK_EQ(store.replace("slot1", record, sealed(world, 1)), OK);
    NETW_CHECK_EQ(store.replace("slot1", snapshot, sealed(world, 2)), OK);
    CHECK(
        bool(store.path_of("slot1", record) != store.path_of("slot1", snapshot))
    );

    NETW_CHECK_EQ(store.erase("slot1", record), OK);
    Dictionary held;
    bool found = false;
    NETW_CHECK_EQ(store.read("slot1", record, held, found), OK);
    CHECK_FALSE(found);
    NETW_CHECK_EQ(store.read("slot1", snapshot, held, found), OK);
    CHECK(found);
}

TEST_CASE(
    "[Networked][Table][Hosted] FS5 erasing one slot leaves another slot's "
    "records whole and takes both of its own address kinds with it"
) {
    scrub("fs5");
    World world("fs5");
    FileStore store(world.root);
    const Dictionary record = address(world, Kind::RECORD, "hero");
    const Dictionary snapshot = address(world, Kind::SNAPSHOT, "hero");

    NETW_CHECK_EQ(store.replace("slot1", record, sealed(world, 1)), OK);
    NETW_CHECK_EQ(store.replace("slot1", snapshot, sealed(world, 2)), OK);
    NETW_CHECK_EQ(store.open_slot("slot2"), OK);
    NETW_CHECK_EQ(store.replace("slot2", record, sealed(world, 3)), OK);

    CHECK(store.erase_slot("slot1"));
    CHECK_FALSE(store.has_slot("slot1"));
    CHECK_FALSE(netw::gd::file_exists(store.path_of("slot1", record)));
    CHECK_FALSE(netw::gd::file_exists(store.path_of("slot1", snapshot)));

    Dictionary held;
    bool found = false;
    NETW_CHECK_EQ(store.read("slot2", record, held, found), OK);
    CHECK(found);
}

TEST_CASE(
    "[Networked][Table][Hosted] FS6 slot names are read from the directory, so "
    "a slot this store never opened is still listed"
) {
    scrub("fs6");
    World world("fs6");
    FileStore store(world.root);
    NETW_CHECK_EQ(store.open_slot("slot1"), OK);
    NETW_CHECK_EQ(store.open_slot("a slot/with punctuation"), OK);

    FileStore fresh(world.root);
    const PackedStringArray names = fresh.slot_names();
    NETW_CHECK_EQ(names.has("slot1"), true);
    NETW_CHECK_EQ(names.has("a slot/with punctuation"), true);

    CHECK(fresh.erase_slot("a slot/with punctuation"));
    NETW_CHECK_EQ(fresh.slot_names().has("a slot/with punctuation"), false);
}

TEST_CASE(
    "[Networked][Table][Hosted] FS7 a file this library did not write is found "
    "rather than absent, refused as unrecognized, and left on disk"
) {
    scrub("fs7");
    World world("fs7");
    FileStore store(world.root);
    const Dictionary at = address(world, Kind::RECORD, "hero");
    const String path = store.path_of("slot1", at);
    const String legacy("[gd_resource type=\"Resource\" format=3]\ngold = 4\n");
    write_text(path, legacy);

    Dictionary held;
    bool found = false;
    NETW_CHECK_EQ(store.read("slot1", at, held, found), OK);
    CHECK(found);

    Dictionary values;
    String detail;
    NETW_CHECK_EQ(
        netw::persist::open_record(
            *world.session->get_schema_core(),
            world.schema,
            held,
            values,
            detail
        ),
        ERR_FILE_UNRECOGNIZED
    );
    CHECK_FALSE(detail.is_empty());
    CHECK(netw::gd::file_exists(path));
    CHECK(bool(FileAccess::get_file_as_string(path) == legacy));
}

TEST_CASE(
    "[Networked][Table][Hosted] FS8 a batch is not atomic, so it reports one "
    "honest outcome per operation and keeps the work that succeeded"
) {
    scrub("fs8");
    World world("fs8");
    Array operations;

    Dictionary first;
    first["kind"] = "replace";
    first["address"] = address(world, Kind::RECORD, "one");
    first["envelope"] = sealed(world, 1);
    operations.push_back(first);

    Dictionary nonsense;
    nonsense["kind"] = "ponder";
    nonsense["address"] = address(world, Kind::RECORD, "two");
    operations.push_back(nonsense);

    Dictionary third;
    third["kind"] = "replace";
    third["address"] = address(world, Kind::RECORD, "three");
    third["envelope"] = sealed(world, 3);
    operations.push_back(third);

    const Dictionary reply
        = reply_of(world.connection->write_batch(operations));
    NETW_CHECK_EQ(int(reply["error"]), int(OK));
    const PackedInt32Array errors = reply["errors"];
    NETW_CHECK_EQ(errors.size(), 3);
    NETW_CHECK_EQ(errors[0], int(OK));
    NETW_CHECK_EQ(errors[1], int(ERR_INVALID_DATA));
    NETW_CHECK_EQ(errors[2], int(OK));
    NETW_CHECK_EQ(PackedByteArray(reply["uncertain"]).size(), 3);

    FileStore store(world.root);
    CHECK(
        netw::gd::file_exists(
            store.path_of("slot1", address(world, Kind::RECORD, "one"))
        )
    );
    CHECK_FALSE(
        netw::gd::file_exists(
            store.path_of("slot1", address(world, Kind::RECORD, "two"))
        )
    );
    CHECK(
        netw::gd::file_exists(
            store.path_of("slot1", address(world, Kind::RECORD, "three"))
        )
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] FS9 a read of a record that was never stored "
    "answers absence at OK, and a scan pages the directory in key order"
) {
    scrub("fs9");
    World world("fs9");
    const Dictionary missing = reply_of(
        world.connection->read(address(world, Kind::RECORD, "ghost"))
    );
    NETW_CHECK_EQ(int(missing["error"]), int(OK));
    NETW_CHECK_EQ(bool(missing["found"]), false);
    CHECK_FALSE(missing.has("envelope"));

    FileStore store(world.root);
    const char *keys[] = {"a", "b", "c"};
    for (int at = 0; at < 3; ++at) {
        NETW_CHECK_EQ(
            store.replace(
                "slot1",
                address(world, Kind::RECORD, keys[at]),
                sealed(world, at + 1)
            ),
            OK
        );
    }

    Dictionary request;
    request["schema_name"] = String(world.record()->name);
    request["kind"] = int(Kind::RECORD);
    request["cursor"] = String();
    request["limit"] = 2;
    const Dictionary page = reply_of(world.connection->scan(request));
    NETW_CHECK_EQ(int(page["error"]), int(OK));
    const Array rows = page["records"];
    NETW_CHECK_EQ(rows.size(), 2);
    CHECK(bool(Dictionary(rows[0])["key"] == String("a")));
    CHECK(bool(Dictionary(rows[1])["key"] == String("b")));
    CHECK_FALSE(String(page["cursor"]).is_empty());

    request["cursor"] = page["cursor"];
    const Dictionary rest = reply_of(world.connection->scan(request));
    const Array tail = rest["records"];
    NETW_CHECK_EQ(tail.size(), 1);
    CHECK(bool(Dictionary(tail[0])["key"] == String("c")));
    CHECK(String(rest["cursor"]).is_empty());
}

struct Door {
    String root;
    Ref<NetwMultiplayer> session;
    Ref<NetwSchema> schema;
    Ref<NetwDatabase> db;

    explicit Door(const char *p_name) {
        schema_model::clear();
        root = scratch_root(p_name);
        session.instantiate();
        schema = NetwSchema::create("players");
        schema->replicated(false);
        schema->i64("gold", 1);

        Ref<FileSystemDatabase> backend;
        backend.instantiate();
        backend->set_root(root);
        Ref<NetwDatabaseConfig> config;
        config.instantiate();
        config->backend(backend);
        db = NetwDatabase::over(
            session->database_create("saves", config),
            session.ptr()
        );
    }

    ~Door() {
        schema_model::clear();
    }
};

TEST_CASE(
    "[Networked][Table][Hosted] FS10 the published file backend defaults its "
    "root to the user directory and opens a slot no session has opened yet"
) {
    Ref<FileSystemDatabase> backend;
    backend.instantiate();
    CHECK(bool(backend->get_root() == String("user://saves")));

    scrub("fs10");
    {
        Door door("fs10");
        NETW_CHECK_EQ(int(door.db->open("slot1")->get_result()), int(OK));
        Dictionary values;
        values["gold"] = int64_t(11);
        NETW_CHECK_EQ(
            int(door.db->write(door.schema, "hero", values)->get_result()),
            int(OK)
        );
        CHECK(
            netw::gd::file_exists(FileStore(door.root).path_of(
                "slot1",
                netw::persist::address_of(Kind::RECORD, "players", "hero")
            ))
        );
    }

    netw::persist::forget_stores();
    Door again("fs10");
    const Dictionary listed = again.db->list_slots()->get_result();
    NETW_CHECK_EQ(int(listed["error"]), OK);
    NETW_CHECK_EQ(PackedStringArray(listed["slots"]).has("slot1"), true);

    NETW_CHECK_EQ(int(again.db->open("slot1")->get_result()), int(OK));
    const Dictionary read = again.db->read(again.schema, "hero")->get_result();
    NETW_CHECK_EQ(int(read["error"]), OK);
    CHECK(bool(read["found"]));
    NETW_CHECK_EQ(int64_t(Dictionary(read["values"])["gold"]), int64_t(11));
}

TEST_CASE(
    "[Networked][Table][Hosted] FS11 the published file backend deletes a slot "
    "it is not holding open and stops listing it"
) {
    scrub("fs11");
    Door door("fs11");
    NETW_CHECK_EQ(int(door.db->open("slot1")->get_result()), int(OK));
    Dictionary values;
    values["gold"] = int64_t(2);
    NETW_CHECK_EQ(
        int(door.db->write(door.schema, "hero", values)->get_result()),
        int(OK)
    );

    FileStore store(door.root);
    NETW_CHECK_EQ(store.open_slot("slot2"), OK);
    CHECK(store.has_slot("slot2"));

    const Ref<NetwPromise> removed = door.db->delete_slot("slot2");
    CHECK(removed->get_is_completed());
    NETW_CHECK_EQ(int(removed->get_result()), int(OK));
    CHECK_FALSE(store.has_slot("slot2"));
    const Dictionary listed = door.db->list_slots()->get_result();
    const PackedStringArray listed_slots = listed["slots"];
    NETW_CHECK_EQ(listed_slots.has("slot2"), false);
    NETW_CHECK_EQ(listed_slots.has("slot1"), true);
    CHECK(store.has_slot("slot1"));
}

} // namespace TestFileStoreLaws

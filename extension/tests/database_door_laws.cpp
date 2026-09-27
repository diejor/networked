#include "support/netw_test.h"

#include "netw/api/database.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"

namespace TestDatabaseDoor {

using namespace godot;
using netw::MemoryDatabase;
using netw::NetwDatabase;
using netw::NetwDatabaseConfig;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwSchema;
using netw::NetwWriteBatch;
namespace schema_model = netw::schema_model;

struct World {
    Ref<NetwMultiplayer> session;
    Ref<NetwSchema> schema;
    Ref<NetwDatabase> db;

    World(const String &p_store) {
        schema_model::clear();
        netw::persist::forget_stores();
        session.instantiate();
        schema = NetwSchema::create("players");
        schema->replicated(false);
        schema->i64("gold", 1);
        schema->string("title", 1);

        Ref<MemoryDatabase> backend;
        backend.instantiate();
        backend->set_store(StringName(p_store));
        Ref<NetwDatabaseConfig> config;
        config.instantiate();
        config->backend(backend);
        const RID made = session->database_create("saves", config);
        db = NetwDatabase::over(made, session.ptr());
    }

    ~World() {
        schema_model::clear();
        netw::persist::forget_stores();
    }
};

Dictionary row(int64_t p_gold, const String &p_title) {
    Dictionary out;
    out["gold"] = p_gold;
    out["title"] = p_title;
    return out;
}

TEST_CASE(
    "[Networked][Table][Hosted] DD1 the typed handle carries its name, slot "
    "and state, and answers them from the session"
) {
    World world("dd1");
    CHECK(world.db.is_valid());
    CHECK(bool(world.db->get_database_name() == StringName("saves")));
    CHECK(bool(world.db->get_slot() == StringName()));
    NETW_CHECK_EQ(
        int(world.db->get_state()),
        int(NetwMultiplayer::DATABASE_CLOSED)
    );
    CHECK(world.db->get_is_valid());

    NETW_CHECK_EQ(int(world.db->open("slot1")->get_result()), int(OK));
    NETW_CHECK_EQ(
        int(world.db->get_state()),
        int(NetwMultiplayer::DATABASE_OPEN)
    );
    CHECK(bool(world.db->get_slot() == StringName("slot1")));
}

TEST_CASE(
    "[Networked][Table][Hosted] DD2 a database with no backend refuses to "
    "open rather than opening onto nothing"
) {
    schema_model::clear();
    Ref<NetwMultiplayer> session;
    session.instantiate();
    Ref<NetwDatabaseConfig> config;
    config.instantiate();
    const Ref<NetwDatabase> db = NetwDatabase::over(
        session->database_create("saves", config),
        session.ptr()
    );
    const Ref<NetwPromise> opening = db->open("slot1");
    CHECK(opening->get_is_failed());
    NETW_CHECK_EQ(opening->get_code(), ERR_UNCONFIGURED);
    schema_model::clear();
}

TEST_CASE(
    "[Networked][Table][Hosted] DD3 a record written through the handle reads "
    "back through it, and a schema declaration needs no separate registration"
) {
    World world("dd3");
    world.db->open("slot1");

    NETW_CHECK_EQ(
        int(world.db->write(world.schema, "hero", row(7, "knight"))
                ->get_result()),
        int(OK)
    );
    const Dictionary read = world.db->read(world.schema, "hero")->get_result();
    NETW_CHECK_EQ(int(read["error"]), OK);
    CHECK(bool(read["found"]));
    const Dictionary read_values = read["values"];
    NETW_CHECK_EQ(int64_t(read_values["gold"]), int64_t(7));
    CHECK(bool(String(read_values["title"]) == String("knight")));
}

TEST_CASE(
    "[Networked][Table][Hosted] DD4 a batch reaches storage only at submit, "
    "and seals once it has"
) {
    World world("dd4");
    world.db->open("slot1");

    const Ref<NetwWriteBatch> batch = world.db->batch();
    NETW_CHECK_EQ(batch->write(world.schema, "one", row(1, "a")), OK);
    NETW_CHECK_EQ(batch->erase(world.schema, "two"), OK);
    NETW_CHECK_EQ(batch->get_size(), 2);
    CHECK_FALSE(
        bool(Dictionary(
            world.db->read(world.schema, "one")->get_result()
        )["found"])
    );

    const Dictionary result = batch->submit()->get_result();
    NETW_CHECK_EQ(int(result["error"]), OK);
    NETW_CHECK_EQ(PackedInt32Array(result["errors"]).size(), 2);
    CHECK(
        bool(Dictionary(
            world.db->read(world.schema, "one")->get_result()
        )["found"])
    );

    NETW_CHECK_EQ(batch->write(world.schema, "three", row(3, "c")), ERR_LOCKED);
    NETW_CHECK_EQ(
        int(Dictionary(batch->submit()->get_result())["error"]),
        ERR_LOCKED
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] DD5 a batch a builder refused performs no "
    "storage work, even when the caller ignored the builder's answer"
) {
    World world("dd5");
    world.db->open("slot1");

    const Ref<NetwWriteBatch> batch = world.db->batch();
    batch->write(world.schema, "one", row(1, "a"));
    Dictionary short_row;
    short_row["gold"] = 2;
    batch->write(world.schema, "two", short_row);

    const Dictionary result = batch->submit()->get_result();
    NETW_CHECK_ORDER(int(result["error"]), int(OK), !=);
    CHECK_FALSE(
        bool(Dictionary(
            world.db->read(world.schema, "one")->get_result()
        )["found"])
    );
}

TEST_CASE(
    "[Networked][Table][Hosted] DD6 a slot a backend holds is listed before "
    "it is opened, and the open one is refused deletion"
) {
    World world("dd6");
    world.db->open("slot1");
    world.db->write(world.schema, "hero", row(1, "a"));

    const Dictionary listed = world.db->list_slots()->get_result();
    NETW_CHECK_EQ(int(listed["error"]), OK);
    NETW_CHECK_ORDER(PackedStringArray(listed["slots"]).size(), 0, >);

    const Ref<NetwPromise> refused = world.db->delete_slot("slot1");
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(refused->get_code(), ERR_BUSY);
}

TEST_CASE(
    "[Networked][Table][Hosted] DD7 a handle whose session is gone answers "
    "the typed failure rather than reaching through a dangling pointer"
) {
    Ref<NetwDatabase> orphan;
    {
        World world("dd7");
        world.db->open("slot1");
        orphan = world.db;
    }
    CHECK_FALSE(orphan->get_is_valid());
    CHECK(bool(orphan->get_database_name() == StringName()));
    NETW_CHECK_EQ(
        int(orphan->get_state()),
        int(NetwMultiplayer::DATABASE_CLOSED)
    );
    CHECK(orphan->close()->get_is_failed());
}

TEST_CASE(
    "[Networked][Table][Hosted] DD8 declaring one name twice keeps one "
    "database, and a second backend under that name is refused"
) {
    World world("dd8");
    const RID first = world.session->database_find("saves");
    CHECK(first.is_valid());

    Ref<NetwDatabaseConfig> same;
    same.instantiate();
    same->backend(world.session->database_backend_of(first));
    NETW_CHECK_EQ(
        int(world.session->database_create("saves", same) == first),
        1
    );

    Ref<MemoryDatabase> other_backend;
    other_backend.instantiate();
    other_backend->set_store("dd8-other");
    Ref<NetwDatabaseConfig> other;
    other.instantiate();
    other->backend(other_backend);
    CHECK_FALSE(world.session->database_create("saves", other).is_valid());
}

} // namespace TestDatabaseDoor

#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/database.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"
#include "netw/api/persistence.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/envelope.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "support/netw_call_log.h"

namespace TestPersistenceFlatRelay {

using namespace godot;
using netw::NetwDatabase;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPersistence;
using netw::NetwPromise;
using netw::NetwQuantize;
using netw::NetwSchema;
using netw::persist::MemoryConnection;
using netw_test::CallLog;
namespace model = netw::script::model;
namespace schema_model = netw::schema_model;

TEST_CASE(
    "[Networked][Persistence][Hosted] PX1 a database handle's failed relay "
    "answers only its own database's failure, and ignores another "
    "database's"
) {
    schema_model::clear();
    netw::persist::forget_stores();
    Ref<NetwMultiplayer> session;
    session.instantiate();
    Node *account = memnew(Node);
    account->set_name("hero");
    Ref<NetwSchema> schema = NetwSchema::create("players");
    schema->replicated(false);
    schema->vector2("where", Ref<NetwQuantize>(), 1);

    const RID one = session->get_databases()->create("saves_one");
    const RID two = session->get_databases()->create("saves_two");
    const Ref<MemoryConnection> connection_one
        = MemoryConnection::opened("px1a", "slot1");
    const Ref<MemoryConnection> connection_two
        = MemoryConnection::opened("px1b", "slot1");
    session->get_databases()
        ->open(one, "slot1", NetwPromise::resolved(connection_one));
    session->get_databases()
        ->open(two, "slot1", NetwPromise::resolved(connection_two));

    const Ref<NetwDatabase> handle_one = session->database_handle(one);
    const Ref<NetwDatabase> handle_two = session->database_handle(two);
    CallLog log_one;
    CallLog log_two;
    handle_one->connect("failed", log_one.callable("failed"));
    handle_two->connect("failed", log_two.callable("failed"));

    Node2D *root = memnew(Node2D);
    root->set_name("A");
    netw::Netw::configure_persistence(root)
        ->database(StringName("saves_one"))
        ->table(schema->get_schema_name())
        ->record_id(Callable(account, "get_name"));
    model::configure_node_property(root, "position")
        ->persisted(StringName("where"));
    const RID binding = session->persist_bind(root);
    REQUIRE(binding.is_valid());

    root->set_position(Vector2(2, 2));
    connection_one->fail_next(ERR_FILE_CANT_WRITE);
    CHECK(session->persist_save_binding(binding)->get_is_failed());

    NETW_CHECK_EQ(log_one.count("failed"), 1);
    NETW_CHECK_EQ(log_two.count("failed"), 0);

    model::clear_node_overlay(root);
    memdelete(root);
    memdelete(account);
    schema_model::clear();
    netw::persist::forget_stores();
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PX2 a persistence handle's loaded and "
    "saved relays answer only their own entity, and ignore another entity's"
) {
    schema_model::clear();
    netw::persist::forget_stores();
    Ref<NetwMultiplayer> session;
    session.instantiate();
    Node *account = memnew(Node);
    account->set_name("hero");
    Ref<NetwSchema> schema = NetwSchema::create("players");
    schema->replicated(false);
    schema->vector2("where", Ref<NetwQuantize>(), 1);

    const RID database = session->get_databases()->create("saves");
    const Ref<MemoryConnection> connection
        = MemoryConnection::opened("px2", "slot1");
    session->get_databases()
        ->open(database, "slot1", NetwPromise::resolved(connection));

    Node2D *root_a = memnew(Node2D);
    root_a->set_name("A");
    Node2D *root_b = memnew(Node2D);
    root_b->set_name("B");

    netw::Netw::configure_persistence(root_a)
        ->database(StringName("saves"))
        ->table(schema->get_schema_name())
        ->record_id(Callable(account, "get_name"));
    netw::Netw::configure_persistence(root_b)
        ->database(StringName("saves"))
        ->table(schema->get_schema_name())
        ->record_id(Callable(account, "get_name"));
    model::configure_node_property(root_a, "position")
        ->persisted(StringName("where"));
    model::configure_node_property(root_b, "position")
        ->persisted(StringName("where"));

    const RID binding_a = session->persist_bind(root_a);
    const RID binding_b = session->persist_bind(root_b);
    REQUIRE(binding_a.is_valid());
    REQUIRE(binding_b.is_valid());

    const Ref<NetwPersistence> handle_a
        = NetwEntity::resolve(root_a)->get_persistence();
    const Ref<NetwPersistence> handle_b
        = NetwEntity::resolve(root_b)->get_persistence();
    CallLog log_a;
    CallLog log_b;
    handle_a->connect("saved", log_a.callable("saved"));
    handle_b->connect("saved", log_b.callable("saved"));

    root_a->set_position(Vector2(2, 2));
    REQUIRE(session->persist_save_binding(binding_a)->get_is_completed());

    NETW_CHECK_EQ(log_a.count("saved"), 1);
    NETW_CHECK_EQ(log_b.count("saved"), 0);

    model::clear_node_overlay(root_a);
    model::clear_node_overlay(root_b);
    memdelete(root_a);
    memdelete(root_b);
    memdelete(account);
    schema_model::clear();
    netw::persist::forget_stores();
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PX3 an entity with no persistence "
    "binding answers ERR_UNCONFIGURED from load and save and false or empty "
    "from the getters, and a dead or foreign RID answers the same way"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    const RID entity = session->entity_create();
    REQUIRE(entity.is_valid());

    const Ref<NetwPromise> loading = session->persist_load(entity);
    CHECK(loading->get_is_failed());
    NETW_CHECK_EQ(int(loading->get_code()), int(ERR_UNCONFIGURED));

    const Ref<NetwPromise> saving = session->persist_save(entity);
    CHECK(saving->get_is_failed());
    NETW_CHECK_EQ(int(saving->get_code()), int(ERR_UNCONFIGURED));

    CHECK_FALSE(session->persist_is_dirty(entity));
    CHECK(bool(session->persist_get_record_id(entity) == StringName()));

    SUBCASE("a dead RID") {
        const RID dead;
        CHECK(session->persist_load(dead)->get_is_failed());
        CHECK_FALSE(session->persist_is_dirty(dead));
        CHECK(bool(session->persist_get_record_id(dead) == StringName()));
    }

    SUBCASE("a foreign RID from another session") {
        Ref<NetwMultiplayer> other;
        other.instantiate();
        const RID foreign = other->entity_create();
        REQUIRE(foreign.is_valid());
        CHECK(session->persist_load(foreign)->get_is_failed());
        CHECK_FALSE(session->persist_is_dirty(foreign));
    }
}

Dictionary keep_properties(const Dictionary &p_values) {
    return p_values;
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] PX4 a persistence schema "
    "reads saved properties by their saved names and survives its root"
) {
    schema_model::clear();
    netw::persist::forget_stores();
    Ref<NetwMultiplayer> session;
    session.instantiate();
    netw::MultiplayerViewport *viewport = memnew(netw::MultiplayerViewport);
    viewport->set_api(session);
    netw::gd::scene_root()->add_child(viewport);

    const RID database = session->get_databases()->create("saves");
    const Ref<MemoryConnection> connection
        = MemoryConnection::opened("px4", "slot1");
    session->get_databases()
        ->open(database, "slot1", NetwPromise::resolved(connection));

    Node2D *root = memnew(Node2D);
    const Ref<NetwEntity> entity = netw::Netw::configure_entity(root);
    entity->set_name("hero");
    netw::Netw::configure_persistence(root)
        ->database(StringName("saves"))
        ->table(StringName("players"))
        ->record_id(Callable(entity.ptr(), "get_name"))
        ->load_on_spawn(false)
        ->storage_version(2)
        ->migrate(1, callable_mp_static(&keep_properties));
    model::configure_node_property(root, "position")
        ->persisted(StringName("where"));
    const Ref<NetwPersistence> persistence = entity->get_persistence();
    CHECK_FALSE(persistence->get_schema().is_valid());
    viewport->add_child(root);

    const Ref<NetwSchema> schema = persistence->get_schema();
    REQUIRE(schema.is_valid());
    CHECK(schema == persistence->get_schema());
    CHECK_FALSE(schema->is_replicated());
    CHECK(bool(schema->get_schema_name() == StringName("players")));
    NETW_CHECK_EQ(schema->get_storage_version(), 2);
    CHECK(schema->get_migrations().has(1));
    ERR_PRINT_OFF;
    schema->storage_version(99);
    NETW_CHECK_EQ(schema->vector2("unexpected", Ref<NetwQuantize>(), 1), -1);
    ERR_PRINT_ON;
    NETW_CHECK_EQ(schema->get_storage_version(), 2);
    const Ref<NetwDatabase> storage = session->database_handle(database);
    Dictionary values;
    values["where"] = Vector2(3, 4);
    const Ref<NetwPromise> saved = storage->write(schema, "hero", values);
    REQUIRE(saved->get_is_completed());
    const Dictionary read = storage->read(schema, "hero")->get_result();
    CHECK(bool(read.get("found", false)));
    const Dictionary stored = read.get("values", Dictionary());
    CHECK(bool(Vector2(stored.get("where", Vector2())) == Vector2(3, 4)));
    CHECK_FALSE(stored.has("position"));

    model::clear_node_overlay(root);
    memdelete(root);
    CHECK_FALSE(persistence->get_schema().is_valid());
    const Dictionary after = storage->read(schema, "hero")->get_result();
    CHECK(bool(after.get("found", false)));

    memdelete(viewport);
    const netw::SchemaRecord *record
        = session->get_schema_core()->record_of(
            session->schema_find("players")
        );
    REQUIRE(record != nullptr);
    netw::persist::store_named("px4").replace(
        "slot1",
        netw::persist::address_text(netw::persist::address_of(
            netw::persist::Kind::RECORD,
            "players",
            "hero"
        )),
        netw::persist::seal_record(*record, 1, values)
    );
    Ref<NetwMultiplayer> reader;
    reader.instantiate();
    const RID reader_database = reader->get_databases()->create("saves");
    reader->get_databases()->open(
        reader_database,
        "slot1",
        NetwPromise::resolved(MemoryConnection::opened("px4", "slot1"))
    );
    const Ref<NetwPromise> migrated
        = reader->database_handle(reader_database)->read(schema, "hero");
    REQUIRE(migrated->get_is_completed());
    const Dictionary migrated_record = migrated->get_result();
    CHECK(bool(migrated_record.get("found", false)));
    const Dictionary migrated_values
        = migrated_record.get("values", Dictionary());
    CHECK(bool(
        Vector2(migrated_values.get("where", Vector2())) == Vector2(3, 4)
    ));
    schema_model::clear();
    netw::persist::forget_stores();
}

} // namespace TestPersistenceFlatRelay

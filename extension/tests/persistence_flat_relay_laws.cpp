#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/database.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/persistence_handle.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "support/netw_call_log.h"

namespace TestPersistenceFlatRelay {

using namespace godot;
using netw::NetwDatabase;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPersistenceHandle;
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

    const Ref<NetwDatabase> handle_one = NetwDatabase::over(one, session.ptr());
    const Ref<NetwDatabase> handle_two = NetwDatabase::over(two, session.ptr());
    CallLog log_one;
    CallLog log_two;
    handle_one->connect("failed", log_one.callable("failed"));
    handle_two->connect("failed", log_two.callable("failed"));

    Node2D *root = memnew(Node2D);
    root->set_name("A");
    netw::Netw::configure_persistence(root)
        ->database(StringName("saves_one"))
        ->schema(schema)
        ->record_id(Callable(account, "get_name"));
    model::configure_node_property(root, "position")
        ->persisted(schema->column_ref(0));
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
        ->schema(schema)
        ->record_id(Callable(account, "get_name"));
    netw::Netw::configure_persistence(root_b)
        ->database(StringName("saves"))
        ->schema(schema)
        ->record_id(Callable(account, "get_name"));
    model::configure_node_property(root_a, "position")
        ->persisted(schema->column_ref(0));
    model::configure_node_property(root_b, "position")
        ->persisted(schema->column_ref(0));

    const RID binding_a = session->persist_bind(root_a);
    const RID binding_b = session->persist_bind(root_b);
    REQUIRE(binding_a.is_valid());
    REQUIRE(binding_b.is_valid());

    const Ref<NetwPersistenceHandle> handle_a
        = NetwEntity::resolve(root_a)->get_persistence();
    const Ref<NetwPersistenceHandle> handle_b
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

} // namespace TestPersistenceFlatRelay

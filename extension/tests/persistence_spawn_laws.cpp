#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/session_core.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)
#include "support/minted_script.h"
#endif

namespace TestPersistenceSpawn {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwQuantize;
using netw::NetwSchema;
using netw::persist::MemoryConnection;
using netw::spawn::Book;
using netw::spawn::Record;
using netw_test::CallLog;
namespace model = netw::script::model;
namespace schema_model = netw::schema_model;

const int64_t SPAWN_CHANNEL = 14;
const int64_t PEER = 4;

PackedByteArray a_frame() {
    PackedByteArray bytes;
    bytes.push_back(7);
    return bytes;
}

struct World {
    Ref<NetwMultiplayer> session;
    Ref<NetwSchema> schema;
    Ref<MemoryConnection> connection;
    RID database;
    Book book;
    Node *arena = nullptr;
    Node2D *root = nullptr;
    Node2D *limb = nullptr;
    Node *account = nullptr;

    World(const String &p_store, Node2D *p_root = nullptr) {
        schema_model::clear();
        netw::persist::forget_stores();
        session.instantiate();
        account = memnew(Node);
        account->set_name("hero");

        schema = NetwSchema::create("players");
        schema->replicated(false);
        schema->vector2("where", Ref<NetwQuantize>(), 1);
        schema->f32("spin", Ref<NetwQuantize>(), 1);

        database = session->get_databases()->create("saves");
        connection = MemoryConnection::opened(p_store, "slot1");
        session->get_databases()
            ->open(database, "slot1", NetwPromise::resolved(connection));

        arena = memnew(Node);
        arena->set_name("Arena");
        netw::gd::scene_root()->add_child(arena);
        root = p_root != nullptr ? p_root : memnew(Node2D);
        root->set_name("Player");
        root->set_position(Vector2(1, 1));
        limb = memnew(Node2D);
        limb->set_name("Turret");
        root->add_child(limb);

        netw::Netw::configure_persistence(root)
            ->database(StringName("saves"))
            ->schema(schema)
            ->record_id(Callable(account, "get_name"));
    }

    ~World() {
        if (limb != nullptr) {
            model::clear_node_overlay(limb);
        }
        if (root != nullptr) {
            model::clear_node_overlay(root);
            memdelete(root);
        }
        netw::gd::scene_root()->remove_child(arena);
        memdelete(arena);
        memdelete(account);
        schema_model::clear();
        netw::persist::forget_stores();
    }

    void bind(Node *p_node, const StringName &p_property, int p_column) {
        model::configure_node_property(p_node, p_property)
            ->persisted(schema->column_ref(p_column));
    }

    void bind_both() {
        bind(root, "position", 0);
        bind(limb, "rotation", 1);
    }

    void store(const Vector2 &p_where) {
        Dictionary row;
        row["where"] = p_where;
        row["spin"] = 0.5;
        const Ref<NetwPromise> wrote = session->database_write(
            database,
            session->schema_of_declaration(schema),
            "hero",
            row
        );
        REQUIRE(wrote->get_is_completed());
    }

    Record *spawned() {
        arena->add_child(root);
        Record record;
        record.set_route(1);
        record.bind_node(root);
        return book.issue(record);
    }

    int published(CallLog &r_log) {
        session->spawn_replay_to(
            &book,
            PEER,
            SPAWN_CHANNEL,
            r_log.answering("encode", a_frame())
        );
        return r_log.count("encode");
    }

    bool planned() {
        PackedInt32Array peers;
        peers.push_back(int32_t(PEER));
        return !session->spawn_reconcile_rows(&book, peers).is_empty();
    }

    RID binding() const {
        return session->get_bindings()->find(root);
    }
};

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL1 a load still in flight "
    "withholds the entity's first publication, and the stored row is applied "
    "before any peer is sent it"
) {
    World world("sl1");
    world.bind_both();
    world.store(Vector2(9, 9));
    world.connection->defer(true);
    Record *record = world.spawned();
    CallLog log;

    CHECK(world.session->persist_enroll(world.root));
    CHECK(world.session->persist_withholds(world.root));
    NETW_CHECK_EQ(world.published(log), 0);
    CHECK_FALSE(world.planned());
    CHECK_FALSE(record->has_recipient(int(PEER)));
    CHECK(bool(world.root->get_position() == Vector2(1, 1)));

    world.connection->release();

    CHECK(bool(world.root->get_position() == Vector2(9, 9)));
    CHECK_FALSE(world.session->persist_withholds(world.root));
    CHECK_FALSE(world.session->persist_is_dirty_binding(world.binding()));
    NETW_CHECK_EQ(world.published(log), 1);
    CHECK(world.planned());
    CHECK(record->has_recipient(int(PEER)));
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL2 a record that is not "
    "stored yet publishes the values the entity was spawned with, and leaves "
    "them owed to the next save"
) {
    World world("sl2");
    world.bind_both();
    world.connection->defer(true);
    world.spawned();
    CallLog log;

    CHECK(world.session->persist_enroll(world.root));
    NETW_CHECK_EQ(world.published(log), 0);

    world.connection->release();

    CHECK(bool(world.root->get_position() == Vector2(1, 1)));
    CHECK_FALSE(world.session->persist_withholds(world.root));
    CHECK(world.session->persist_is_dirty_binding(world.binding()));
    NETW_CHECK_EQ(world.published(log), 1);
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL3 a load that fails keeps "
    "the entity unpublished, saves nothing over the stored row, and a "
    "successful retry publishes it"
) {
    World world("sl3");
    world.bind_both();
    world.store(Vector2(9, 9));
    world.connection->fail_next(ERR_FILE_CANT_READ);
    world.spawned();
    CallLog log;

    CHECK(world.session->persist_enroll(world.root));

    CHECK(world.session->persist_withholds(world.root));
    CHECK(bool(world.root->get_position() == Vector2(1, 1)));
    NETW_CHECK_EQ(world.published(log), 0);
    CHECK(
        world.session->persist_save_binding(world.binding())->get_is_failed()
    );

    const Ref<NetwPromise> retried
        = world.session->persist_load_binding(world.binding());
    REQUIRE(retried->get_is_completed());
    CHECK(bool(retried->get_result()));
    CHECK(bool(world.root->get_position() == Vector2(9, 9)));
    NETW_CHECK_EQ(world.published(log), 1);
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL4 a bound property that "
    "moves while the load is in flight refuses the stored row, and the entity "
    "stays unpublished"
) {
    World world("sl4");
    world.bind_both();
    world.store(Vector2(9, 9));
    world.connection->defer(true);
    world.spawned();
    CallLog log;

    world.session->persist_enroll(world.root);
    const Ref<NetwPromise> shared
        = world.session->persist_load_binding(world.binding());
    world.root->set_position(Vector2(2, 2));
    world.connection->release();

    CHECK(shared->get_is_failed());
    NETW_CHECK_EQ(int(shared->get_code()), int(ERR_BUSY));
    CHECK(bool(world.root->get_position() == Vector2(2, 2)));
    CHECK(world.session->persist_withholds(world.root));
    NETW_CHECK_EQ(world.published(log), 0);
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL5 an entity declared with "
    "load_on_spawn(false) publishes at once and reads nothing"
) {
    World world("sl5");
    world.bind_both();
    world.store(Vector2(9, 9));
    netw::Netw::configure_persistence(world.root)->load_on_spawn(false);
    world.spawned();
    CallLog log;

    CHECK_FALSE(world.session->persist_enroll(world.root));
    CHECK(bool(world.root->get_position() == Vector2(1, 1)));
    NETW_CHECK_EQ(world.published(log), 1);
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL6 an entity whose binding "
    "does not compile is never published as loaded"
) {
    World world("sl6");
    world.bind(world.root, "position", 0);
    world.spawned();
    CallLog log;

    CHECK(world.session->persist_enroll(world.root));
    CHECK_FALSE(world.binding().is_valid());
    NETW_CHECK_EQ(world.published(log), 0);
}

TEST_CASE(
    "[Networked][Persistence][Hosted][SceneTree] SL7 a replica never reads the "
    "row, withholds nothing, and refuses an explicit load"
) {
    World world("sl7");
    world.bind_both();
    world.store(Vector2(9, 9));
    world.session->session_plane().set_role(netw::SessionCore::ROLE_CLIENT);
    world.spawned();

    CHECK_FALSE(world.session->persist_enroll(world.root));
    CHECK_FALSE(world.session->persist_withholds(world.root));

    const Ref<NetwPromise> refused = world.session->persist_load_binding(
        world.session->persist_bind(world.root)
    );
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(int(refused->get_code()), int(ERR_UNAUTHORIZED));
    CHECK(bool(world.root->get_position() == Vector2(1, 1)));
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Persistence][SceneTree] SL8 a setter that frees another bound "
    "node while the stored row is applied withholds completion and publication"
) {
    Node2D *scripted = Object::cast_to<Node2D>(netw_test::minted_node(
        "extends Node2D\n"
        "var spin := 0.0:\n"
        "\tset(value):\n"
        "\t\tspin = value\n"
        "\t\tvar limb := get_node_or_null(^\"Turret\")\n"
        "\t\tif limb != null:\n"
        "\t\t\tremove_child(limb)\n"
        "\t\t\tlimb.free()\n"
    ));
    REQUIRE(scripted != nullptr);
    World world("sl8", scripted);
    world.bind(world.root, "spin", 1);
    world.bind(world.limb, "position", 0);
    world.store(Vector2(9, 9));
    world.connection->defer(true);
    world.spawned();
    CallLog log;

    world.session->persist_enroll(world.root);
    const Ref<NetwPromise> shared
        = world.session->persist_load_binding(world.binding());
    model::clear_node_overlay(world.limb);
    world.limb = nullptr;
    world.connection->release();

    CHECK(shared->get_is_failed());
    CHECK(world.session->persist_withholds(world.root));
    NETW_CHECK_EQ(world.published(log), 0);
}

#endif

} // namespace TestPersistenceSpawn

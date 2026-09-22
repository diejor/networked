#include "support/netw_test.h"

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/persist/binding.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/session_core.hpp"

#if defined(NETW_TIER_HOSTED)
#include "support/declared_nodes.h"
#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"
#endif

namespace TestPersistenceLifecycle {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwQuantize;
using netw::NetwSchema;
using netw::SessionCore;
using netw::persist::MemoryConnection;
namespace model = netw::script::model;
namespace schema_model = netw::schema_model;

Ref<NetwSchema> players_schema() {
    Ref<NetwSchema> schema = NetwSchema::create("players");
    schema->replicated(false);
    schema->vector2("where", Ref<NetwQuantize>(), 1);
    schema->f32("spin", Ref<NetwQuantize>(), 1);
    return schema;
}

Vector2 stored_where(
    const Ref<NetwMultiplayer> &p_session,
    const RID &p_database
) {
    const Ref<NetwPromise> read = p_session->database_read(
        p_database,
        p_session->schema_find("players"),
        "hero"
    );
    const Dictionary stored = read->get_result();
    if (!bool(stored["found"])) {
        return Vector2(-1, -1);
    }
    return Vector2(Dictionary(stored["values"])["where"]);
}

struct World {
    String store;
    Ref<NetwMultiplayer> session;
    Ref<NetwSchema> schema;
    Ref<MemoryConnection> connection;
    RID database;
    Node2D *root = nullptr;
    Node2D *limb = nullptr;
    Node *account = nullptr;
    ObjectID root_id;

    World(const String &p_store, Node2D *p_root = nullptr) : store(p_store) {
        schema_model::clear();
        netw::persist::forget_stores();
        session.instantiate();
        account = memnew(Node);
        account->set_name("hero");
        schema = players_schema();

        database = session->get_databases()->create("saves");
        connection = MemoryConnection::opened(p_store, "slot1");
        session->get_databases()->open(
            database,
            "slot1",
            NetwPromise::resolved(connection)
        );

        root = p_root != nullptr ? p_root : memnew(Node2D);
        root->set_name("Player");
        root->set_position(Vector2(1, 1));
        limb = memnew(Node2D);
        limb->set_name("Turret");
        root->add_child(limb);
        root_id = netw::gd::instance_id(root);

        netw::Netw::configure_persistence(root)
            ->database(StringName("saves"))
            ->schema(schema)
            ->record_id(Callable(account, "get_name"));
    }

    ~World() {
        free_root();
        memdelete(account);
        schema_model::clear();
        netw::persist::forget_stores();
    }

    void free_root() {
        if (root == nullptr) {
            return;
        }
        if (limb != nullptr) {
            model::clear_node_overlay(limb);
        }
        model::clear_node_overlay(root);
        memdelete(root);
        root = nullptr;
        limb = nullptr;
    }

    void bind(Node *p_node, const StringName &p_property, int p_column) {
        model::configure_node_property(p_node, p_property)
            ->persisted(schema->column_ref(p_column));
    }

    void bind_both() {
        bind(root, "position", 0);
        bind(limb, "rotation", 1);
    }

    RID loaded() {
        bind_both();
        netw::Netw::configure_persistence(root)->load_on_spawn(false);
        session->persist_enroll(root);
        const RID made = binding();
        REQUIRE(made.is_valid());
        REQUIRE(session->persist_save_binding(made)->get_is_completed());
        return made;
    }

    RID binding() const {
        return session->get_bindings()->find_id(root_id);
    }

    const netw::persist::Binding *held(const RID &p_binding) const {
        return session->get_bindings()->at(p_binding);
    }

    void exit_and_free() {
        session->persist_capture_exit(root);
        root->set_position(Vector2(5, 5));
        free_root();
    }

    void settle(bool p_terminal) {
        session->persist_settle_departure(root_id, p_terminal);
    }

    void authority_away_and_back() {
        session->session_plane().set_role(SessionCore::ROLE_CLIENT);
        session->session_plane().set_role(SessionCore::ROLE_NONE);
    }
};

TEST_CASE(
    "[Networked][Persistence][Hosted] LC1 a terminal departure writes the "
    "values captured at the exit after the node is freed, and lets the "
    "binding go once the write settles"
) {
    World world("lc1");
    const RID binding = world.loaded();
    world.root->set_position(Vector2(8, 8));
    world.connection->defer(true);

    world.exit_and_free();
    world.settle(true);

    NETW_CHECK_EQ(world.connection->withheld_count(), 1);
    CHECK(world.session->get_bindings()->is_valid(binding));
    world.connection->release();
    world.connection->defer(false);

    CHECK_FALSE(world.session->get_bindings()->is_valid(binding));
    CHECK(bool(stored_where(world.session, world.database) == Vector2(8, 8)));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] LC2 a move keeps the binding and writes "
    "nothing, and a departure reached twice writes its captured row once"
) {
    World world("lc2");
    const RID binding = world.loaded();
    world.root->set_position(Vector2(8, 8));
    world.connection->defer(true);

    world.session->persist_capture_exit(world.root);
    world.settle(false);

    NETW_CHECK_EQ(world.connection->withheld_count(), 0);
    CHECK(bool(world.binding() == binding));
    CHECK_FALSE(world.held(binding)->departed);
    CHECK_FALSE(world.session->persist_withholds(world.root));

    world.session->persist_depart(binding, true);
    world.root->set_position(Vector2(9, 9));
    world.exit_and_free();
    world.settle(true);
    world.settle(true);

    NETW_CHECK_EQ(world.connection->withheld_count(), 1);
    world.connection->release();
    world.connection->defer(false);
    CHECK(bool(stored_where(world.session, world.database) == Vector2(8, 8)));
    CHECK_FALSE(world.session->get_bindings()->is_valid(binding));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] LC3 a despawn that opts out of the "
    "final save retires the binding without writing"
) {
    World world("lc3");
    const RID binding = world.loaded();
    world.root->set_position(Vector2(8, 8));

    world.session->persist_depart(binding, false);
    world.session->persist_pump(100.0);
    world.exit_and_free();
    world.settle(true);

    CHECK_FALSE(world.session->get_bindings()->is_valid(binding));
    CHECK(bool(stored_where(world.session, world.database) == Vector2(1, 1)));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] LC4 a root that departs while its spawn "
    "load is out, or after its binding failed to compile, writes nothing and "
    "is no longer withheld"
) {
    SUBCASE("the load is still in flight") {
        World world("lc4");
        world.bind_both();
        world.connection->defer(true);
        CHECK(world.session->persist_enroll(world.root));
        const Ref<NetwPromise> loading
            = world.session->persist_load_binding(world.binding());

        world.session->persist_capture_exit(world.root);
        world.settle(true);

        CHECK_FALSE(world.session->persist_withholds(world.root));
        CHECK_FALSE(world.binding().is_valid());
        NETW_CHECK_EQ(world.connection->withheld_count(), 1);
        world.connection->release();
        CHECK(loading->get_is_failed());
    }

    SUBCASE("the binding never compiled") {
        World world("lc4b");
        world.bind(world.root, "position", 0);
        CHECK(world.session->persist_enroll(world.root));
        CHECK_FALSE(world.binding().is_valid());

        world.settle(true);

        CHECK_FALSE(world.session->persist_withholds(world.root));
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] LC5 a final write the database refuses "
    "keeps the captured row, and the next flush writes it and lets the "
    "binding go"
) {
    World world("lc5");
    const RID binding = world.loaded();
    world.root->set_position(Vector2(8, 8));
    world.exit_and_free();
    world.connection->fail_next(ERR_FILE_CANT_WRITE);

    world.settle(true);

    CHECK(world.session->get_bindings()->is_valid(binding));
    NETW_CHECK_EQ(int(world.held(binding)->failed), int(ERR_FILE_CANT_WRITE));
    CHECK(bool(stored_where(world.session, world.database) == Vector2(1, 1)));

    NETW_CHECK_EQ(
        int(world.session->persist_flush_all()->get_result()),
        int(OK)
    );
    CHECK_FALSE(world.session->get_bindings()->is_valid(binding));
    CHECK(bool(stored_where(world.session, world.database) == Vector2(8, 8)));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] LC6 authority that leaves and returns "
    "while a write or a load is out discards the old completion"
) {
    SUBCASE("a write") {
        World world("lc6");
        const RID binding = world.loaded();
        world.root->set_position(Vector2(8, 8));
        world.connection->defer(true);
        const Ref<NetwPromise> saving = world.session->persist_save_binding(binding);

        world.authority_away_and_back();
        world.connection->release();
        world.connection->defer(false);

        CHECK(saving->get_is_failed());
        NETW_CHECK_EQ(int(saving->get_code()), int(ERR_UNAVAILABLE));
        CHECK(world.session->persist_is_dirty_binding(binding));
        CHECK(bool(world.session->persist_save_binding(binding)->get_result()));
        CHECK_FALSE(world.session->persist_is_dirty_binding(binding));
    }

    SUBCASE("a spawn load") {
        World world("lc6b");
        world.bind_both();
        Dictionary row;
        row["where"] = Vector2(9, 9);
        row["spin"] = 0.5;
        world.session->database_write(
            world.database,
            world.session->schema_of_declaration(world.schema),
            "hero",
            row
        );
        world.connection->defer(true);
        CHECK(world.session->persist_enroll(world.root));
        const Ref<NetwPromise> loading
            = world.session->persist_load_binding(world.binding());

        world.authority_away_and_back();
        world.connection->release();

        CHECK(loading->get_is_failed());
        NETW_CHECK_EQ(int(loading->get_code()), int(ERR_UNAVAILABLE));
        CHECK(bool(world.root->get_position() == Vector2(1, 1)));
        CHECK(world.session->persist_withholds(world.root));
    }
}

#if defined(NETW_TIER_HOSTED)

World *&reopening() {
    static World *held = nullptr;
    return held;
}

void reopen_saves() {
    World *world = reopening();
    reopening() = nullptr;
    if (world == nullptr) {
        return;
    }
    world->session->database_close(world->database);
    world->session->get_databases()->open(
        world->database,
        "slot1",
        NetwPromise::resolved(MemoryConnection::opened(world->store, "slot1"))
    );
}

TEST_CASE(
    "[Networked][Persistence][SceneTree] LC7 a load whose database closed "
    "and reopened while the row was applied does not publish the entity"
) {
    Node2D *scripted = Object::cast_to<Node2D>(netw_test::minted_node(
        "extends Node2D\n"
        "var hook := Callable()\n"
        "var spin := 0.0:\n"
        "\tset(value):\n"
        "\t\tspin = value\n"
        "\t\tif hook.is_valid():\n"
        "\t\t\thook.call()\n"
    ));
    REQUIRE(scripted != nullptr);
    World world("lc7", scripted);
    world.bind(world.root, "position", 0);
    world.bind(world.root, "spin", 1);
    Dictionary row;
    row["where"] = Vector2(9, 9);
    row["spin"] = 0.5;
    world.session->database_write(
        world.database,
        world.session->schema_of_declaration(world.schema),
        "hero",
        row
    );
    world.connection->defer(true);
    CHECK(world.session->persist_enroll(world.root));
    const Ref<NetwPromise> loading
        = world.session->persist_load_binding(world.binding());

    reopening() = &world;
    world.root->set("hook", callable_mp_static(&reopen_saves));
    world.connection->release();
    reopening() = nullptr;
    world.root->set("hook", Callable());

    CHECK(loading->get_is_failed());
    NETW_CHECK_EQ(int(loading->get_code()), int(ERR_UNAVAILABLE));
    CHECK(world.session->persist_withholds(world.root));
    NETW_CHECK_EQ(
        int(world.session->database_get_state(world.database)),
        int(NetwMultiplayer::DATABASE_OPEN)
    );
}

struct Cleared {
    Cleared() {
        schema_model::clear();
        netw::persist::forget_stores();
    }

    ~Cleared() {
        schema_model::clear();
        netw::persist::forget_stores();
    }
};

struct Leaving {
    Cleared cleared;
    netw_test::LoopbackRig rig;
    NetwMultiplayer *server = nullptr;
    Ref<NetwSchema> schema;
    Ref<MemoryConnection> connection;
    RID database;
    Node2D *root = nullptr;
    Node *account = nullptr;
    RID binding;

    Leaving() : rig(1) {
        rig.mount();
        server = netw_test::flow_core(rig.server());
        account = memnew(Node);
        account->set_name("hero");
        schema = NetwSchema::create("players");
        schema->replicated(false);
        schema->vector2("where", Ref<NetwQuantize>(), 1);

        database = server->get_databases()->create("saves");
        connection = MemoryConnection::opened("lc8", "slot1");
        server->get_databases()->open(
            database,
            "slot1",
            NetwPromise::resolved(connection)
        );
        root = memnew(Node2D);
        netw::Netw::configure_persistence(root)
            ->database(StringName("saves"))
            ->schema(schema)
            ->record_id(Callable(account, "get_name"));
        model::configure_node_property(root, "position")
            ->persisted(schema->column_ref(0));
        binding = server->persist_bind(root);
    }

    ~Leaving() {
        model::clear_node_overlay(root);
        memdelete(root);
        memdelete(account);
    }
};

TEST_CASE(
    "[Networked][Persistence][Session][SceneTree] LC8 a leave whose entity "
    "rows cannot be saved keeps the session online, and a retry that saves "
    "them goes on to close the peer"
) {
    Leaving stand;
    REQUIRE(stand.binding.is_valid());
    NETW_CHECK_EQ(
        int(stand.server->session_get_state()),
        int(NetwMultiplayer::SESSION_STATE_ONLINE)
    );
    stand.root->set_position(Vector2(4, 4));
    stand.connection->fail_next(ERR_FILE_CANT_WRITE);

    const Ref<NetwPromise> refused = stand.server->session_leave();

    CHECK(refused->get_is_completed());
    NETW_CHECK_EQ(int(refused->get_result()), int(ERR_FILE_CANT_WRITE));
    NETW_CHECK_EQ(
        int(stand.server->session_get_state()),
        int(NetwMultiplayer::SESSION_STATE_ONLINE)
    );
    CHECK(stand.server->persist_is_dirty_binding(stand.binding));

    stand.server->session_leave();

    CHECK_FALSE(stand.server->persist_is_dirty_binding(stand.binding));
    NETW_CHECK_EQ(
        int(stand.server->session_get_state()),
        int(NetwMultiplayer::SESSION_STATE_DISCONNECTING)
    );
}

struct Departing {
    Cleared cleared;
    netw_test::LoopbackRig rig;
    NetwMultiplayer *server = nullptr;
    Node *away = nullptr;
    netw_test::FlowPair pair;
    Ref<NetwSchema> schema;
    Ref<MemoryConnection> connection;
    RID database;
    Node *account = nullptr;
    ObjectID body_id;

    Departing() : rig(1) {
        rig.mount();
        netw_test::flow_clocks(rig, 30);
        pair = netw_test::stand_flow_pair(
            rig,
            netw_test::gdsrc::STATE_ONLY,
            "Keeper"
        );
        server = netw_test::flow_core(rig.server());
        away = memnew(Node);
        away->set_name("Away");
        rig.branch(-1)->add_child(away);

        account = memnew(Node);
        account->set_name("hero");
        schema = NetwSchema::create("players");
        schema->replicated(false);
        schema->f32("spin", Ref<NetwQuantize>(), 1);
        database = server->get_databases()->create("saves");
        connection = MemoryConnection::opened("lc9", "slot1");
        server->get_databases()->open(
            database,
            "slot1",
            NetwPromise::resolved(connection)
        );
        Node *body = pair.authored;
        netw::Netw::configure_persistence(body)
            ->database(StringName("saves"))
            ->schema(schema)
            ->record_id(Callable(account, "get_name"));
        model::configure_node_property(body, "rotation")
            ->persisted(schema->column_ref(0));
        body_id = netw::gd::instance_id(body);
    }

    ~Departing() {
        if (pair.authored != nullptr) {
            model::clear_node_overlay(pair.authored);
            if (pair.authored->get_parent() != nullptr) {
                pair.authored->get_parent()->remove_child(pair.authored);
            }
            memdelete(pair.authored);
        }
        away->get_parent()->remove_child(away);
        memdelete(away);
        memdelete(account);
    }

    RID binding() const {
        return server->get_bindings()->find_id(body_id);
    }

    double stored() {
        const Ref<NetwPromise> read = server->database_read(
            database,
            server->schema_of_declaration(schema),
            "hero"
        );
        const Dictionary row = read->get_result();
        if (!bool(row["found"])) {
            return -1.0;
        }
        return double(Dictionary(row["values"])["spin"]);
    }
};

TEST_CASE(
    "[Networked][Persistence][Entity][SceneTree] LC9 an entity moved to a new "
    "parent keeps its binding and writes nothing, and one freed in the tree "
    "writes the values it held as it left"
) {
    Departing stand;
    REQUIRE(stand.pair.route > 0);
    const RID binding = stand.server->persist_bind(stand.pair.authored);
    REQUIRE(binding.is_valid());
    stand.pair.authored->set_rotation(0.75);
    stand.connection->defer(true);

    stand.pair.authored->reparent(stand.away);
    stand.server->session_flush_deferred();

    NETW_CHECK_EQ(stand.connection->withheld_count(), 0);
    CHECK(bool(stand.binding() == binding));

    model::clear_node_overlay(stand.pair.authored);
    memdelete(stand.pair.authored);
    stand.pair.authored = nullptr;
    stand.server->session_flush_deferred();

    NETW_CHECK_EQ(stand.connection->withheld_count(), 1);
    stand.connection->release();
    stand.connection->defer(false);
    CHECK_FALSE(stand.binding().is_valid());
    CHECK(bool(stand.stored() == 0.75));
}

#endif

} // namespace TestPersistenceLifecycle

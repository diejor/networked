#include "support/netw_test.h"

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
#include "support/minted_script.h"
#endif

namespace TestPersistenceBinding {

using namespace godot;
using netw::NetwColumnRef;
using netw::NetwMultiplayer;
using netw::NetwPersistenceConfig;
using netw::NetwPromise;
using netw::NetwPropertyConfig;
using netw::NetwQuantize;
using netw::NetwSchema;
using netw::SessionCore;
using netw::persist::MemoryConnection;
namespace model = netw::script::model;
namespace schema_model = netw::schema_model;

struct World {
    Ref<NetwMultiplayer> session;
    Ref<NetwSchema> schema;
    Ref<MemoryConnection> connection;
    RID database;
    Node2D *root = nullptr;
    Node2D *limb = nullptr;
    Node *account = nullptr;

    World(const String &p_store) {
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
        session->get_databases()->open(
            database,
            "slot1",
            NetwPromise::resolved(connection)
        );

        root = memnew(Node2D);
        root->set_name("Player");
        limb = memnew(Node2D);
        limb->set_name("Turret");
        root->add_child(limb);
    }

    ~World() {
        if (root != nullptr) {
            model::clear_node_overlay(limb);
            model::clear_node_overlay(root);
            memdelete(root);
        }
        memdelete(account);
        schema_model::clear();
        netw::persist::forget_stores();
    }

    Ref<NetwPersistenceConfig> declare(double p_interval = 0.0) {
        const Ref<NetwPersistenceConfig> config
            = netw::Netw::configure_persistence(root);
        config->database(StringName("saves"))
            ->schema(schema)
            ->record_id(Callable(account, "get_name"))
            ->interval(p_interval);
        return config;
    }

    Ref<NetwPropertyConfig> property(Node *p_node, const StringName &p_name) {
        return model::configure_node_property(p_node, p_name);
    }

    void bind_both() {
        property(root, "position")->persisted(schema->column_ref(0));
        property(limb, "rotation")->persisted(schema->column_ref(1));
    }

    RID compile() {
        return session->persist_bind(root);
    }

    void become_replica() {
        session->session_plane().set_role(SessionCore::ROLE_CLIENT);
    }
};

Variant settled(const Ref<NetwPromise> &p_promise) {
    return p_promise.is_valid() ? p_promise->get_result() : Variant();
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB1 a binding compiles every column of "
    "the declared schema out of the entity's own nodes, and answers the "
    "database and the record id it captured"
) {
    World world("pb1");
    world.declare();
    world.bind_both();

    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    CHECK(bool(
        world.session->persist_get_record_id_binding(binding)
        == StringName("hero")
    ));
    CHECK(bool(world.session->persist_get_database(binding) == world.database));
    CHECK(world.session->persist_is_dirty_binding(binding));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB2 a column reference taken from "
    "another schema refuses the whole binding, even where its index names a "
    "column of the declared schema"
) {
    World world("pb2");
    world.declare();
    const Ref<NetwSchema> stranger = NetwSchema::create("monsters");
    stranger->vector2("where", Ref<NetwQuantize>(), 1);
    stranger->f32("spin", Ref<NetwQuantize>(), 1);

    world.property(world.root, "position")->persisted(stranger->column_ref(0));
    world.property(world.limb, "rotation")
        ->persisted(world.schema->column_ref(1));

    CHECK_FALSE(world.compile().is_valid());
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB3 a declared column that no property "
    "binds refuses the whole binding, so a saved row is never half a row"
) {
    World world("pb3");
    world.declare();
    world.property(world.root, "position")
        ->persisted(world.schema->column_ref(0));

    CHECK_FALSE(world.compile().is_valid());
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB4 two properties naming one column "
    "refuse the binding, including from sibling nodes, because one column "
    "holds one property"
) {
    World world("pb4");
    world.declare();
    world.property(world.root, "position")
        ->persisted(world.schema->column_ref(0));
    world.property(world.limb, "position")
        ->persisted(world.schema->column_ref(0));

    CHECK_FALSE(world.compile().is_valid());
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB5 a property a node does not carry, "
    "and a property whose value the column cannot store, each refuse the "
    "binding"
) {
    SUBCASE("a name no node carries") {
        World world("pb5a");
        world.declare();
        world.property(world.root, "position")
            ->persisted(world.schema->column_ref(0));
        world.property(world.limb, "no_such_property")
            ->persisted(world.schema->column_ref(1));

        CHECK_FALSE(world.compile().is_valid());
    }

    SUBCASE("a Vector2 offered to a float column") {
        World world("pb5b");
        world.declare();
        world.property(world.root, "position")
            ->persisted(world.schema->column_ref(0));
        world.property(world.limb, "position")
            ->persisted(world.schema->column_ref(1));

        CHECK_FALSE(world.compile().is_valid());
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB6 a binding needs a database this "
    "session declares and a record id the provider answers, and refuses "
    "rather than inventing either"
) {
    SUBCASE("a database no session declares") {
        World world("pb6a");
        world.declare()->database(StringName("elsewhere"));
        world.bind_both();

        CHECK_FALSE(world.compile().is_valid());
    }

    SUBCASE("no provider at all") {
        World world("pb6b");
        world.declare()->record_id(Callable());
        world.bind_both();

        CHECK_FALSE(world.compile().is_valid());
    }

    SUBCASE("a provider answering an empty id") {
        World world("pb6c");
        world.declare()->record_id(
            Callable(world.account, "get_scene_file_path")
        );
        world.bind_both();

        CHECK_FALSE(world.compile().is_valid());
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB7 the record id is captured once, so "
    "moving the account the provider reads never retargets an enrolled save"
) {
    World world("pb7");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());

    world.account->set_name("villain");

    CHECK(bool(
        world.session->persist_get_record_id_binding(binding)
        == StringName("hero")
    ));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB8 a saved entity that changed nothing "
    "submits no write, and the row it did save reads back"
) {
    World world("pb8");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(3, 4));

    CHECK(bool(settled(world.session->persist_save_binding(binding))));
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));

    const Ref<NetwPromise> again = world.session->persist_save_binding(binding);
    CHECK_FALSE(bool(settled(again)));

    const Dictionary stored = world.session
                                   ->database_read(
                                       world.database,
                                       world.session->schema_find("players"),
                                       "hero"
                                   )
                                   ->get_result();
    CHECK(bool(stored["found"]));
    CHECK(bool(
        Vector2(Dictionary(stored["values"])["where"]) == Vector2(3, 4)
    ));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB9 a load applies the stored row and "
    "leaves the binding clean, and a load that found nothing leaves the "
    "entity's own values standing and dirty"
) {
    World world("pb9");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.session->persist_save_binding(binding);
    Dictionary elsewhere;
    elsewhere["where"] = Vector2(9, 9);
    elsewhere["spin"] = 0.0;
    world.session->database_write(
        world.database,
        world.session->schema_find("players"),
        "hero",
        elsewhere
    );

    CHECK(bool(settled(world.session->persist_load_binding(binding))));
    CHECK(bool(world.root->get_position() == Vector2(9, 9)));
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));

    World absent("pb9b");
    absent.declare();
    absent.bind_both();
    absent.root->set_position(Vector2(5, 5));
    const RID missing = absent.compile();
    REQUIRE(missing.is_valid());

    const Ref<NetwPromise> miss = absent.session->persist_load_binding(missing);
    REQUIRE(miss->get_is_completed());
    CHECK_FALSE(bool(miss->get_result()));
    CHECK(bool(absent.root->get_position() == Vector2(5, 5)));
    CHECK(absent.session->persist_is_dirty_binding(missing));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB10 a load refuses a dirty binding, "
    "because reading the stored row over unsaved values discards them"
) {
    World world("pb10");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.session->persist_save_binding(binding);
    world.root->set_position(Vector2(1, 1));

    const Ref<NetwPromise> refused = world.session->persist_load_binding(binding);
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(int(refused->get_code()), int(ERR_BUSY));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB11 a save freezes its values at "
    "submission, so a property that moved while the write was in flight "
    "leaves the entity dirty after the acknowledgement"
) {
    World world("pb11");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(2, 2));
    world.session->persist_save_binding(binding);
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));

    world.connection->defer(true);
    world.root->set_position(Vector2(4, 4));
    const Ref<NetwPromise> writing = world.session->persist_save_binding(binding);
    CHECK_FALSE(writing->get_is_settled());

    world.root->set_position(Vector2(6, 6));
    world.connection->release();

    CHECK(writing->get_is_settled());
    CHECK(world.session->persist_is_dirty_binding(binding));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB12 a write the backend refused never "
    "becomes the saved baseline, so the entity stays dirty and the next save "
    "carries the same values again"
) {
    World world("pb12");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(7, 7));

    world.connection->fail_next(ERR_FILE_CANT_WRITE);
    const Ref<NetwPromise> refused = world.session->persist_save_binding(binding);
    CHECK(refused->get_is_failed());
    CHECK(world.session->persist_is_dirty_binding(binding));

    CHECK(bool(settled(world.session->persist_save_binding(binding))));
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB14 an interval saves the whole entity "
    "row on its own cadence, and an interval of zero saves only when the game "
    "asks"
) {
    World world("pb14");
    world.declare(1.0);
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(1, 2));

    world.session->persist_pump(0.5);
    CHECK(world.session->persist_is_dirty_binding(binding));
    world.session->persist_pump(0.6);
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));

    World quiet("pb14b");
    quiet.declare(0.0);
    quiet.bind_both();
    const RID never = quiet.compile();
    REQUIRE(never.is_valid());
    quiet.root->set_position(Vector2(1, 2));

    quiet.session->persist_pump(100.0);
    CHECK(quiet.session->persist_is_dirty_binding(never));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB15 a peer holding no session "
    "authority opens no automatic save, because a replica's values are the "
    "authority's own row arriving late"
) {
    World world("pb15");
    world.declare(1.0);
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(1, 2));
    world.become_replica();

    world.session->persist_pump(5.0);

    CHECK(world.session->persist_is_dirty_binding(binding));
    NETW_CHECK_EQ(
        int(settled(world.session->persist_flush_all())),
        int(ERR_UNAUTHORIZED)
    );
    CHECK(world.session->persist_is_dirty_binding(binding));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB15b an explicit save on a peer "
    "holding no session authority is refused too, because asking directly "
    "does not make a replicated view the row to store"
) {
    World world("pb15b");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(1, 2));
    world.become_replica();

    const Ref<NetwPromise> refused = world.session->persist_save_binding(binding);
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(refused->get_code(), ERR_UNAUTHORIZED);
    CHECK(world.session->persist_is_dirty_binding(binding));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PB16 flush_all batches every dirty "
    "entity of a database into one submission"
) {
    World world("pb16");
    world.declare();
    world.bind_both();
    const RID binding = world.compile();
    REQUIRE(binding.is_valid());
    world.root->set_position(Vector2(4, 5));

    NETW_CHECK_EQ(int(settled(world.session->persist_flush_all())), int(OK));
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));
    NETW_CHECK_EQ(int(settled(world.session->persist_flush_all())), int(OK));
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Persistence] PB17 a persistence declaration belongs to the "
    "node that made it, so two players running one script keep two record id "
    "providers"
) {
    const Ref<Script> shared = netw_test::minted_script(
        "extends Node\n"
        "var who := &\"nobody\"\n"
        "func answer() -> StringName:\n"
        "\treturn who\n"
    );
    REQUIRE(shared.is_valid());

    Node *first = Object::cast_to<Node>(shared->call("new"));
    Node *second = Object::cast_to<Node>(shared->call("new"));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    first->set("who", StringName("ada"));
    second->set("who", StringName("bob"));

    netw::Netw::configure_persistence(first)->record_id(
        Callable(first, "answer")
    );
    netw::Netw::configure_persistence(second)->record_id(
        Callable(second, "answer")
    );

    const Ref<NetwPersistenceConfig> left = model::get_persistence_config(first);
    const Ref<NetwPersistenceConfig> right
        = model::get_persistence_config(second);
    REQUIRE(left.is_valid());
    REQUIRE(right.is_valid());
    CHECK(bool(left != right));
    CHECK(bool(StringName(left->get_id_provider().call()) == StringName("ada")));
    CHECK(bool(
        StringName(right->get_id_provider().call()) == StringName("bob")
    ));

    model::clear_node_overlay(first);
    model::clear_node_overlay(second);
    memdelete(first);
    memdelete(second);
}

#endif

} // namespace TestPersistenceBinding

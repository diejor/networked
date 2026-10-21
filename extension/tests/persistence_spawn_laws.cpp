#include "support/fault_log.h"
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
#include "netw/api/entity.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence.hpp"
#include "support/loopback_rig.h"
#include "support/minted_script.h"
#include "support/value_flow_stand.h"
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
        schema->f64("spin", Ref<NetwQuantize>(), 1);

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
            ->table(schema->get_schema_name())
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
            ->persisted(StringName(p_column == 0 ? "where" : "spin"));
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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][table] Database '*' failed. Loading record '*' "
        "failed, so its entity is not published (the memory store "
        "was told to fail).",
        1
    );

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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][table] Database '*' failed. Loading record '*' "
        "failed, so its entity is not published (its bound "
        "properties changed while the row was being read, and the "
        "stored row would overwrite those changes).",
        1
    );

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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][table] Condition \"!gd::has_property(node, "
        "property)\" is true. Returning: RID(). '*' saves column '*'"
        " of table '*' from '*', but that node has no such "
        "property. Fix the property name.",
        1
    );

    World world("sl6");
    world.bind(world.root, "no_such_property", 0);
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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][table] Database '*' failed. Loading record '*' "
        "failed, so its entity is not published (a bound node went "
        "away while the stored row was applied).",
        1
    );

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

const char *SAVED_SOURCE = R"(extends Node2D

var seen := []
var where := Vector2(1, 1):
	set(value):
		where = value
		seen.append(value)


func _init() -> void:
	Netw.configure_property(self, &"where").on_spawn().persisted(&"where")
	Netw.configure_persistence(self).database(&"saves").table(&"players") \
			.record_id(account)


func account() -> StringName:
	return NetwEntity.of(self).name
)";

const Vector2 START(1, 1);
const Vector2 STORED(9, 9);

Node *build_saved(
    const Variant &p_name,
    const Variant &p_script,
    const Variant &p_loads_at_spawn
) {
    const Ref<Script> script = p_script;
    Node *made
        = Object::cast_to<Node>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    netw::Netw::configure_persistence(made)->load_on_spawn(
        bool(p_loads_at_spawn)
    );
    return made;
}

struct Saved {
    netw_test::LoopbackRig rig;
    NetwMultiplayer *server = nullptr;
    Ref<Script> script;
    Ref<MemoryConnection> connection;
    RID database;

    Saved(const String &p_store, bool p_loads_at_spawn = true) : rig(1) {
        schema_model::clear();
        netw::persist::forget_stores();
        rig.mount();
        netw_test::flow_clocks(rig, 30);
        server = rig.server();
        script = netw_test::minted_script(SAVED_SOURCE);
        REQUIRE(script.is_valid());
        database = server->get_databases()->create("saves");
        connection = MemoryConnection::opened(p_store, "slot1");
        server->get_databases()
            ->open(database, "slot1", NetwPromise::resolved(connection));
        const Callable build
            = callable_mp_static(&build_saved).bind(script, p_loads_at_spawn);
        Array types;
        types.push_back(int(Variant::STRING));
        rig.register_constructor(server, "saved", build, types);
        rig.register_constructor(rig.client(0), "saved", build, types);
        rig.join(0, StringName("hero"));
        rig.step_ticks(4);
    }

    ~Saved() {
        schema_model::clear();
        netw::persist::forget_stores();
    }

    Node2D *spawn(bool p_for_player) {
        const Ref<netw::NetwPlayer> hero = rig.player(0);
        REQUIRE(hero.is_valid());
        Array named;
        named.push_back(String("Body"));
        const RID entity = server->spawn_registered(
            StringName("saved"),
            named,
            p_for_player ? hero.ptr() : nullptr
        );
        Node2D *body = Object::cast_to<Node2D>(server->entity_get_node(entity));
        REQUIRE(body != nullptr);
        REQUIRE_FALSE(body->is_inside_tree());
        return body;
    }

    static Ref<netw::NetwPersistence> persistence(Node *p_body) {
        return netw::NetwEntity::of(p_body)->get_persistence();
    }

    void store(const StringName &p_id, const Vector2 &p_where) {
        Dictionary row;
        row["where"] = p_where;
        const Ref<NetwPromise> wrote = server->database_write(
            database,
            server->schema_find("players"),
            p_id,
            row
        );
        REQUIRE(wrote->get_is_completed());
    }

    Vector2 stored(const StringName &p_id) {
        const Ref<NetwPromise> read = server->database_read(
            database,
            server->schema_find("players"),
            p_id
        );
        const Dictionary row = read->get_result();
        if (!bool(row.get("found", false))) {
            return Vector2(-1, -1);
        }
        return Vector2(Dictionary(row["values"])["where"]);
    }

    void mount(Node *p_body) {
        rig.branch(-1)->add_child(p_body);
    }
};

TEST_CASE(
    "[Networked][Persistence][Spawn][SceneTree] SL9 a spawned node not yet in "
    "the tree loads its stored record, and answers false when none is stored"
) {
    struct Row {
        const char *label;
        bool for_player;
        bool stored;
    };
    const Row rows[] = {
        {"Netw.spawn, stored", false, true},
        {"Netw.spawn, not stored", false, false},
        {"Netw.spawn_player, stored", true, true},
        {"Netw.spawn_player, not stored", true, false},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(netw_row, row.label);
        CAPTURE(netw_row);
        Saved stand(String("sl9 ") + row.label);
        Node2D *body = stand.spawn(row.for_player);
        const Ref<netw::NetwPersistence> saved = Saved::persistence(body);
        const StringName id = saved->get_record_id();
        CHECK(id == netw::NetwEntity::of(body)->get_name());
        if (row.for_player) {
            CHECK(id == StringName("hero"));
        }
        if (row.stored) {
            stand.store(id, STORED);
        }

        const Ref<NetwPromise> loading = saved->load();

        REQUIRE(loading->get_is_completed());
        CHECK(bool(loading->get_result()) == row.stored);
        const Vector2 held = body->get("where");
        CHECK(bool(held == (row.stored ? STORED : START)));
        stand.mount(body);
        stand.rig.step_ticks(4);
    }
}

TEST_CASE(
    "[Networked][Persistence][Spawn][SceneTree] SL10 a node mounted after its "
    "load reaches the client with the stored values, and mounting neither "
    "loads again nor binds the client"
) {
    struct Row {
        const char *label;
        bool loads_at_spawn;
        bool in_flight;
    };
    const Row rows[] = {
        {"loaded, load_on_spawn", true, false},
        {"loaded, no load_on_spawn", false, false},
        {"in flight, load_on_spawn", true, true},
        {"in flight, no load_on_spawn", false, true},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(netw_row, row.label);
        CAPTURE(netw_row);
        Saved stand(String("sl10 ") + row.label, row.loads_at_spawn);
        Node2D *body = stand.spawn(true);
        const Ref<netw::NetwPersistence> saved = Saved::persistence(body);
        CallLog log;
        saved->connect("loaded", log.callable("loaded"));
        stand.store(saved->get_record_id(), STORED);
        stand.connection->defer(row.in_flight);
        const Ref<NetwPromise> loading = saved->load();
        const int route = int(netw::NetwEntity::of(body)->get_route());

        stand.mount(body);
        stand.rig.step_ticks(4);
        if (row.in_flight) {
            CHECK(stand.rig.route_node(route, 0) == nullptr);
            stand.connection->release();
            stand.connection->defer(false);
            stand.rig.step_ticks(4);
        }

        CHECK(loading->get_is_completed());
        NETW_CHECK_EQ(log.count("loaded"), 1);
        CHECK(bool(Vector2(body->get("where")) == STORED));
        Node *view = stand.rig.route_node(route, 0);
        REQUIRE(view != nullptr);
        const Array seen = view->get("seen");
        CHECK_FALSE(seen.has(START));
        CHECK(bool(Vector2(view->get("where")) == STORED));
        CHECK(stand.rig.client(0)->get_bindings()->enrolled().is_empty());
    }
}

TEST_CASE(
    "[Networked][Persistence][Spawn][SceneTree] SL11 a spawned node freed "
    "before it is added to the tree leaves no binding, writes nothing and "
    "reports nothing"
) {
    struct Row {
        const char *label;
        bool loads;
        bool changes;
        bool in_flight;
    };
    const Row rows[] = {
        {"never loaded", false, false, false},
        {"loaded", true, false, false},
        {"loaded and changed", true, true, false},
        {"freed while loading", true, false, true},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(netw_row, row.label);
        CAPTURE(netw_row);
        netw_test::FaultWatch watch;
        Saved stand(String("sl11 ") + row.label);
        Node2D *body = stand.spawn(true);
        const StringName id = Saved::persistence(body)->get_record_id();
        stand.store(id, STORED);
        stand.connection->defer(row.in_flight);
        CallLog log;
        if (row.loads) {
            Saved::persistence(body)->load()->when_settled(
                log.callable("settled")
            );
        }
        if (row.changes) {
            body->set("where", Vector2(2, 2));
        }

        memdelete(body);
        stand.connection->release();
        stand.connection->defer(false);
        stand.rig.step_ticks(4);
        stand.server->persist_flush_all();

        CHECK(stand.server->get_bindings()->enrolled().is_empty());
        CHECK(bool(stand.stored(id) == STORED));
        NETW_CHECK_EQ(watch.errors(), 0);
    }
}

#endif

} // namespace TestPersistenceSpawn

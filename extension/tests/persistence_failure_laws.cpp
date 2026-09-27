#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/database.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/persistence_handle.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/session_handle.hpp"
#include "netw/persist/binding.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/session_core.hpp"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)
#include "support/minted_script.h"
#endif

namespace TestPersistenceFailure {

using namespace godot;
using netw::NetwDatabase;
using netw::NetwMultiplayer;
using netw::NetwPersistenceHandle;
using netw::NetwPromise;
using netw::NetwQuantize;
using netw::NetwSchema;
using netw::NetwSessionHandle;
using netw::SessionCore;
using netw::persist::MemoryConnection;
using netw_test::CallLog;
namespace model = netw::script::model;
namespace schema_model = netw::schema_model;

struct World {
    CallLog log;
    Ref<NetwMultiplayer> session;
    Ref<NetwSchema> schema;
    Ref<MemoryConnection> connection;
    Ref<NetwDatabase> database;
    Ref<NetwPersistenceHandle> row;
    Node2D *root = nullptr;
    Node *account = nullptr;
    ObjectID root_id;

    explicit World(const String &p_store, const Ref<Script> &p_script = {}) {
        schema_model::clear();
        netw::persist::forget_stores();
        if (p_script.is_valid()) {
            session = Ref<NetwMultiplayer>(
                Object::cast_to<NetwMultiplayer>(p_script->call("new"))
            );
        } else {
            session.instantiate();
        }
        account = memnew(Node);
        account->set_name("hero");
        schema = NetwSchema::create("players");
        schema->replicated(false);
        schema->vector2("where", Ref<NetwQuantize>(), 1);

        const RID made = session->get_databases()->create("saves");
        connection = MemoryConnection::opened(p_store, "slot1");
        session->get_databases()
            ->open(made, "slot1", NetwPromise::resolved(connection));
        database = session->database_handle(made);
        database->connect("failed", log.callable("failed"));

        root = memnew(Node2D);
        root->set_position(Vector2(1, 1));
        root_id = netw::gd::instance_id(root);
        netw::Netw::configure_persistence(root)
            ->database(StringName("saves"))
            ->schema(schema)
            ->record_id(Callable(account, "get_name"))
            ->interval(1.0);
        model::configure_node_property(root, "position")
            ->persisted(schema->column_ref(0));

        row = netw::NetwEntity::resolve(root)->get_persistence();
        row->connect("saved", log.callable("saved"));
        row->connect("loaded", log.callable("loaded"));
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
        model::clear_node_overlay(root);
        memdelete(root);
        root = nullptr;
    }

    RID enroll(bool p_load) {
        netw::Netw::configure_persistence(root)->load_on_spawn(p_load);
        session->persist_enroll(root);
        const RID made = binding();
        REQUIRE(made.is_valid());
        return made;
    }

    RID binding() const {
        return session->get_bindings()->find_id(root_id);
    }

    int failed_code() const {
        const Array carried = log.args("failed");
        return carried.is_empty() ? -1 : int(carried[0]);
    }
};

TEST_CASE(
    "[Networked][Persistence][Hosted] PF1 a refused entity write emits the "
    "database's failed signal once and no saved signal, whichever path "
    "submitted it, and a write that settles stale emits nothing"
) {
    SUBCASE("the interval") {
        World world("pf1a");
        world.enroll(false);
        world.root->set_position(Vector2(2, 2));
        world.connection->fail_next(ERR_FILE_CANT_WRITE);

        world.session->persist_pump(1.5);

        NETW_CHECK_EQ(world.log.count("failed"), 1);
        NETW_CHECK_EQ(world.failed_code(), int(ERR_FILE_CANT_WRITE));
        NETW_CHECK_EQ(world.log.count("saved"), 0);
    }

    SUBCASE("an explicit save") {
        World world("pf1b");
        const RID binding = world.enroll(false);
        world.root->set_position(Vector2(2, 2));
        world.connection->fail_next(ERR_FILE_CANT_WRITE);

        const Ref<NetwPromise> refused
            = world.session->persist_save_binding(binding);

        CHECK(refused->get_is_failed());
        NETW_CHECK_EQ(world.log.count("failed"), 1);
        NETW_CHECK_EQ(world.log.count("saved"), 0);
    }

    SUBCASE("the final write of a departed entity") {
        World world("pf1c");
        const RID binding = world.enroll(false);
        world.root->set_position(Vector2(2, 2));
        world.session->persist_capture_exit(world.root);
        world.free_root();
        world.connection->fail_next(ERR_FILE_CANT_WRITE);

        world.session->persist_settle_departure(world.root_id, true);

        CHECK(world.session->get_bindings()->is_valid(binding));
        NETW_CHECK_EQ(world.log.count("failed"), 1);
        NETW_CHECK_EQ(world.log.count("saved"), 0);
    }

    SUBCASE("a write whose authority moved while it was out") {
        World world("pf1d");
        const RID binding = world.enroll(false);
        world.root->set_position(Vector2(2, 2));
        world.connection->defer(true);
        const Ref<NetwPromise> saving
            = world.session->persist_save_binding(binding);

        world.session->session_plane().set_role(SessionCore::ROLE_CLIENT);
        world.session->session_plane().set_role(SessionCore::ROLE_NONE);
        world.connection->release();

        CHECK(saving->get_is_failed());
        NETW_CHECK_EQ(world.log.count("failed"), 0);
        NETW_CHECK_EQ(world.log.count("saved"), 0);
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PF2 a refused load emits the database's "
    "failed "
    "signal and no loaded signal, and a refused spawn load keeps the entity "
    "withheld"
) {
    SUBCASE("the spawn load") {
        World world("pf2a");
        world.connection->fail_next(ERR_FILE_CANT_READ);

        world.enroll(true);

        NETW_CHECK_EQ(world.log.count("failed"), 1);
        NETW_CHECK_EQ(world.failed_code(), int(ERR_FILE_CANT_READ));
        CHECK(world.session->persist_withholds(world.root));
    }

    SUBCASE("an explicit load") {
        World world("pf2b");
        const RID binding = world.enroll(false);
        world.connection->fail_next(ERR_FILE_CANT_READ);

        const Ref<NetwPromise> refused
            = world.session->persist_load_binding(binding);

        CHECK(refused->get_is_failed());
        NETW_CHECK_EQ(world.log.count("failed"), 1);
        NETW_CHECK_EQ(world.log.count("loaded"), 0);
    }
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PF3 save_entities waits for writes "
    "already in "
    "flight, answers the first error while a row stays unsaved, and answers "
    "OK once every row is stored"
) {
    World world("pf3");
    const RID binding = world.enroll(false);
    Ref<NetwSessionHandle> handle;
    handle.instantiate();
    handle->bind_session(world.session.ptr());

    world.root->set_position(Vector2(2, 2));
    world.connection->fail_next(ERR_FILE_CANT_WRITE);
    const Ref<NetwPromise> refused = handle->save_entities();
    CHECK(refused->get_is_completed());
    NETW_CHECK_EQ(int(refused->get_result()), int(ERR_FILE_CANT_WRITE));
    CHECK(world.session->persist_is_dirty_binding(binding));

    world.connection->defer(true);
    world.session->persist_pump(1.5);
    const Ref<NetwPromise> waiting = handle->save_entities();
    CHECK_FALSE(waiting->get_is_completed());
    world.connection->release();
    world.connection->defer(false);

    CHECK(waiting->get_is_completed());
    NETW_CHECK_EQ(int(waiting->get_result()), int(OK));
    CHECK_FALSE(world.session->persist_is_dirty_binding(binding));

    world.session->session_plane().set_role(SessionCore::ROLE_CLIENT);
    NETW_CHECK_EQ(
        int(handle->save_entities()->get_result()),
        int(ERR_UNAUTHORIZED)
    );
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PF5 a save whose row the database left "
    "uncertain fails its promise and leaves the entity dirty"
) {
    World world("pf5");
    const RID binding = world.enroll(false);
    world.root->set_position(Vector2(2, 2));
    world.connection->doubt_next(OK);

    const Ref<NetwPromise> saving
        = world.session->persist_save_binding(binding);

    CHECK(saving->get_is_failed());
    NETW_CHECK_EQ(saving->get_code(), ERR_UNAVAILABLE);
    CHECK(world.session->persist_is_dirty_binding(binding));
    NETW_CHECK_EQ(world.log.count("failed"), 1);
    NETW_CHECK_EQ(world.log.count("saved"), 0);
}

#if defined(NETW_TIER_HOSTED)

const char *TICK_SEAM = R"(extends NetwMultiplayer

var ticks := 0
var seen := 0.0
var calls_through := false

func _persist_tick(delta: float) -> void:
	ticks += 1
	seen += delta
	if calls_through:
		persist_tick_default(delta)
)";

TEST_CASE(
    "[Networked][Persistence] PF4 an override of _persist_tick replaces the "
    "interval pass, and persist_tick_default inside it runs the stock pass"
) {
    const Ref<Script> seam = netw_test::minted_script(TICK_SEAM);
    REQUIRE(seam.is_valid());

    SUBCASE("an override that does not call through") {
        World world("pf4a", seam);
        const RID binding = world.enroll(false);
        world.root->set_position(Vector2(2, 2));

        world.session->persist_pump(1.5);

        NETW_CHECK_EQ(int(world.session->get("ticks")), 1);
        CHECK(bool(double(world.session->get("seen")) == 1.5));
        CHECK(world.session->persist_is_dirty_binding(binding));
    }

    SUBCASE("an override that calls through") {
        World world("pf4b", seam);
        const RID binding = world.enroll(false);
        world.session->set("calls_through", true);
        world.root->set_position(Vector2(2, 2));

        world.session->persist_pump(1.5);

        NETW_CHECK_EQ(int(world.session->get("ticks")), 1);
        CHECK_FALSE(world.session->persist_is_dirty_binding(binding));
        NETW_CHECK_EQ(world.log.count("saved"), 1);
    }
}

#endif

} // namespace TestPersistenceFailure

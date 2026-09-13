#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/persistence_stand.h"
#include "support/value_flow_stand.h"

#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/api/promise.hpp"

namespace TestPersistenceSessionLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPersistenceEngine;
using netw::NetwPromise;

constexpr double TINY_INTERVAL = 1e-6;

constexpr const char *STATE_BODY = netw_test::gdsrc::STATE_AND_INPUT;

void declare_nothing(Object *, const StringName &, const Array &) {
}

Array position_column(double p_interval) {
    Dictionary entry;
    entry["property"] = StringName("position");
    entry["interval"] = p_interval;
    Array declared;
    declared.push_back(entry);
    return declared;
}

Array position_columns() {
    Array declared;
    declared.push_back(StringName("position"));
    return declared;
}

struct Saved {
    Ref<NetwMultiplayer> session;
    Node2D *owner = nullptr;
    netw_test::DatabaseStand database;
    RID entity;

    explicit Saved(double p_interval = 0.0) {
        session.instantiate();
        database.declare(StringName("players"), position_columns());
        NetwPersistenceEngine::forget_claims();
        NetwPersistenceEngine::set_schema_declarer(
            callable_mp_static(&declare_nothing)
        );
        owner = memnew(Node2D);
        owner->set_name("Saved");
        owner->set_meta(
            NetwPersistenceEngine::meta_columns(),
            position_column(p_interval)
        );
        owner->set_meta(NetwPersistenceEngine::meta_database(), database.db);
        owner->set_meta(
            NetwPersistenceEngine::meta_table(),
            StringName("players")
        );
        NetwEntity::ensure(owner);
        entity = session->entity_of(owner);
        REQUIRE(entity.is_valid());
    }

    Ref<NetwPersistenceEngine> engine() const {
        return session->persistence_engine_for(NetwEntity::of(owner).ptr());
    }

    Dictionary last_upsert() const {
        const Array rows = database.upserts();
        NETW_CHECK_EQ(int(rows.size() > 0), 1);
        if (rows.is_empty()) {
            return Dictionary();
        }
        return rows[rows.size() - 1];
    }

    ~Saved() {
        NetwPersistenceEngine::set_schema_declarer(Callable());
        NetwPersistenceEngine::forget_claims();
        if (owner == nullptr) {
            return;
        }
        if (owner->get_parent() != nullptr) {
            owner->get_parent()->remove_child(owner);
        }
        memdelete(owner);
    }
};

TEST_CASE(
    "[Networked][Database][Session] SV1 a node that declares persistence "
    "answers a live engine through its entity, columns and database "
    "resolved from the declaration"
) {
    Saved saved;

    const Ref<NetwPersistenceEngine> engine = saved.engine();
    NETW_CHECK_EQ(int(engine.is_valid()), 1);
    if (engine.is_null()) {
        return;
    }
    CHECK_FALSE(engine->columns_empty());
    NETW_CHECK_EQ(int(engine->database() == saved.database.db), 1);
    NETW_CHECK_EQ(int(engine.ptr() == saved.engine().ptr()), 1);
}

TEST_CASE(
    "[Networked][Database][Session][SceneTree] SV6 a persisted owner "
    "that is moving keeps its engine and writes nothing, and one that "
    "departs writes what its body last held, because the values were "
    "read while the body was still there to read"
) {
    LoopbackRig rig(0);
    rig.mount();
    netw_test::DatabaseStand database;
    database.declare(StringName("players"), position_columns());
    NetwPersistenceEngine::forget_claims();
    NetwPersistenceEngine::set_schema_declarer(
        callable_mp_static(&declare_nothing)
    );

    Node2D *body = memnew(Node2D);
    body->set_name("Saved");
    body->set_meta(NetwPersistenceEngine::meta_columns(), position_column(0.0));
    body->set_meta(NetwPersistenceEngine::meta_database(), database.db);
    body->set_meta(NetwPersistenceEngine::meta_table(), StringName("players"));
    NetwEntity::ensure(body);
    rig.branch(-1)->add_child(body);

    NetwMultiplayer *server = flow_core(rig.server());
    const Ref<NetwEntity> held = NetwEntity::of(body);
    REQUIRE(held.is_valid());
    REQUIRE(server->persistence_engine_for(held.ptr()).is_valid());

    SUBCASE("a move writes nothing and keeps the engine") {
        body->set_position(Vector2(11, 22));
        const int before = database.upserts().size();

        Node *from = body->get_parent();
        from->remove_child(body);
        from->add_child(body);
        server->session_flush_deferred();

        NETW_CHECK_EQ(database.upserts().size(), before);
        CHECK(server->persistence_engine_for(held.ptr()).is_valid());
        body->get_parent()->remove_child(body);
        memdelete(body);
    }

    SUBCASE("a departure writes the value the body held as it left") {
        body->set_position(Vector2(33, 44));

        memdelete(body);
        server->session_flush_deferred();

        const Array rows = database.upserts();
        NETW_CHECK_EQ(int(rows.size() > 0), 1);
        if (rows.size() > 0) {
            const Dictionary row = rows[rows.size() - 1];
            const Dictionary values = row["data"];
            NETW_CHECK_EQ(
                int(Vector2(values[StringName("position")]) == Vector2(33, 44)),
                1
            );
        }
    }

    NetwPersistenceEngine::set_schema_declarer(Callable());
    NetwPersistenceEngine::forget_claims();
}

TEST_CASE(
    "[Networked][Database][Session] SV2 the session's persistence verbs "
    "carry the live scene value out to the database and back onto it"
) {
    Saved saved;
    saved.owner->set_position(Vector2(10, 20));

    const Ref<NetwPromise> flushed
        = saved.session->persist_flush(saved.entity, Array());
    REQUIRE(flushed.is_valid());
    REQUIRE(flushed->get_is_settled());
    NETW_CHECK_EQ(int(flushed->get_is_failed()), 0);

    const Dictionary row = saved.last_upsert();
    const Dictionary values = row["data"];
    NETW_CHECK_EQ(
        int(Vector2(values[StringName("position")]) == Vector2(10, 20)),
        1
    );

    saved.database.set_stored(values);
    saved.owner->set_position(Vector2(0, 0));
    const Ref<NetwPromise> hydrated
        = saved.session->persist_hydrate(saved.entity);
    REQUIRE(hydrated.is_valid());
    NETW_CHECK_EQ(int(saved.owner->get_position() == Vector2(10, 20)), 1);
}

TEST_CASE(
    "[Networked][Database][Session] SV5 every poll drains what the live "
    "scene changed, so a session that only polls keeps saving"
) {
    Saved saved(TINY_INTERVAL);
    REQUIRE(saved.engine().is_valid());

    saved.session->poll();
    NETW_CHECK_EQ(saved.database.transaction_count(), 0);

    for (int at = 1; at <= 3; ++at) {
        saved.owner->set_position(Vector2(at, at));
        saved.session->poll();
        NETW_CHECK_EQ(saved.database.transaction_count(), at);
    }

    saved.session->poll();
    NETW_CHECK_EQ(saved.database.transaction_count(), 3);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] SV3 a property a derived state set "
    "governs is seen as governed, and one nothing declares is not"
) {
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30);
    const FlowPair pair = stand_flow_pair(rig, STATE_BODY, "Governed");

    NetwMultiplayer *server = flow_core(rig.server());
    const Ref<NetwEntity> entity = NetwEntity::of(pair.authored);
    REQUIRE(entity.is_valid());

    const NodePath governed
        = entity->property_path(pair.authored, StringName("position"), nullptr);
    const NodePath clean
        = entity->property_path(pair.authored, StringName("skew"), nullptr);
    NETW_CHECK_EQ(int(governed.is_empty()), 0);

    NETW_CHECK_EQ(
        int(server->entity_governs_property(
            pair.authored,
            governed,
            nullptr,
            pair.route
        )),
        1
    );
    NETW_CHECK_EQ(
        int(server->entity_governs_property(
            pair.authored,
            clean,
            nullptr,
            pair.route
        )),
        0
    );
}

TEST_CASE(
    "[Networked][Sync][SceneTree] SV4 governance is read from the live "
    "tree, so it survives the body being reparented"
) {
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30);
    const FlowPair pair = stand_flow_pair(rig, STATE_BODY, "Reparented");

    NetwMultiplayer *server = flow_core(rig.server());
    const Ref<NetwEntity> entity = NetwEntity::of(pair.authored);
    const NodePath governed
        = entity->property_path(pair.authored, StringName("position"), nullptr);

    Node *elsewhere = memnew(Node);
    elsewhere->set_name("Elsewhere");
    rig.branch(-1)->add_child(elsewhere);
    pair.authored->get_parent()->remove_child(pair.authored);
    elsewhere->add_child(pair.authored);
    rig.pump();

    const NodePath rewalked
        = entity->property_path(pair.authored, StringName("position"), nullptr);
    NETW_CHECK_EQ(int(rewalked == governed), 1);
    NETW_CHECK_EQ(
        int(server->entity_governs_property(
            pair.authored,
            rewalked,
            nullptr,
            pair.route
        )),
        1
    );
}

} // namespace TestPersistenceSessionLaws

#endif

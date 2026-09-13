#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/netw_call_log.h"
#include "support/persistence_stand.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_engine.hpp"
#include "netw/interest/decl.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestEntityDepartureLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw::NetwPersistenceEngine;
using netw::interest::Decl;

constexpr const char *MOVING_BODY = netw_test::gdsrc::STATE_AND_INPUT;

struct Travelling {
    LoopbackRig rig;
    NetwMultiplayer *server = nullptr;
    Node *away = nullptr;
    FlowPair pair;

    Travelling() : rig(1) {
        rig.mount();
        flow_clocks(rig, 30);
        pair = stand_flow_pair(rig, MOVING_BODY, "Traveller");
        server = flow_core(rig.server());
        away = memnew(Node);
        away->set_name("Away");
        rig.branch(-1)->add_child(away);
    }

    ~Travelling() {
        away->get_parent()->remove_child(away);
        memdelete(away);
    }

    Node *body() const {
        return pair.authored;
    }

    void discard_body() {
        if (pair.authored == nullptr) {
            return;
        }
        if (pair.authored->get_parent() != nullptr) {
            pair.authored->get_parent()->remove_child(pair.authored);
        }
        memdelete(pair.authored);
        pair.authored = nullptr;
    }

    int64_t state() const {
        return server->liveness_route_state(pair.route);
    }

    void settle() {
        server->session_flush_deferred();
    }
};

TEST_CASE(
    "[Networked][Entity][Departure][SceneTree] ED1 a departure is read off "
    "the settled tree and never off the exit, so every way of leaving a "
    "parent waits for the same answer"
) {
    Travelling stand;
    REQUIRE(stand.pair.route > 0);
    NETW_CHECK_EQ(stand.state(), int64_t(NetwMultiplayer::ENTITY_STATE_LIVE));

    SUBCASE("Node.reparent lands the owner again, so the route survives") {
        stand.body()->reparent(stand.away);
        stand.settle();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
        const bool landed = stand.body()->get_parent() == stand.away;
        CHECK(landed);
    }

    SUBCASE("a bare remove and add is the same move") {
        Node *from = stand.body()->get_parent();
        from->remove_child(stand.body());
        stand.away->add_child(stand.body());
        stand.settle();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
    }

    SUBCASE("several moves in one interval answer for the final tree") {
        Node *from = stand.body()->get_parent();
        stand.body()->reparent(stand.away);
        stand.body()->reparent(from);
        stand.settle();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
        const bool landed = stand.body()->get_parent() == from;
        CHECK(landed);
    }

    SUBCASE("an owner left detached across the settle has ended") {
        stand.body()->get_parent()->remove_child(stand.body());
        stand.settle();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_DEAD)
        );
        stand.discard_body();
    }

    SUBCASE("a route no spawn owns still waits for the settle to answer") {
        Node *from = stand.body()->get_parent();
        from->remove_child(stand.body());

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );

        stand.away->add_child(stand.body());
        stand.settle();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
    }
}

struct Watched : Travelling {
    CallLog seen;
    Ref<NetwEntity> entity;

    Watched() {
        entity = NetwEntity::of(body());
        REQUIRE(entity.is_valid());
        entity->connect(StringName("despawned"), seen.callable("despawned"));
        entity->connect(StringName("despawning"), seen.callable("despawning"));
        entity->connect(StringName("reparented"), seen.callable("reparented"));
    }
};

TEST_CASE(
    "[Networked][Entity][Departure][SceneTree] ED2 a death completes exactly "
    "once however it was reached, and a preparation signal is only ever owed "
    "by a despawn a caller declared"
) {
    Watched stand;

    SUBCASE("an undeclared removal completes without a preparation signal") {
        stand.body()->get_parent()->remove_child(stand.body());
        stand.settle();

        NETW_CHECK_EQ(stand.seen.count("despawned"), 1);
        NETW_CHECK_EQ(stand.seen.count("despawning"), 0);
        NETW_CHECK_EQ(stand.seen.count("reparented"), 0);
        NETW_CHECK_EQ(
            stand.entity->get_stage(),
            int64_t(netw::entity::Stage::FREED)
        );
        stand.discard_body();
    }

    SUBCASE("a body freed out from under the session still completes") {
        memdelete(stand.pair.authored);
        stand.pair.authored = nullptr;
        stand.settle();

        NETW_CHECK_EQ(stand.seen.count("despawned"), 1);
        NETW_CHECK_EQ(stand.seen.count("reparented"), 0);
    }

    SUBCASE("a declared despawn cannot be taken back by landing again") {
        Ref<netw::NetwDespawnOpts> opts;
        opts.instantiate();
        stand.entity->despawn(opts);
        stand.body()->reparent(stand.away);
        stand.settle();

        NETW_CHECK_EQ(stand.seen.count("despawned"), 1);
        NETW_CHECK_EQ(stand.seen.count("reparented"), 0);
        stand.pair.authored = nullptr;
    }

    SUBCASE("a declared despawn prepares once and completes once") {
        Ref<netw::NetwDespawnOpts> opts;
        opts.instantiate();
        stand.entity->despawn(opts);
        stand.body()->get_parent()->remove_child(stand.body());
        stand.settle();

        NETW_CHECK_EQ(stand.seen.count("despawning"), 1);
        NETW_CHECK_EQ(stand.seen.count("despawned"), 1);
        NETW_CHECK_EQ(stand.seen.count("reparented"), 0);
        stand.pair.authored = nullptr;
    }

    SUBCASE("a move completes no death at all") {
        stand.body()->reparent(stand.away);
        stand.settle();

        NETW_CHECK_EQ(stand.seen.count("despawned"), 0);
        NETW_CHECK_EQ(stand.seen.count("despawning"), 0);
    }
}

constexpr const char *WATCHED_ID = "watched_body";
constexpr const char *SAVED_TABLE = "players";
constexpr const char *GATE_LAYER = "gate";

DatabaseStand *client_database = nullptr;

void declare_no_schema(Object *, const StringName &, const Array &) {
}

Array position_columns() {
    Array out;
    out.push_back(StringName("position"));
    return out;
}

Array position_column() {
    Dictionary entry;
    entry["property"] = StringName("position");
    entry["interval"] = 0.0;
    Array out;
    out.push_back(entry);
    return out;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_string() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Node *build_saved_body(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    if (client_database != nullptr) {
        made->set_meta(
            NetwPersistenceEngine::meta_columns(),
            position_column()
        );
        made->set_meta(
            NetwPersistenceEngine::meta_database(),
            client_database->db
        );
        made->set_meta(
            NetwPersistenceEngine::meta_table(),
            StringName(SAVED_TABLE)
        );
    }
    return made;
}

struct Watching {
    LoopbackRig rig;
    DatabaseStand database;
    Node *arena = nullptr;
    Ref<NetwInterestLayer> gate;
    int64_t watcher = 0;
    int route = 0;

    Watching() : rig(1) {
        rig.mount();
        database.declare(StringName(SAVED_TABLE), position_columns());
        NetwPersistenceEngine::forget_claims();
        NetwPersistenceEngine::set_schema_declarer(
            callable_mp_static(&declare_no_schema)
        );

        arena = rig.mirror_child("Arena");
        route = rig.spawn_registered(
            StringName(WATCHED_ID),
            callable_mp_static(&build_saved_body),
            named("Watched"),
            one_string(),
            arena,
            Variant(),
            false
        );
        client_database = &database;
        gate = rig.server()->interest_layer(StringName(GATE_LAYER));
        REQUIRE(gate.is_valid());
        gate->set_default_leave_policy(Decl::LEAVE_HIDE);
        gate->add_entity(NetwEntity::of(rig.route_node(route)));
        watcher = rig.peer_id(0);
        show();
        REQUIRE(held() != nullptr);
    }

    ~Watching() {
        client_database = nullptr;
        NetwPersistenceEngine::set_schema_declarer(Callable());
        NetwPersistenceEngine::forget_claims();
    }

    NetwMultiplayer *client() const {
        return rig.client(0);
    }

    Node *held() const {
        return rig.route_node(route, 0);
    }

    Ref<NetwEntity> wrapper() const {
        return client()->wrapper_for_route(route);
    }

    RID entity() const {
        return client()->entity_from_route(route);
    }

    int64_t state() const {
        return client()->liveness_route_state(route);
    }

    Ref<NetwPersistenceEngine> saved() const {
        const Ref<NetwEntity> held_view = wrapper();
        return held_view.is_valid()
            ? client()->persistence_engine_for(held_view.ptr())
            : Ref<NetwPersistenceEngine>();
    }

    Ref<NetwInterestLayer> local() const {
        return client()->interest_layer(StringName(GATE_LAYER));
    }

    void show() {
        gate->add_viewer(watcher);
        rig.flush_interest();
        rig.pump(10);
    }

    void conceal() {
        gate->remove_viewer(watcher);
        rig.flush_interest();
        rig.pump(10);
    }
};

TEST_CASE(
    "[Networked][Entity][Departure][SceneTree] ED3 a hide lets go of every "
    "book the lost body was in and writes none of them out, while the "
    "identity, its route and the server's own view of it all stand"
) {
    Watching stand;
    const RID standing = stand.entity();
    REQUIRE(standing.is_valid());
    const Ref<NetwEntity> body = stand.wrapper();
    REQUIRE(body.is_valid());
    const Ref<NetwPersistenceEngine> first = stand.saved();
    REQUIRE(first.is_valid());
    CHECK(stand.local()->has_entity(body));
    const int written = stand.database.upserts().size();

    stand.conceal();

    NETW_CHECK_EQ(stand.state(), int64_t(NetwMultiplayer::ENTITY_STATE_ABSENT));
    CHECK(stand.held() == nullptr);
    CHECK_FALSE(stand.local()->has_entity(body));
    NETW_CHECK_EQ(stand.database.upserts().size(), written);

    SUBCASE("the route still names the entity it named while it was here") {
        CHECK(stand.entity() == standing);
    }

    SUBCASE("the server keeps the entity and the seat it admitted") {
        NETW_CHECK_EQ(
            stand.rig.server()->liveness_route_state(stand.route),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
        CHECK(stand.rig.route_node(stand.route) != nullptr);
    }

    SUBCASE("readmission seats a fresh body on the standing identity") {
        stand.show();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
        CHECK(stand.entity() == standing);
        CHECK(stand.held() != nullptr);
        const Ref<NetwPersistenceEngine> again = stand.saved();
        NETW_CHECK_EQ(int(again.is_valid()), 1);
        NETW_CHECK_EQ(int(again != first), 1);
    }
}

TEST_CASE(
    "[Networked][Entity][Departure][SceneTree] ED4 a receiving peer that "
    "deletes its own copy has lost a body rather than learned of a death, "
    "so the identity waits absent for the next admission"
) {
    Watching stand;
    const RID standing = stand.entity();
    const Ref<NetwEntity> body = stand.wrapper();
    REQUIRE(body.is_valid());
    Node *copy = stand.held();
    REQUIRE(copy != nullptr);

    memdelete(copy);
    stand.client()->session_flush_deferred();

    NETW_CHECK_EQ(stand.state(), int64_t(NetwMultiplayer::ENTITY_STATE_ABSENT));
    CHECK(stand.entity() == standing);
    CHECK_FALSE(stand.local()->has_entity(body));
    NETW_CHECK_EQ(stand.database.upserts().size(), 0);

    SUBCASE("the server never heard a death it did not author") {
        NETW_CHECK_EQ(
            stand.rig.server()->liveness_route_state(stand.route),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
    }

    SUBCASE("the next admission is not refused as a body already held") {
        stand.conceal();
        stand.show();

        NETW_CHECK_EQ(
            stand.state(),
            int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
        );
        CHECK(stand.held() != nullptr);
        CHECK(stand.entity() == standing);
    }
}

} // namespace TestEntityDepartureLaws

#endif

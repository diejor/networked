#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/wire/registry.hpp"
#include "support/minted_script.h"
#include "support/netw_call_log.h"
#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

namespace TestLifecycleDespawnLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::CallLog;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int SETTLE_TICKS = 24;

const char *CRATE_SOURCE = R"(extends Node3D

var hp := 0


func _init() -> void:
	Netw.configure_property(self, &"hp").broadcast().on_spawn()
)";

Node *build_crate(
    const Variant &p_name,
    const Variant &p_script,
    const Variant &p_spawned
) {
    const Ref<Script> script = p_script;
    Node3D *made
        = Object::cast_to<Node3D>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    made->set_position(Vector3(2.0, 0.0, 1.0));
    const Ref<NetwEntity> entity = NetwEntity::ensure(made);
    entity->set_lifecycle(NetwEntity::LIFECYCLE_CONTROLLER);
    entity->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);
    entity->connect(StringName("spawned"), Callable(p_spawned));
    return made;
}

Node3D *build_spatial(const String &p_name, const Vector3 &p_at) {
    Node3D *made = memnew(Node3D);
    made->set_name(p_name);
    made->set_position(p_at);
    made->set_rotation(Vector3(0.0, real_t(Math_PI * 0.5), 0.0));
    return made;
}

Node *build_holder(const Variant &p_name) {
    Node3D *made = build_spatial(String(p_name), Vector3(4.0, 0.0, 0.0));
    NetwEntity::ensure(made)->set_lifecycle(NetwEntity::LIFECYCLE_CONTROLLER);
    return made;
}

Node *build_detaching(const Variant &p_name) {
    Node3D *made = build_spatial(String(p_name), Vector3(1.0, 2.0, 0.0));
    NetwEntity::ensure(made)->set_on_parent_despawn(
        NetwEntity::PARENT_DESPAWN_DETACH
    );
    return made;
}

Node *build_cascading(const Variant &p_name) {
    return build_spatial(String(p_name), Vector3(0.0, 1.0, 3.0));
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Node3D *plain_node(Node *p_parent, const char *p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

int64_t frames_in(
    NetwMultiplayer *p_session,
    int64_t p_from,
    const char *p_name
) {
    const Dictionary rows
        = p_session->attribution_snapshot()["frames_in_by_channel"];
    const Dictionary channels = rows.get(p_from, Dictionary());
    return int64_t(channels.get(
        int64_t(netw::wire::builtin_channel(StringName(p_name))),
        0
    ));
}

int64_t counter_of(NetwMultiplayer *p_session, const char *p_name) {
    const Dictionary counted
        = p_session->get_replication_plane()->get_spawn_pipeline()->counters();
    return int64_t(counted.get(StringName(p_name), -1));
}

class DespawnStage {
public:
    LoopbackRig rig;
    Ref<Script> crate_script;
    CallLog spawned;
    Node *arenas[3] = {nullptr, nullptr, nullptr};
    int crate = 0;

    DespawnStage() : rig(2) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        rig.mirror_child("Arena");
        for (int side = -1; side < 2; ++side) {
            arenas[side + 1]
                = rig.branch(side)->get_node_or_null(NodePath("Arena"));
            REQUIRE(arenas[side + 1] != nullptr);
            plain_node(arenas[side + 1], "Shelf");
        }
        crate_script = netw_test::minted_script(CRATE_SOURCE);
        REQUIRE(crate_script.is_valid());
    }

    void spawn_crate() {
        crate = rig.spawn_registered(
            StringName("lifecycle_crate"),
            callable_mp_static(&build_crate)
                .bind(crate_script, spawned.callable("spawned")),
            named("Crate"),
            one_type(),
            arenas[0]
        );
        NetwEntity::of(rig.route_node(crate))->set_controller(rig.peer_id(0));
        rig.step_ticks(6);
    }

    NetwMultiplayer *side(int p_side) const {
        return p_side < 0 ? rig.server() : rig.client(p_side);
    }

    Node *crate_on(int p_side) const {
        return rig.route_node(crate, p_side);
    }

    bool gone(int p_route, int p_side) const {
        return rig.route_node(p_route, p_side) == nullptr;
    }

    bool under(Node *p_node, Node *p_parent) const {
        return p_node != nullptr && p_parent != nullptr
            && p_node->get_parent() == p_parent;
    }

    Node *arena_on(int p_side) const {
        return arenas[p_side + 1];
    }

    struct HolderTree {
        int holder = 0;
        int cube = 0;
        int satchel = 0;
        int coin = 0;
    };

    HolderTree spawn_holder_tree() {
        HolderTree tree;
        tree.holder = rig.spawn_registered(
            StringName("lifecycle_holder"),
            callable_mp_static(&build_holder),
            named("Holder"),
            one_type(),
            arena_on(-1)
        );
        tree.cube = rig.spawn_registered(
            StringName("lifecycle_detaching"),
            callable_mp_static(&build_detaching),
            named("Cube"),
            one_type(),
            rig.route_node(tree.holder)
        );
        tree.satchel = rig.spawn_registered(
            StringName("lifecycle_cascading"),
            callable_mp_static(&build_cascading),
            named("Satchel"),
            one_type(),
            rig.route_node(tree.holder)
        );
        tree.coin = rig.spawn_registered(
            StringName("lifecycle_detaching"),
            callable_mp_static(&build_detaching),
            named("Coin"),
            one_type(),
            rig.route_node(tree.satchel)
        );
        NetwEntity::of(rig.route_node(tree.holder))
            ->set_controller(rig.peer_id(0));
        rig.step_ticks(6);
        return tree;
    }

    void author_despawns(int p_route) {
        NetwMultiplayer *author = rig.client(0);
        Node *freed = rig.route_node(p_route, 0);
        REQUIRE(freed != nullptr);
        const Error despawned = author->entity_despawn(
            author->entity_from_route(p_route),
            Ref<netw::NetwDespawnOpts>()
        );
        NETW_REQUIRE_EQ(despawned, OK);
        freed->get_parent()->remove_child(freed);
    }
};

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] despawn, a controller's despawn "
    "of its LIFECYCLE_CONTROLLER entity reaches every peer once, fires "
    "despawning on each, and the author never hears its own DESPAWN back"
) {
    DespawnStage stage;
    stage.spawn_crate();
    CallLog despawning[3];
    for (int at = -1; at < 2; ++at) {
        Node *held = stage.crate_on(at);
        REQUIRE(held != nullptr);
        NetwEntity::of(held)->connect(
            StringName("despawning"),
            despawning[at + 1].callable("despawning")
        );
    }
    NETW_CHECK_EQ(
        NetwEntity::of(stage.crate_on(0))->get_controller(),
        int64_t(stage.rig.peer_id(0))
    );

    NetwMultiplayer *author = stage.rig.client(0);
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t echoed = frames_in(author, session_peer, "DESPAWN");
    const int64_t unknown = counter_of(author, "drops_despawn_unknown");
    const int64_t observed = frames_in(observer, session_peer, "DESPAWN");

    stage.author_despawns(stage.crate);
    stage.rig.pump(1);
    CHECK(stage.gone(stage.crate, 0));
    stage.rig.step_ticks(SETTLE_TICKS);

    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.gone(stage.crate, at));
        NETW_CHECK_EQ(despawning[at + 1].count("despawning"), 1);
    }
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_admitted"),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        counter_of(stage.rig.server(), "lifecycle_ops_refused"),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        frames_in(observer, session_peer, "DESPAWN") - observed,
        int64_t(1)
    );
    NETW_CHECK_EQ(
        frames_in(author, session_peer, "DESPAWN") - echoed,
        int64_t(0)
    );
    NETW_CHECK_EQ(
        counter_of(author, "drops_despawn_unknown") - unknown,
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] re-serve, a refused despawn "
    "brings the entity back on the author with the session's values and "
    "spawned fired, and never touches an observer"
) {
    DespawnStage stage;
    stage.spawn_crate();
    Node *written = stage.crate_on(0);
    REQUIRE(written != nullptr);
    for (int step = 0; step < SETTLE_TICKS; ++step) {
        written->set(StringName("hp"), int64_t(step < 4 ? step : 5));
        stage.rig.step_ticks(1);
    }
    NETW_REQUIRE_EQ(int64_t(stage.crate_on(-1)->get(StringName("hp"))), 5);

    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    Node *observed = stage.crate_on(1);
    REQUIRE(observed != nullptr);
    const int64_t observer_spawns = frames_in(observer, session_peer, "SPAWN");
    const int64_t observer_despawns
        = frames_in(observer, session_peer, "DESPAWN");
    const int spawned_before = stage.spawned.count("spawned");

    stage.rig.hold(-1);
    stage.author_despawns(stage.crate);
    stage.rig.step_ticks(2);
    CHECK(stage.gone(stage.crate, 0));
    const Ref<netw::NetwPromise> ruled = session->entity_reparent(
        session->entity_from_route(stage.crate),
        stage.arena_on(-1)->get_node_or_null(NodePath("Shelf"))
    );
    stage.rig.step_ticks(2);
    stage.rig.release(-1);
    stage.rig.step_ticks(SETTLE_TICKS);

    CHECK(ruled->get_is_settled());
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    const bool back = !stage.gone(stage.crate, 0);
    CHECK(back);
    if (!back) {
        return;
    }
    Node *served = stage.crate_on(0);
    const bool rebuilt = served != written;
    CHECK(rebuilt);
    NETW_CHECK_EQ(int64_t(served->get(StringName("hp"))), 5);
    NETW_CHECK_EQ(
        int64_t(served->get(StringName("hp"))),
        int64_t(stage.crate_on(-1)->get(StringName("hp")))
    );
    CHECK(stage.under(
        served,
        stage.arena_on(0)->get_node_or_null(NodePath("Shelf"))
    ));
    NETW_CHECK_EQ(
        int64_t(stage.rig.client(0)->liveness_route_anchor(stage.crate)),
        int64_t(session->liveness_route_anchor(stage.crate))
    );
    NETW_CHECK_EQ(stage.spawned.count("spawned") - spawned_before, 1);
    NETW_CHECK_EQ(
        NetwEntity::of(served)->get_controller(),
        int64_t(stage.rig.peer_id(0))
    );

    const bool untouched = stage.crate_on(1) == observed;
    CHECK(untouched);
    NETW_CHECK_EQ(
        frames_in(observer, session_peer, "SPAWN") - observer_spawns,
        int64_t(0)
    );
    NETW_CHECK_EQ(
        frames_in(observer, session_peer, "DESPAWN") - observer_despawns,
        int64_t(0)
    );
}

Transform3D world_of(LoopbackRig &p_rig, int p_route, int p_side) {
    Node3D *spatial
        = Object::cast_to<Node3D>(p_rig.route_node(p_route, p_side));
    return spatial != nullptr ? spatial->get_global_transform() : Transform3D();
}

bool stands_at(const Transform3D &p_seen, const Transform3D &p_before) {
    return p_seen.origin.distance_to(p_before.origin) < 1e-4
        && p_seen.basis.is_equal_approx(p_before.basis);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] cascade, a controller "
    "despawning its parent entity moves each detaching descendant under the "
    "parent's own parent where it stood and takes the rest with it, on every "
    "peer"
) {
    DespawnStage stage;
    LoopbackRig &rig = stage.rig;
    const DespawnStage::HolderTree tree = stage.spawn_holder_tree();
    const int holder = tree.holder;
    const int satchel = tree.satchel;

    const int moved[2] = {tree.cube, tree.coin};
    Transform3D before[3][2];
    for (int side = -1; side < 2; ++side) {
        for (int at = 0; at < 2; ++at) {
            REQUIRE(rig.route_node(moved[at], side) != nullptr);
            before[side + 1][at] = world_of(rig, moved[at], side);
        }
    }

    stage.author_despawns(holder);
    rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(
        counter_of(rig.server(), "lifecycle_ops_admitted"),
        int64_t(1)
    );
    for (int side = -1; side < 2; ++side) {
        NETW_FORMAT_INT(netw_side_text, side);
        CAPTURE(netw_side_text);
        CHECK(stage.gone(holder, side));
        CHECK(stage.gone(satchel, side));
        for (int at = 0; at < 2; ++at) {
            CHECK_FALSE(stage.gone(moved[at], side));
            CHECK(stage.under(
                rig.route_node(moved[at], side),
                stage.arena_on(side)
            ));
            CHECK(
                stands_at(world_of(rig, moved[at], side), before[side + 1][at])
            );
            NETW_CHECK_EQ(
                int64_t(stage.side(side)->liveness_route_anchor(moved[at])),
                int64_t(rig.server()->liveness_route_anchor(moved[at]))
            );
        }
    }
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] re-serve detach, a refused "
    "despawn moves each descendant the author detached ahead back where the "
    "session holds it, keeping its world pose"
) {
    DespawnStage stage;
    LoopbackRig &rig = stage.rig;
    const DespawnStage::HolderTree tree = stage.spawn_holder_tree();
    const int holder = tree.holder;
    const int satchel = tree.satchel;
    const int cube = tree.cube;
    const int moved[2] = {cube, tree.coin};
    const int held_by[2] = {holder, satchel};
    Transform3D before[2];
    for (int at = 0; at < 2; ++at) {
        before[at] = world_of(rig, moved[at], 0);
    }

    NetwMultiplayer *session = rig.server();
    rig.hold(-1);
    stage.author_despawns(holder);
    rig.step_ticks(2);
    CHECK(stage.under(rig.route_node(cube, 0), stage.arena_on(0)));
    NetwEntity::of(rig.route_node(holder))->set_controller(rig.peer_id(1));
    rig.step_ticks(2);
    rig.release(-1);
    rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    const bool back = !stage.gone(holder, 0) && !stage.gone(satchel, 0);
    CHECK(back);
    if (!back) {
        return;
    }
    for (int at = 0; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(
            rig.route_node(moved[at], 0),
            rig.route_node(held_by[at], 0)
        ));
        CHECK(stage.under(
            rig.route_node(moved[at], -1),
            rig.route_node(held_by[at], -1)
        ));
        CHECK(stands_at(world_of(rig, moved[at], 0), before[at]));
        NETW_CHECK_EQ(
            int64_t(rig.client(0)->liveness_route_anchor(moved[at])),
            int64_t(session->liveness_route_anchor(moved[at]))
        );
    }
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] re-serve detach anchor, a "
    "despawn refused because the session moved the entity leaves each "
    "descendant the author detached ahead at the session's anchor revision "
    "and author"
) {
    DespawnStage stage;
    LoopbackRig &rig = stage.rig;
    const DespawnStage::HolderTree tree = stage.spawn_holder_tree();
    const int moved[2] = {tree.cube, tree.coin};
    const int held_by[2] = {tree.holder, tree.satchel};

    NetwMultiplayer *session = rig.server();
    NetwMultiplayer *author = rig.client(0);
    rig.hold(-1);
    stage.author_despawns(tree.holder);
    rig.step_ticks(2);
    CHECK(stage.under(rig.route_node(tree.cube, 0), stage.arena_on(0)));
    const Ref<netw::NetwPromise> ruled = session->entity_reparent(
        session->entity_from_route(tree.holder),
        stage.arena_on(-1)->get_node_or_null(NodePath("Shelf"))
    );
    rig.step_ticks(2);
    rig.release(-1);
    rig.step_ticks(SETTLE_TICKS);

    CHECK(ruled->get_is_settled());
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    const bool back
        = !stage.gone(tree.holder, 0) && !stage.gone(tree.satchel, 0);
    CHECK(back);
    if (!back) {
        return;
    }
    for (int at = 0; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(
            rig.route_node(moved[at], 0),
            rig.route_node(held_by[at], 0)
        ));
        const NetwMultiplayer::AnchorRevision here
            = author->anchor_installed(moved[at]);
        const NetwMultiplayer::AnchorRevision truth
            = session->anchor_installed(moved[at]);
        CHECK(truth.revision > 1);
        NETW_CHECK_EQ(int64_t(here.revision), int64_t(truth.revision));
        NETW_CHECK_EQ(int64_t(here.author), int64_t(truth.author));
    }
}

} // namespace TestLifecycleDespawnLaws

#endif

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/liveness_core.hpp"
#include "netw/persist/memory_store.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/wire/registry.hpp"
#include "support/minted_script.h"
#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

namespace TestLifecycleSpawnLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int SETTLE_TICKS = 24;
constexpr int64_t STAMPED = 99;

const char *CRATE_SOURCE = R"(extends Node3D

var seen := []
var notes := []
var hp := 0:
	set(value):
		hp = value
		seen.append(value)


func _init() -> void:
	Netw.configure_property(self, &"hp").broadcast().on_spawn()


@rpc("any_peer", "call_remote", "reliable")
func note(tag: int) -> void:
	notes.append(tag)
)";

const char *TEMPLATE_SOURCE = R"(extends Node3D

var hp := 0


func _init() -> void:
	Netw.configure_entity(self).lifecycle = NetwEntity.LIFECYCLE_CONTROLLER
	Netw.configure_property(self, &"hp").broadcast().on_spawn()
)";

const char *TEMPLATE_PATH
    = "res://tests/native/authored_lifecycle_template.tscn";

Node *build_crate(const Variant &p_name, const Variant &p_script) {
    const Ref<Script> script = p_script;
    Node3D *made
        = Object::cast_to<Node3D>(netw::gd::live_object(script->call("new")));
    made->set_name(String(p_name));
    NetwEntity::ensure(made)->set_lifecycle(NetwEntity::LIFECYCLE_CONTROLLER);
    return made;
}

Node *build_tethered(
    const Variant &p_name,
    const Variant &p_tether,
    const Variant &p_script
) {
    Node *made = build_crate(p_name, p_script);
    made->set_meta(StringName("tether"), p_tether);
    return made;
}

Node *build_board(const Variant &p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(String(p_name));
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    sync->set_name("Sync");
    sync->set_root_path(NodePath(".."));
    sync->set_visibility_public(false);
    made->add_child(sync);
    return made;
}

Node *build_anchor(const Variant &p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(String(p_name));
    return made;
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

Array tethered_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    out.push_back(int(Variant::OBJECT));
    return out;
}

Node3D *plain_node(Node *p_parent, const char *p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

int64_t frames_in(NetwMultiplayer *p_api, int64_t p_from, const char *p_name) {
    const Dictionary rows
        = p_api->attribution_snapshot()["frames_in_by_channel"];
    const Dictionary channels = rows.get(p_from, Dictionary());
    return int64_t(channels.get(
        int64_t(netw::wire::builtin_channel(StringName(p_name))),
        0
    ));
}

int64_t counter_of(NetwMultiplayer *p_api, const char *p_name) {
    const Dictionary counted
        = p_api->get_replication_plane()->get_spawn_pipeline()->counters();
    return int64_t(counted.get(StringName(p_name), -1));
}

class SpawnStage {
public:
    LoopbackRig rig;
    Ref<Script> crate_script;
    Node *arenas[3] = {nullptr, nullptr, nullptr};

    SpawnStage() : rig(2) {
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        rig.mirror_child("Arena");
        for (int side = -1; side < 2; ++side) {
            arenas[side + 1]
                = rig.branch(side)->get_node_or_null(NodePath("Arena"));
            REQUIRE(arenas[side + 1] != nullptr);
        }
        plain_node(arenas[1], "Ghost");
        crate_script = netw_test::minted_script(CRATE_SOURCE);
        REQUIRE(crate_script.is_valid());
        for (int side = -1; side < 2; ++side) {
            register_on(side < 0 ? rig.server() : rig.client(side));
        }
        rig.join(0, StringName("author"));
        rig.join(1, StringName("observer"));
        rig.step_ticks(4);
    }

    void register_on(NetwMultiplayer *p_api) {
        rig.register_constructor(
            p_api,
            StringName("authored_crate"),
            callable_mp_static(&build_crate).bind(crate_script),
            one_type()
        );
        rig.register_constructor(
            p_api,
            StringName("authored_tethered"),
            callable_mp_static(&build_tethered).bind(crate_script),
            tethered_types()
        );
    }

    NetwMultiplayer *side(int p_side) const {
        return p_side < 0 ? rig.server() : rig.client(p_side);
    }

    Node *arena_on(int p_side) const {
        return arenas[p_side + 1];
    }

    Node *author_spawns(const char *p_id, const Array &p_args, Node *p_under) {
        NetwMultiplayer *author = rig.client(0);
        const RID entity
            = author->spawn_registered(StringName(p_id), p_args, nullptr);
        Node *node = author->entity_get_node(entity);
        if (node != nullptr && p_under != nullptr) {
            node->set(StringName("hp"), int64_t(1));
            p_under->add_child(node);
        }
        return node;
    }

    bool holds(int p_route, int p_side) const {
        return rig.route_node(p_route, p_side) != nullptr;
    }

    bool under(int p_route, int p_side, Node *p_parent) const {
        Node *held = rig.route_node(p_route, p_side);
        return held != nullptr && p_parent != nullptr
            && held->get_parent() == p_parent;
    }
};

int route_of(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    return entity.is_valid() ? int(entity->get_route()) : 0;
}

void configure_spawned_copy(Object *p_entity, Array p_seen) {
    NetwEntity *entity = Object::cast_to<NetwEntity>(p_entity);
    REQUIRE(entity != nullptr);
    p_seen.push_back(entity->get_entity_id());
    p_seen.push_back(entity->get_owner()->is_inside_tree());
    entity->set_entity_id(StringName("Configured"));
    entity->get_owner()->set(StringName("hp"), STAMPED);
}

Node *place_template(SpawnStage &p_stage, Ref<PackedScene> &r_packed) {
    const Ref<Script> script = netw_test::minted_script(TEMPLATE_SOURCE);
    REQUIRE(script.is_valid());
    Node3D *shape = memnew(Node3D);
    shape->set_name("Template");
    shape->set_script(script);
    shape->remove_meta(NetwMultiplayer::wrapper_meta());
    r_packed.instantiate();
    REQUIRE(r_packed->pack(shape) == OK);
    memdelete(shape);
    r_packed->set_path(TEMPLATE_PATH);

    Node *placed = r_packed->instantiate();
    REQUIRE(placed != nullptr);
    REQUIRE(p_stage.rig.server()
                ->get_replication_plane()
                ->get_spawn_pipeline()
                ->replicate(placed, Ref<netw::NetwPlayer>())
                .is_valid());
    p_stage.arena_on(-1)->add_child(placed);
    p_stage.rig.step_ticks(SETTLE_TICKS);
    return p_stage.rig.route_node(route_of(placed), 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] spawn, a controller's spawn of "
    "a LIFECYCLE_CONTROLLER entity reaches every peer with one route and one "
    "entity_id, the session holds it seeded to the author, and a peer "
    "joining later receives it"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *author = stage.rig.client(0);
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t author_peer = stage.rig.peer_id(0);
    const int64_t echoed = frames_in(author, session_peer, "SPAWN");

    Node *made = stage.author_spawns(
        "authored_crate",
        named("Crate"),
        stage.arena_on(0)
    );
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(SETTLE_TICKS);

    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(route, at, stage.arena_on(at)));
    }
    const bool kept = stage.rig.route_node(route, 0) == made;
    CHECK(kept);
    const StringName id = NetwEntity::of(made)->get_entity_id();
    CHECK_FALSE(id.is_empty());
    for (int at = -1; at < 2; ++at) {
        Node *held = stage.rig.route_node(route, at);
        const bool same_id
            = held != nullptr && NetwEntity::of(held)->get_entity_id() == id;
        CHECK(same_id);
    }
    Node *copy = stage.rig.route_node(route, -1);
    netw::spawn::Record *record = session->get_replication_plane()
                                      ->get_spawn_pipeline()
                                      ->get_spawn_book()
                                      ->spawned_of(route);
    const bool booked = copy != nullptr && record != nullptr;
    CHECK(booked);
    const bool author_holds
        = record != nullptr && record->has_recipient(int(author_peer));
    CHECK(author_holds);
    if (copy != nullptr) {
        NETW_CHECK_EQ(NetwEntity::of(copy)->get_controller(), author_peer);
    }
    const Ref<NetwEntity> seen = NetwEntity::of(stage.rig.route_node(route, 1));
    NETW_CHECK_EQ(seen.is_valid() ? seen->get_controller() : 0, author_peer);
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(0));
    NETW_CHECK_EQ(frames_in(author, session_peer, "SPAWN") - echoed, 0);
    NETW_CHECK_EQ(frames_in(observer, session_peer, "SPAWN") > 0, true);
    CHECK_FALSE(
        session->get_liveness_core()->lease_holds(int(author_peer), route)
    );

    const int late = stage.rig.add_client();
    stage.register_on(stage.rig.client(late));
    stage.rig.hold(late);
    stage.rig.mount_late(late);
    stage.rig.mirror_late(late, "Arena");
    stage.rig.release(late);
    stage.rig.step_ticks(SETTLE_TICKS);
    Node *late_arena
        = stage.rig.branch(late)->get_node_or_null(NodePath("Arena"));
    CHECK(stage.under(route, late, late_arena));
    Node *joined = stage.rig.route_node(route, late);
    const bool joined_same_id
        = joined != nullptr && NetwEntity::of(joined)->get_entity_id() == id;
    CHECK(joined_same_id);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] never, a controller's spawn "
    "the session cannot place is refused, burnt and never sent to an "
    "observer, and the author loses its copy"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    const int64_t author_peer = stage.rig.peer_id(0);
    const int64_t observed = frames_in(observer, session_peer, "SPAWN");
    const int64_t unresolved = counter_of(session, "spawn_deferrals");

    Node *ghost = stage.arena_on(0)->get_node_or_null(NodePath("Ghost"));
    Node *made = stage.author_spawns("authored_crate", named("Lost"), ghost);
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    NETW_CHECK_EQ(counter_of(session, "spawn_deferrals") - unresolved, 0);
    CHECK_FALSE(stage.holds(route, -1));
    CHECK_FALSE(stage.holds(route, 0));
    CHECK_FALSE(stage.holds(route, 1));
    NETW_CHECK_EQ(frames_in(observer, session_peer, "SPAWN") - observed, 0);
    CHECK_FALSE(
        session->get_liveness_core()->lease_holds(int(author_peer), route)
    );
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] no park, a controller's spawn "
    "whose argument names a route the session despawned is refused rather "
    "than parked"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    const int anchor = stage.rig.spawn_registered(
        StringName("authored_anchor"),
        callable_mp_static(&build_anchor),
        named("Anchor"),
        one_type(),
        stage.arena_on(-1)
    );
    stage.rig.step_ticks(4);
    Node *tether = stage.rig.route_node(anchor, 0);
    REQUIRE(tether != nullptr);
    const int64_t deferred = counter_of(session, "spawn_deferrals");
    const int parked = session->get_replication_plane()
                           ->get_spawn_pipeline()
                           ->get_park()
                           .size();

    stage.rig.hold(0);
    NetwEntity::of(stage.rig.route_node(anchor))
        ->despawn(Ref<netw::NetwDespawnOpts>());
    Node *gone = stage.rig.route_node(anchor);
    gone->get_parent()->remove_child(gone);
    stage.rig.step_ticks(4);
    Array args;
    args.push_back(String("Tethered"));
    args.push_back(tether);
    Node *made
        = stage.author_spawns("authored_tethered", args, stage.arena_on(0));
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(4);
    stage.rig.release(0);
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(1));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    NETW_CHECK_EQ(counter_of(session, "spawn_deferrals") - deferred, 0);
    NETW_CHECK_EQ(
        session->get_replication_plane()
                ->get_spawn_pipeline()
                ->get_park()
                .size()
            - parked,
        0
    );
    CHECK_FALSE(stage.holds(route, -1));
    CHECK_FALSE(stage.holds(route, 0));
    CHECK_FALSE(stage.holds(route, 1));
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] no park, a controller's spawn "
    "whose argument names a route the session never held is refused rather "
    "than parked"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    const int64_t deferred = counter_of(session, "spawn_deferrals");

    Node *ghost = stage.arena_on(0)->get_node_or_null(NodePath("Ghost"));
    Node *lost = stage.author_spawns("authored_crate", named("Lost"), ghost);
    REQUIRE(lost != nullptr);
    Array args;
    args.push_back(String("Tethered"));
    args.push_back(lost);
    Node *made
        = stage.author_spawns("authored_tethered", args, stage.arena_on(0));
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_refused"), int64_t(2));
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(0));
    NETW_CHECK_EQ(counter_of(session, "spawn_deferrals") - deferred, 0);
    NETW_CHECK_EQ(
        session->get_replication_plane()
            ->get_spawn_pipeline()
            ->get_park()
            .size(),
        0
    );
    CHECK_FALSE(stage.holds(route, -1));
    CHECK_FALSE(stage.holds(route, 0));
    CHECK_FALSE(stage.holds(route, 1));
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] nested, a stroke a controller "
    "spawns under the board reaches only the peers holding the board"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    const int board = stage.rig.spawn_registered(
        StringName("authored_board"),
        callable_mp_static(&build_board),
        named("Board"),
        one_type(),
        stage.arena_on(-1)
    );
    MultiplayerSynchronizer *sync = Object::cast_to<MultiplayerSynchronizer>(
        stage.rig.route_node(board)->get_node_or_null(NodePath("Sync"))
    );
    REQUIRE(sync != nullptr);
    sync->set_visibility_for(stage.rig.peer_id(0), true);
    session->get_replication_plane()
        ->get_spawn_pipeline()
        ->schedule_visibility_sweep();
    stage.rig.step_ticks(SETTLE_TICKS);
    REQUIRE(stage.holds(board, 0));
    REQUIRE_FALSE(stage.holds(board, 1));

    Node *made = stage.author_spawns(
        "authored_crate",
        named("Stroke"),
        stage.rig.route_node(board, 0)
    );
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(1));
    CHECK(stage.under(route, -1, stage.rig.route_node(board, -1)));
    CHECK(stage.under(route, 0, stage.rig.route_node(board, 0)));
    CHECK_FALSE(stage.holds(route, 1));
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] first rows, an observer's first "
    "values of a controller's spawn are the session's newest, not the "
    "author's"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    Callable stamp = callable_mp_static(+[](int64_t p_route,
                                            const Ref<RefCounted> &p_wrapper) {
        const Ref<NetwEntity> entity
            = Object::cast_to<NetwEntity>(p_wrapper.ptr());
        if (entity.is_valid() && entity->get_owner() != nullptr) {
            entity->get_owner()->set(StringName("hp"), STAMPED);
        }
    });
    session->connect(StringName("entity_live"), stamp);

    Node *made = stage.author_spawns(
        "authored_crate",
        named("Crate"),
        stage.arena_on(0)
    );
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    stage.rig.step_ticks(2);
    session->disconnect(StringName("entity_live"), stamp);
    stage.rig.step_ticks(SETTLE_TICKS);

    Node *observed = stage.rig.route_node(route, 1);
    const Array seen
        = observed != nullptr ? Array(observed->get("seen")) : Array();
    NETW_CHECK_EQ(seen.is_empty() ? int64_t(-1) : int64_t(seen[0]), STAMPED);
    NETW_CHECK_EQ(int64_t(made->get(StringName("hp"))), int64_t(1));
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] rpc, a reliable CALL on a "
    "controller's new entity issued in the frame it is spawned is addressed "
    "to the observer by the author and runs there once"
) {
    SpawnStage stage;
    Node *made = stage.author_spawns(
        "authored_crate",
        named("Crate"),
        stage.arena_on(0)
    );
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    made->rpc(StringName("note"), int64_t(7));
    stage.rig.step_ticks(SETTLE_TICKS);

    for (int at = -1; at < 2; at += 2) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        Node *held = stage.rig.route_node(route, at);
        const Array notes
            = held != nullptr ? Array(held->get("notes")) : Array();
        NETW_CHECK_EQ(notes.size(), 1);
        if (!notes.is_empty()) {
            NETW_CHECK_EQ(int64_t(notes[0]), int64_t(7));
        }
    }
    const Array own = made->get(StringName("notes"));
    NETW_CHECK_EQ(own.size(), 0);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] spawn under, a controller's "
    "copy of a scene template reaches every peer as the session's own spawn"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    Ref<PackedScene> packed;
    Node *seen_template = place_template(stage, packed);
    const bool template_arrived = seen_template != nullptr;
    CHECK(template_arrived);
    if (!template_arrived) {
        return;
    }

    Node *copy = NetwEntity::of(seen_template)
                     ->spawn_under(stage.arena_on(0), StringName("Copy"));
    const bool built = copy != nullptr;
    CHECK(built);
    const int route = built ? route_of(copy) : 0;
    stage.rig.step_ticks(SETTLE_TICKS);

    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), int64_t(1));
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        CHECK(stage.under(route, at, stage.arena_on(at)));
    }
    const bool booked = session->get_replication_plane()
                            ->get_spawn_pipeline()
                            ->get_spawn_book()
                            ->spawned_of(route)
        != nullptr;
    CHECK(booked);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] spawn under, configure runs on "
    "the orphan copy after its id is stamped, so what it writes wins and "
    "rides the spawn to every peer"
) {
    SpawnStage stage;
    Ref<PackedScene> packed;
    Node *seen_template = place_template(stage, packed);
    const bool template_arrived = seen_template != nullptr;
    CHECK(template_arrived);
    if (!template_arrived) {
        return;
    }

    Array seen;
    Node *copy = NetwEntity::of(seen_template)
                     ->spawn_under(
                         stage.arena_on(0),
                         StringName("Copy"),
                         callable_mp_static(&configure_spawned_copy).bind(seen)
                     );
    const bool built = copy != nullptr;
    CHECK(built);
    if (!built) {
        return;
    }
    NETW_REQUIRE_EQ(seen.size(), 2);
    CHECK(StringName(seen[0]) == StringName("Copy"));
    CHECK_FALSE(bool(seen[1]));

    const int route = route_of(copy);
    stage.rig.step_ticks(SETTLE_TICKS);
    for (int at = -1; at < 2; ++at) {
        NETW_FORMAT_INT(netw_side_text, at);
        CAPTURE(netw_side_text);
        Node *held = stage.rig.route_node(route, at);
        const bool arrived = held != nullptr;
        CHECK(arrived);
        if (!arrived) {
            continue;
        }
        CHECK(
            NetwEntity::of(held)->get_entity_id() == StringName("Configured")
        );
        NETW_CHECK_EQ(int64_t(held->get(StringName("hp"))), STAMPED);
    }
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] lease, a controller's spawn "
    "with no route left in its lease is refused on the author and sends "
    "nothing"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *author = stage.rig.client(0);
    const int64_t author_peer = stage.rig.peer_id(0);
    const int64_t block = NetwMultiplayer::ROUTE_LEASE_BLOCK;
    NETW_REQUIRE_EQ(author->liveness_lease_remaining(), block);

    stage.rig.hold(0);
    int64_t spawned = 0;
    for (int64_t at = 0; at < block; ++at) {
        spawned
            += stage.author_spawns(
                   "authored_crate",
                   named("Burst"),
                   stage.arena_on(0)
               ) != nullptr
            ? 1
            : 0;
    }
    stage.rig.step_ticks(2);
    NETW_CHECK_EQ(spawned, block);
    NETW_CHECK_EQ(author->liveness_lease_remaining(), 0);
    const int64_t sent = frames_in(session, author_peer, "SPAWN");

    const RID refused = author->spawn_registered(
        StringName("authored_crate"),
        named("Over"),
        nullptr
    );
    CHECK_FALSE(refused.is_valid());
    stage.rig.step_ticks(4);
    NETW_CHECK_EQ(frames_in(session, author_peer, "SPAWN") - sent, 0);
    NETW_CHECK_EQ(counter_of(session, "lifecycle_ops_admitted"), block);
    stage.rig.release(0);
    stage.rig.step_ticks(SETTLE_TICKS);
    NETW_CHECK_EQ(author->liveness_lease_remaining() > 0, true);
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] leave, when the controller of "
    "the entity it spawned leaves, control reverts to the session, which then "
    "despawns the entity everywhere"
) {
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    NetwMultiplayer *observer = stage.rig.client(1);
    const int64_t session_peer = stage.rig.peer_id(-1);
    Node *made = stage.author_spawns(
        "authored_crate",
        named("Left"),
        stage.arena_on(0)
    );
    REQUIRE(made != nullptr);
    const int route = route_of(made);
    REQUIRE(route > 0);
    stage.rig.step_ticks(SETTLE_TICKS);
    REQUIRE(stage.holds(route, -1));
    REQUIRE(stage.holds(route, 1));

    stage.rig.drop_client(0);
    stage.rig.step_ticks(SETTLE_TICKS);

    const bool kept = stage.holds(route, -1) && stage.holds(route, 1);
    CHECK(kept);
    if (!kept) {
        return;
    }
    NETW_CHECK_EQ(
        NetwEntity::of(stage.rig.route_node(route, -1))->get_controller(),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        NetwEntity::of(stage.rig.route_node(route, 1))->get_controller(),
        int64_t(0)
    );
    const int64_t despawns = frames_in(observer, session_peer, "DESPAWN");
    Node *freed = stage.rig.route_node(route, -1);
    const Error despawned = session->entity_despawn(
        session->entity_from_route(route),
        Ref<netw::NetwDespawnOpts>()
    );
    NETW_CHECK_EQ(despawned, OK);
    freed->get_parent()->remove_child(freed);
    stage.rig.step_ticks(SETTLE_TICKS);

    CHECK_FALSE(stage.holds(route, -1));
    CHECK_FALSE(stage.holds(route, 1));
    NETW_CHECK_EQ(frames_in(observer, session_peer, "DESPAWN") - despawns, 1);
}

LocalVector<ObjectID> &built_blocks() {
    static LocalVector<ObjectID> built;
    return built;
}

Node *build_block(
    const Variant &p_name,
    const Variant &p_script,
    const Variant &p_schema,
    const Variant &p_account,
    const Variant &p_loads
) {
    Node *made = build_crate(p_name, p_script);
    const Ref<netw::NetwSchema> schema = p_schema;
    Object *account = p_account;
    netw::Netw::configure_property(made, StringName("hp"), true)
        ->persisted(schema->column_ref(0));
    netw::Netw::configure_persistence(made)
        ->database(StringName("saves"))
        ->schema(schema)
        ->record_id(Callable(account, "get_name"))
        ->load_on_spawn(bool(p_loads));
    built_blocks().push_back(netw::gd::instance_id(made));
    return made;
}

struct Stored {
    Node *account = nullptr;

    Stored() {
        netw::schema_model::clear();
        netw::persist::forget_stores();
        built_blocks().clear();
        account = memnew(Node);
        account->set_name("block");
    }

    ~Stored() {
        for (const ObjectID &id : built_blocks()) {
            netw::script::model::clear_node_overlay(
                Object::cast_to<Node>(netw::gd::object_of(id))
            );
        }
        built_blocks().clear();
        netw::script::model::sweep_dead_overlays();
        memdelete(account);
        netw::schema_model::clear();
        netw::persist::forget_stores();
    }
};

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] save, a block a client placed "
    "is saved by the session with the values the client wrote, and reloads "
    "as a block the session controls"
) {
    Stored stored;
    SpawnStage stage;
    NetwMultiplayer *session = stage.rig.server();
    Node *account = stored.account;
    const Ref<netw::NetwSchema> schema = netw::NetwSchema::create("blocks");
    schema->replicated(false);
    schema->i32("hp", 1);
    const RID database = session->get_databases()->create("saves");
    session->get_databases()->open(
        database,
        "slot1",
        NetwPromise::resolved(
            netw::persist::MemoryConnection::opened("k9", "slot1")
        )
    );
    for (int side = -1; side < 2; ++side) {
        for (const bool loads : {false, true}) {
            stage.rig.register_constructor(
                stage.side(side),
                StringName(loads ? "reloaded_block" : "placed_block"),
                callable_mp_static(&build_block)
                    .bind(stage.crate_script, schema, account, loads),
                one_type()
            );
        }
    }

    Node *placed = stage.author_spawns(
        "placed_block",
        named("Block"),
        stage.arena_on(0)
    );
    REQUIRE(placed != nullptr);
    const int route = route_of(placed);
    REQUIRE(route > 0);
    placed->set(StringName("hp"), int64_t(7));
    stage.rig.step_ticks(SETTLE_TICKS);
    Node *copy = stage.rig.route_node(route, -1);
    REQUIRE(copy != nullptr);
    NETW_REQUIRE_EQ(int64_t(copy->get(StringName("hp"))), int64_t(7));

    const Ref<NetwPromise> saved
        = session->persist_save(session->entity_from_route(route));
    stage.rig.step_ticks(4);
    CHECK(saved->get_is_completed());
    CHECK_FALSE(saved->get_is_failed());
    NETW_CHECK_EQ(
        session->entity_despawn(
            session->entity_from_route(route),
            Ref<netw::NetwDespawnOpts>()
        ),
        OK
    );
    copy->get_parent()->remove_child(copy);
    stage.rig.step_ticks(SETTLE_TICKS);

    const RID reloaded = session->spawn_registered(
        StringName("reloaded_block"),
        named("Reloaded"),
        nullptr
    );
    Node *back = session->entity_get_node(reloaded);
    REQUIRE(back != nullptr);
    stage.arena_on(-1)->add_child(back);
    stage.rig.step_ticks(SETTLE_TICKS);

    const int again = route_of(back);
    NETW_CHECK_EQ(int64_t(back->get(StringName("hp"))), int64_t(7));
    NETW_CHECK_EQ(NetwEntity::of(back)->get_controller(), int64_t(0));
    Node *seen = stage.rig.route_node(again, 1);
    const bool observed = seen != nullptr;
    CHECK(observed);
    if (observed) {
        NETW_CHECK_EQ(int64_t(seen->get(StringName("hp"))), int64_t(7));
        NETW_CHECK_EQ(NetwEntity::of(seen)->get_controller(), int64_t(0));
    }
}

} // namespace TestLifecycleSpawnLaws

#endif

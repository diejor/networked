#include "support/fault_log.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "support/stock_stand.h"
#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/scene.hpp"
#include "netw/script/model.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSpawnNestedIdentityLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

const char *BALL = "ball";
const char *SPIN = "spin";

Node2D *rooted(Node *p_parent, const char *p_name, Node *p_owner) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    p_parent->add_child(made);
    made->set_owner(p_owner);
    return made;
}

Node *build_nested_arena(const Variant &) {
    Node2D *arena = memnew(Node2D);
    arena->set_name("Arena");
    Node2D *ball = rooted(arena, BALL, arena);
    netw::script::model::configure_node_property(ball, StringName("position"))
        ->on_spawn();
    NetwEntity::ensure(ball)->set_name(StringName(BALL));
    Node2D *spin = rooted(ball, SPIN, arena);
    NetwEntity::ensure(spin)->set_name(StringName(SPIN));
    return arena;
}

Node *build_plain_arena(const Variant &) {
    Node2D *arena = memnew(Node2D);
    arena->set_name("Arena");
    Node2D *body = rooted(arena, "Body", arena);
    netw::script::model::configure_node_property(body, StringName("position"))
        ->on_spawn();
    return arena;
}

Array one_arg() {
    Array out;
    out.push_back(String("arena"));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

int spawn_arena(
    LoopbackRig &p_rig,
    Node *p_under = nullptr,
    const Callable &p_build = callable_mp_static(&build_nested_arena),
    const StringName &p_id = StringName("nested_arena")
) {
    return p_rig
        .spawn_registered(p_id, p_build, one_arg(), one_type(), p_under);
}

int64_t route_of(Node *p_node) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    return entity.is_valid() ? entity->get_route() : int64_t(0);
}

Node *descend(Node *p_root, const char *p_path) {
    return p_root != nullptr ? p_root->get_node_or_null(NodePath(p_path))
                             : nullptr;
}

struct Nest {
    int64_t arena = 0;
    int64_t ball = 0;
    int64_t spin = 0;
};

Nest read_nest(Node *p_arena) {
    Nest out;
    out.arena = route_of(p_arena);
    out.ball = route_of(descend(p_arena, BALL));
    out.spin = route_of(descend(p_arena, "ball/spin"));
    return out;
}

int seat_late(LoopbackRig &p_rig, const PackedInt64Array &p_replay) {
    const int late = p_rig.add_client();
    p_rig.register_constructor(
        p_rig.client(late),
        StringName("nested_arena"),
        callable_mp_static(&build_nested_arena),
        one_type()
    );
    p_rig.mount_late(late);
    for (int at = 0; at < p_replay.size(); ++at) {
        p_rig.deliver_spawn(late, p_rig.spawn_frame_of(int(p_replay[at])));
    }
    p_rig.pump(6);
    return late;
}

void check_matches(const Nest &p_host, const Nest &p_seat) {
    const bool ball_is_published = p_host.ball > 0;
    CHECK(ball_is_published);
    const bool spin_is_published = p_host.spin > 0;
    CHECK(spin_is_published);
    const bool ball_agrees = p_seat.ball == p_host.ball;
    CHECK(ball_agrees);
    const bool spin_agrees = p_seat.spin == p_host.spin;
    CHECK(spin_agrees);
}

TEST_CASE(
    "[Networked][Spawn] CN1 a spawned root publishes the identity of every "
    "entity nested inside it, parent before child, so the receiver reads the "
    "same nonzero route for a grandchild it never asked for"
) {
    LoopbackRig rig(1);
    rig.mount();
    const int arena_route = spawn_arena(rig);

    Node *authored = rig.route_node(arena_route);
    Node *seat = rig.route_node(arena_route, 0);
    const bool seat_arrived = seat != nullptr && seat != authored;
    REQUIRE(seat_arrived);

    const Nest host = read_nest(authored);
    const Nest mirror = read_nest(seat);
    check_matches(host, mirror);

    Node *seat_ball = rig.route_node(int(host.ball), 0);
    const bool ball_is_the_seat_child = seat_ball == descend(seat, BALL);
    CHECK(ball_is_the_seat_child);
    const bool spin_under_ball
        = rig.route_node(int(host.spin), 0) == descend(seat, "ball/spin");
    CHECK(spin_under_ball);
}

TEST_CASE(
    "[Networked][Spawn] CN2 announcing a nested identity a second time reuses "
    "the route it already has and builds no second node, because an ADOPT "
    "names an entity that exists rather than creating one"
) {
    LoopbackRig rig(1);
    rig.mount();
    const int arena_route = spawn_arena(rig);

    Node *authored = rig.route_node(arena_route);
    const Nest before = read_nest(authored);
    const bool published = before.ball > 0;
    REQUIRE(published);

    const PackedByteArray ball_frame = rig.spawn_frame_of(int(before.ball));
    const Dictionary counted = Dictionary(rig.spawn_plane(0)->counters());
    const int64_t duplicates
        = int64_t(counted[StringName("drops_spawn_duplicate")]);

    rig.deliver_spawn(0, ball_frame);
    rig.pump(4);

    const Dictionary after = Dictionary(rig.spawn_plane(0)->counters());
    NETW_CHECK_EQ(
        int64_t(after[StringName("drops_spawn_duplicate")]),
        duplicates + 1
    );

    const Nest again = read_nest(authored);
    NETW_CHECK_EQ(again.ball, before.ball);
    NETW_CHECK_EQ(again.spin, before.spin);

    Node *seat = rig.route_node(arena_route, 0);
    Node *seat_ball = descend(seat, BALL);
    const bool one_ball = seat_ball != nullptr && seat->get_child_count() == 1;
    CHECK(one_ball);
    const bool one_spin
        = seat_ball != nullptr && seat_ball->get_child_count() == 1;
    CHECK(one_spin);
}

TEST_CASE(
    "[Networked][Spawn] CN3 an explicit adopt of a root already standing in "
    "the tree publishes what is nested inside it on the same pass"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = build_nested_arena(Variant());
    rig.branch(-1)->add_child(arena);
    Node *pre_placed = build_nested_arena(Variant());
    rig.branch(0)->add_child(pre_placed);
    rig.pump(4);

    const Ref<NetwEntity> adopted
        = rig.server()->replication_adopt_in_place(arena);
    REQUIRE(adopted.is_valid());
    rig.pump(8);

    const Nest host = read_nest(arena);
    Node *seat = rig.route_node(int(host.arena), 0);
    const bool seat_arrived = seat == pre_placed;
    REQUIRE(seat_arrived);
    check_matches(host, read_nest(seat));
}

TEST_CASE(
    "[Networked][Spawn] CN4 a peer that joins after the spawn replays the "
    "nested identities too, so a late seat is not missing the routes an "
    "early one holds"
) {
    LoopbackRig rig(1);
    rig.mount();
    const int arena_route = spawn_arena(rig);
    const Nest host = read_nest(rig.route_node(arena_route));
    const bool published = host.ball > 0;
    REQUIRE(published);

    const PackedInt64Array replay
        = rig.spawn_plane()->get_spawn_book()->ancestry_order();
    NETW_CHECK_EQ(replay.find(host.arena) >= 0, true);
    const bool ball_replays_after_arena
        = replay.find(host.ball) > replay.find(host.arena);
    CHECK(ball_replays_after_arena);
    const bool spin_replays_after_ball
        = replay.find(host.spin) > replay.find(host.ball);
    CHECK(spin_replays_after_ball);

    const int late = seat_late(rig, replay);
    check_matches(host, read_nest(rig.route_node(arena_route, late)));
}

TEST_CASE(
    "[Networked][Spawn] CN5 leaving a scene closes the nested publication "
    "with its root, and re-entering opens both again under the same routes"
) {
    const String path("res://tests/support/declared_level.tscn");
    const Ref<PackedScene> warm = netw::gd::load_scene(path);
    LoopbackRig rig(1);
    rig.mount();
    netw::NetwMultiplayer *api = rig.server();
    Node *level = api->scene_spawn(
        path,
        netw::NetwMultiplayer::SCENE_ISOLATION_OWN_WORLD
    );
    REQUIRE(level != nullptr);
    rig.pump(6);
    const RID scene = api->entity_of(level);
    REQUIRE(api->scene_watch(scene, rig.peer_id(0)) == OK);
    rig.pump(6);

    const int arena_route = spawn_arena(rig, level);
    const Nest host = read_nest(rig.route_node(arena_route));
    check_matches(host, read_nest(rig.route_node(arena_route, 0)));

    REQUIRE(api->scene_unwatch(scene, rig.peer_id(0)));
    rig.pump(10);
    const bool ball_left = rig.route_node(int(host.ball), 0) == nullptr;
    CHECK(ball_left);

    REQUIRE(api->scene_watch(scene, rig.peer_id(0)) == OK);
    rig.pump(10);

    Node *seat = rig.route_node(arena_route, 0);
    const bool seat_returned = seat != nullptr;
    REQUIRE(seat_returned);
    check_matches(host, read_nest(seat));

    Node *outer = netw::NetwMultiplayer::scene_outer_of(level);
    outer->get_parent()->remove_child(outer);
    memdelete(outer);
}

TEST_CASE(
    "[Networked][Spawn] CN6 the enclosing on-spawn walk stops at a nested "
    "entity, because that entity's own snapshot owns its columns, and it "
    "keeps a child that roots no entity of its own"
) {
    LoopbackRig rig(1);
    rig.mount();
    const int nested_route = spawn_arena(rig);
    Node *nested = rig.route_node(nested_route);
    Node *nested_ball = descend(nested, BALL);
    REQUIRE(nested_ball != nullptr);

    netw::spawn::Pipeline *pipeline = rig.spawn_plane();
    REQUIRE(pipeline != nullptr);

    const TypedArray<Dictionary> enclosing
        = pipeline->collect_spawn_state(nested);
    NETW_CHECK_EQ(enclosing.size(), 0);

    const TypedArray<Dictionary> owned
        = pipeline->collect_spawn_state(nested_ball);
    NETW_CHECK_EQ(owned.size(), 1);

    const int plain_route = spawn_arena(
        rig,
        nullptr,
        callable_mp_static(&build_plain_arena),
        StringName("plain_arena")
    );
    Node *plain = rig.route_node(plain_route);
    const TypedArray<Dictionary> whole = pipeline->collect_spawn_state(plain);
    NETW_CHECK_EQ(whole.size(), 1);
}

TEST_CASE(
    "[Networked][Spawn] CN7 a nested entity's own on-spawn column travels "
    "with its own snapshot, so the receiver reads the value the author set "
    "and not the default its constructor built"
) {
    LoopbackRig rig(1);
    rig.mount();
    const int arena_route = spawn_arena(rig);

    Node *authored = rig.route_node(arena_route);
    Node2D *ball = Object::cast_to<Node2D>(descend(authored, BALL));
    REQUIRE(ball != nullptr);
    ball->set_position(Vector2(12.0, 7.0));
    rig.pump(4);

    const int late
        = seat_late(rig, rig.spawn_plane()->get_spawn_book()->ancestry_order());

    Node2D *seat_ball = Object::cast_to<Node2D>(
        descend(rig.route_node(arena_route, late), BALL)
    );
    const bool ball_arrived = seat_ball != nullptr;
    REQUIRE(ball_arrived);
    const bool pose_travelled
        = seat_ball->get_position().distance_to(Vector2(12.0, 7.0)) < 0.01;
    CHECK(pose_travelled);
}

struct ForeignParent {
    const char *label;
    bool placed_by_the_level;
};

const ForeignParent FOREIGN_PARENTS[] = {
    {"nested-in-a-spawned-root", false},
    {"placed-by-the-level", true},
};

struct ForeignEvidence {
    bool built = false;
    int64_t host_anchor = 0;
    int64_t seat_anchor = 0;
    bool seated = false;
    bool under_the_foreign_body = false;
    bool left_its_origin = false;
    bool spin_follows = false;
    bool early_seat_agrees = false;
};

void place_level(LoopbackRig &p_rig, int p_client) {
    Node *branch = p_rig.branch(p_client);
    Node *origin = build_nested_arena(Variant());
    origin->set_name("ArenaA");
    branch->add_child(origin);
    Node *foreign = build_plain_arena(Variant());
    foreign->set_name("ArenaB");
    branch->add_child(foreign);
}

int64_t adopt_placed(LoopbackRig &p_rig, const char *p_name) {
    const Ref<NetwEntity> adopted = p_rig.server()->replication_adopt_in_place(
        descend(p_rig.branch(-1), p_name)
    );
    return adopted.is_valid() ? adopted->get_route() : int64_t(0);
}

ForeignEvidence run_foreign_parent(const ForeignParent &p_case) {
    ForeignEvidence seen;
    LoopbackRig rig(1);
    rig.mount();
    int64_t origin_route = 0;
    int64_t foreign_route = 0;
    if (p_case.placed_by_the_level) {
        place_level(rig, -1);
        place_level(rig, 0);
        rig.pump(4);
        origin_route = adopt_placed(rig, "ArenaA");
        foreign_route = adopt_placed(rig, "ArenaB");
    } else {
        origin_route = spawn_arena(rig);
        foreign_route = spawn_arena(
            rig,
            nullptr,
            callable_mp_static(&build_plain_arena),
            StringName("plain_arena")
        );
    }
    rig.pump(8);
    Node *origin = rig.route_node(int(origin_route));
    Node *foreign = rig.route_node(int(foreign_route));
    const Nest host = read_nest(origin);
    seen.built = origin != nullptr && foreign != nullptr && host.ball > 0;
    if (!seen.built) {
        return seen;
    }

    netw::NetwMultiplayer::entity_move(
        descend(origin, BALL),
        descend(foreign, "Body")
    );
    rig.pump(8);
    seen.host_anchor = int64_t(rig.server()->liveness_route_anchor(host.ball));
    Node *early_ball = rig.route_node(int(host.ball), 0);
    Node *early_foreign = rig.route_node(int(foreign_route), 0);
    seen.early_seat_agrees = early_ball != nullptr && early_foreign != nullptr
        && early_ball->get_parent() == descend(early_foreign, "Body");

    const int late = rig.add_client();
    rig.register_constructor(
        rig.client(late),
        StringName("nested_arena"),
        callable_mp_static(&build_nested_arena),
        one_type()
    );
    rig.register_constructor(
        rig.client(late),
        StringName("plain_arena"),
        callable_mp_static(&build_plain_arena),
        one_type()
    );
    rig.mount_late(late, false);
    if (p_case.placed_by_the_level) {
        place_level(rig, late);
    }
    const PackedInt64Array replay
        = rig.spawn_plane()->get_spawn_book()->ancestry_order();
    for (int at = 0; at < replay.size(); ++at) {
        rig.deliver_spawn(late, rig.spawn_frame_of(int(replay[at])));
    }
    rig.pump(8);

    Node *seat_ball = rig.route_node(int(host.ball), late);
    Node *seat_origin = rig.route_node(int(origin_route), late);
    Node *seat_foreign = rig.route_node(int(foreign_route), late);
    seen.seated = seat_ball != nullptr;
    seen.under_the_foreign_body = seen.seated && seat_foreign != nullptr
        && seat_ball->get_parent() == descend(seat_foreign, "Body");
    seen.left_its_origin
        = seat_origin != nullptr && descend(seat_origin, BALL) == nullptr;
    seen.spin_follows = seen.seated
        && rig.route_node(int(host.spin), late) == descend(seat_ball, SPIN);
    seen.seat_anchor
        = int64_t(rig.client(late)->liveness_route_anchor(host.ball));
    return seen;
}

TEST_CASE(
    "[Networked][Spawn] CN8 a peer that joins while a scene-placed entity "
    "stands under another entity's child finds it where the scene put it and "
    "moves it there, under the revision the move was made at"
) {
    for (const ForeignParent &placement : FOREIGN_PARENTS) {
        NETW_FORMAT_TEXT(netw_cell_text, placement.label);
        CAPTURE(netw_cell_text);
        const ForeignEvidence seen = run_foreign_parent(placement);
        REQUIRE(seen.built);
        CHECK(seen.early_seat_agrees);
        NETW_CHECK_EQ(seen.host_anchor, int64_t(2));
        CHECK(seen.seated);
        CHECK(seen.under_the_foreign_body);
        CHECK(seen.left_its_origin);
        CHECK(seen.spin_follows);
        NETW_CHECK_EQ(seen.seat_anchor, seen.host_anchor);
    }
}

constexpr const char *UNNAMED_CRATE = R"(extends Node2D

func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_property(self, &"visible").state()
)";

constexpr const char *NAMED_CRATE = R"(extends Node2D

func _init() -> void:
	Netw.configure_entity(self).name = &"crate"
	Netw.configure_property(self, &"visible").state()
)";

struct PlacedCrate {
    const char *label;
    const char *source;
    const char *name;
};

const PlacedCrate PLACED_CRATES[] = {
    {"no-name", UNNAMED_CRATE, nullptr},
    {"named-in-init", NAMED_CRATE, "crate"},
};

struct PlacedEvidence {
    int64_t ready_stage = -1;
    int64_t host_route = 0;
    StringName host_name;
    int64_t seat_route = 0;
    StringName seat_name;
    int64_t late_route = 0;
    StringName late_name;
    bool late_visible = true;
};

Node *place_crate_level(LoopbackRig &p_rig, int p_client, const char *p_src) {
    Node2D *level = memnew(Node2D);
    level->set_name("Level");
    Node *crate = scripted_root(p_src, "Crate");
    level->add_child(crate);
    crate->set_owner(level);
    p_rig.branch(p_client)->add_child(level);
    return crate;
}

void read_crate(Node *p_crate, int64_t &r_route, StringName &r_name) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_crate);
    r_route = entity.is_valid() ? entity->get_route() : int64_t(0);
    r_name = entity.is_valid() ? entity->get_name() : StringName();
}

PlacedEvidence run_placed_crate(const PlacedCrate &p_case) {
    PlacedEvidence seen;
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30);
    Node *host = place_crate_level(rig, -1, p_case.source);
    const Ref<NetwEntity> hosted = NetwEntity::of(host);
    seen.ready_stage = hosted.is_valid() ? hosted->get_stage() : -1;
    read_crate(host, seen.host_route, seen.host_name);
    Node *seat = place_crate_level(rig, 0, p_case.source);
    rig.step_ticks(12);
    read_crate(seat, seen.seat_route, seen.seat_name);

    Object::cast_to<CanvasItem>(host)->set_visible(false);
    rig.step_ticks(4);
    const int late = rig.add_client();
    rig.mount_late(late, false);
    Node *joiner = place_crate_level(rig, late, p_case.source);
    const PackedInt64Array replay
        = rig.spawn_plane()->get_spawn_book()->ancestry_order();
    for (int at = 0; at < replay.size(); ++at) {
        rig.deliver_spawn(late, rig.spawn_frame_of(int(replay[at])));
    }
    rig.step_ticks(8);
    read_crate(joiner, seen.late_route, seen.late_name);
    seen.late_visible = Object::cast_to<CanvasItem>(joiner)->is_visible();
    return seen;
}

TEST_CASE(
    "[Networked][Spawn] NA1 to NA5 an entity placed in a level under the "
    "session root goes online with or without a name and with no verb, and "
    "every peer, a late one too, reads the server's name and state"
) {
    for (const PlacedCrate &placement : PLACED_CRATES) {
        NETW_FORMAT_TEXT(netw_cell_text, placement.label);
        CAPTURE(netw_cell_text);
        const PlacedEvidence seen = run_placed_crate(placement);
        NETW_CHECK_EQ(seen.ready_stage, int64_t(NetwEntity::STAGE_LIVE));
        REQUIRE(seen.host_route > 0);
        const StringName expected = placement.name != nullptr
            ? StringName(placement.name)
            : StringName(vformat("Crate@%d", seen.host_route));
        CHECK(seen.host_name == expected);
        NETW_CHECK_EQ(seen.seat_route, seen.host_route);
        CHECK(seen.seat_name == expected);
        NETW_CHECK_EQ(seen.late_route, seen.host_route);
        CHECK(seen.late_name == expected);
        CHECK_FALSE(seen.late_visible);
    }
}

struct UnconnectedPair {
    Ref<netw::LocalLoopbackSession> link;
    Ref<netw::NetwMultiplayer> apis[3];
    Node *mounts[3] = {nullptr, nullptr, nullptr};
    int count = 0;

    explicit UnconnectedPair(int p_count = 2) : count(p_count) {
        link.instantiate();
        Node *scene = netw::gd::scene_root();
        SceneTree *tree = netw::gd::scene_tree();
        for (int at = 0; at < count; ++at) {
            apis[at] = netw::NetwMultiplayer::create(Ref<Script>());
            apis[at]->session_default_join_held = true;
            apis[at]->session_get_inner()->set_root_path(NodePath("/"));
            Ref<netw::NetwClockConfig> clock;
            clock.instantiate();
            clock->set_tickrate(30);
            apis[at]->clock_initialize(clock);
            apis[at]->clock_engine().set_manual_tick(true);
            mounts[at] = memnew(Node);
            mounts[at]->set_name(vformat("Unconnected%d", at));
            scene->add_child(mounts[at]);
            apis[at]->session_set_root(
                Callable(mounts[at], "get_node").bind(NodePath("."))
            );
            tree->set_multiplayer(
                Ref<MultiplayerAPI>(
                    Object::cast_to<MultiplayerAPI>(apis[at].ptr())
                ),
                mounts[at]->get_path()
            );
        }
    }

    ~UnconnectedPair() {
        SceneTree *tree = netw::gd::scene_tree();
        for (int at = 0; at < count; ++at) {
            apis[at]->set("multiplayer_peer", Ref<MultiplayerPeer>());
            tree->set_multiplayer(
                Ref<MultiplayerAPI>(),
                mounts[at]->get_path()
            );
            mounts[at]->get_parent()->remove_child(mounts[at]);
            memdelete(mounts[at]);
        }
        link->reset();
    }

    void pump(int p_ticks) {
        for (int tick = 0; tick < p_ticks; ++tick) {
            for (int at = 0; at < count; ++at) {
                apis[at]->clock_engine().force_step(1);
            }
            link->advance_time(1000.0 / 30.0);
            for (int at = 0; at < count; ++at) {
                apis[at]->poll();
            }
        }
    }

    void connect(int p_at) {
        apis[p_at]->set(
            "multiplayer_peer",
            p_at == 0 ? Ref<MultiplayerPeer>(link->get_server_peer())
                      : Ref<MultiplayerPeer>(link->create_client_peer())
        );
        pump(4);
    }

    Node *place(int p_at) {
        Node2D *level = memnew(Node2D);
        level->set_name("Level");
        Node *crate = scripted_root(NAMED_CRATE, "Crate");
        level->add_child(crate);
        crate->set_owner(level);
        mounts[p_at]->add_child(level);
        return crate;
    }

    Node *place_level(int p_at, const char *p_path) {
        const Ref<PackedScene> packed = netw::gd::load_scene(String(p_path));
        REQUIRE(packed.is_valid());
        Node *level = packed->instantiate();
        REQUIRE(level != nullptr);
        Node *crate = scripted_root(NAMED_CRATE, "Crate");
        level->add_child(crate);
        crate->set_owner(level);
        mounts[p_at]->add_child(level);
        level->set_owner(mounts[p_at]);
        return level;
    }

    Node *place_all(int p_at, const char *const *p_names, int p_count) {
        Node2D *level = memnew(Node2D);
        level->set_name("Level");
        for (int at = 0; at < p_count; ++at) {
            Node *placed = scripted_root(UNNAMED_CRATE, p_names[at]);
            level->add_child(placed);
            placed->set_owner(level);
        }
        mounts[p_at]->add_child(level);
        return level;
    }
};

struct OfflineClash {
    const char *label;
    const char *host[2];
    int host_count;
    const char *seat[2];
    int seat_count;
};

const OfflineClash OFFLINE_CLASHES[] = {
    {"the-same-node", {"Crate", nullptr}, 1, {"Crate", nullptr}, 1},
    {"another-node", {"Pre", "Crate"}, 2, {"Crate", "Extra"}, 2},
};

TEST_CASE(
    "[Networked][Spawn][SceneTree] NA8 a client whose copy took a route "
    "before it connected gives it up and binds the server's route, whatever "
    "node its own counting had put there"
) {
    for (const OfflineClash &clash : OFFLINE_CLASHES) {
        NETW_FORMAT_TEXT(netw_cell_text, clash.label);
        CAPTURE(netw_cell_text);
        UnconnectedPair pair;
        Node *hosted = pair.place_all(0, clash.host, clash.host_count);
        Node *seated = pair.place_all(1, clash.seat, clash.seat_count);
        pair.pump(2);
        Node *seat_crate = seated->get_node_or_null(NodePath("Crate"));
        REQUIRE(route_of(seat_crate) > 0);
        pair.connect(0);
        pair.connect(1);
        pair.pump(12);

        Node *host_crate = hosted->get_node_or_null(NodePath("Crate"));
        int64_t host_route = 0;
        StringName host_name;
        read_crate(host_crate, host_route, host_name);
        int64_t seat_route = 0;
        StringName seat_name;
        read_crate(seat_crate, seat_route, seat_name);
        REQUIRE(host_route > 0);
        NETW_CHECK_EQ(seat_route, host_route);
        CHECK(seat_name == host_name);
        const Dictionary counted = Dictionary(
            pair.apis[1]->spawn_plane()->counters()
        );
        NETW_CHECK_EQ(
            int64_t(counted[StringName("drops_spawn_duplicate")]),
            int64_t(0)
        );
        NETW_CHECK_EQ(
            int64_t(pair.apis[1]->get_liveness_core()->route_count()),
            int64_t(1)
        );
        NETW_CHECK_EQ(
            route_of(seated->get_node_or_null(NodePath("Extra"))),
            int64_t(0)
        );
    }
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] NA6 an entity placed before the server "
    "hosts is published once it hosts, so a client that joins later binds its "
    "own copy and reads a value the server set before the join"
) {
    UnconnectedPair pair;
    Node *hosted = pair.place(0);
    pair.pump(2);
    Object::cast_to<CanvasItem>(hosted)->set_visible(false);
    pair.connect(0);
    pair.connect(1);
    Node *joined = pair.place(1);
    pair.pump(12);

    int64_t host_route = 0;
    StringName host_name;
    read_crate(hosted, host_route, host_name);
    int64_t seat_route = 0;
    StringName seat_name;
    read_crate(joined, seat_route, seat_name);
    REQUIRE(host_route > 0);
    NETW_CHECK_EQ(seat_route, host_route);
    CHECK(seat_name == StringName("crate"));
    CHECK_FALSE(Object::cast_to<CanvasItem>(joined)->is_visible());
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] NA7 an entity built in code and added "
    "with add_child on the server is reported once and sent to no client"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][spawn] 'Crate' was added to the tree without Netw.spawn, so "
        "no other peer has it. Spawn it with Netw.spawn.",
        1
    );
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30);
    Node *crate = scripted_root(NAMED_CRATE, "Crate");
    rig.branch(-1)->add_child(crate);
    rig.step_ticks(4);
    rig.branch(-1)->remove_child(crate);
    rig.branch(-1)->add_child(crate);
    rig.step_ticks(4);

    const Dictionary sent = Dictionary(rig.spawn_plane()->counters());
    NETW_CHECK_EQ(int64_t(sent[StringName("spawn_book_spawned")]), int64_t(0));
    const Dictionary heard = Dictionary(rig.spawn_plane(0)->counters());
    NETW_CHECK_EQ(int64_t(heard[StringName("spawn_deferrals")]), int64_t(0));
    rig.branch(-1)->remove_child(crate);
    memdelete(crate);
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] NA10 an entity with no owner that was in "
    "the tree before the server hosted is a placed scene root, so it is "
    "published by its path and reported to nobody"
) {
    const netw_test::FaultWatch faults;
    UnconnectedPair pair;
    Node *hosted = scripted_root(NAMED_CRATE, "Crate");
    pair.mounts[0]->add_child(hosted);
    pair.pump(2);
    pair.connect(0);
    pair.connect(1);
    Node *joined = scripted_root(NAMED_CRATE, "Crate");
    pair.mounts[1]->add_child(joined);
    pair.pump(12);

    const int64_t host_route = route_of(hosted);
    REQUIRE(host_route > 0);
    NETW_CHECK_EQ(route_of(joined), host_route);
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] CN9 an entity placed in a level opened with "
    "Netw.change_scene_to_file reaches a client under the server's route and "
    "state"
) {
    const String path("res://tests/support/crate_level.tscn");
    const Ref<PackedScene> held = netw::gd::load_scene(path);
    REQUIRE(held.is_valid());
    LoopbackRig rig(1);
    rig.mount();
    flow_clocks(rig, 30);
    rig.join(-1, StringName("host"));
    rig.join(0, StringName("guest"));
    const Ref<netw::NetwPromise> changed = netw::Netw::change_scene_to_file(
        rig.branch(-1),
        path,
        netw::NetwMultiplayer::SCENE_CHANGE_SESSION
    );
    rig.step_ticks(8);
    REQUIRE(changed->get_is_completed());
    const Ref<netw::NetwScene> level = changed->get_result();
    REQUIRE(level.is_valid());
    Node *hosted = rig.server()->scene_container(StringName("CrateLevel"));
    REQUIRE(hosted != nullptr);
    Node *crate = descend(hosted, "Crate");
    REQUIRE(route_of(crate) > 0);
    Object::cast_to<CanvasItem>(crate)->set_visible(false);
    rig.step_ticks(12);

    const CanvasItem *seen = Object::cast_to<CanvasItem>(
        rig.route_node(int(route_of(crate)), 0)
    );
    Node *seen_level = rig.route_node(int(route_of(hosted)), 0);
    const bool placed_under_level
        = seen != nullptr && seen->get_parent() == seen_level;
    CHECK(placed_under_level);
    const bool synced = seen != nullptr && !seen->is_visible();
    CHECK(synced);
}

const char *DECLARED_LEVEL = "res://tests/support/declared_level.tscn";
const char *ISOLATED_LEVEL = "res://tests/support/isolated_level.tscn";

Node *crate_in(Node *p_level) {
    return p_level->get_node_or_null(NodePath("Crate"));
}

struct LevelSeat {
    bool kept = false;
    bool bound = false;
    bool same_scene = false;
    bool crate_bound = false;
    bool crate_synced = false;
};

LevelSeat read_level_seat(
    netw::NetwMultiplayer *p_api,
    Node *p_level,
    ObjectID p_placed,
    Node *p_hosted
) {
    LevelSeat seen;
    seen.kept = netw::gd::instance_from_id(p_placed) == p_level
        && p_level->is_inside_tree();
    if (!seen.kept) {
        return seen;
    }
    seen.bound = route_of(p_level) == route_of(p_hosted);
    const Ref<netw::NetwScene> scene = netw::Netw::scene(crate_in(p_level));
    seen.same_scene = scene.is_valid()
        && scene == netw::Netw::scene(p_level)
        && p_api->scene_list().size() == 1
        && RID(p_api->scene_list()[0]) == p_api->entity_of(p_level);
    seen.crate_bound = route_of(crate_in(p_level)) > 0
        && route_of(crate_in(p_level)) == route_of(crate_in(p_hosted));
    const CanvasItem *crate = Object::cast_to<CanvasItem>(crate_in(p_level));
    seen.crate_synced = crate != nullptr && !crate->is_visible();
    return seen;
}

void check_level_seat(const LevelSeat &p_seen) {
    CHECK(p_seen.kept);
    CHECK(p_seen.bound);
    CHECK(p_seen.same_scene);
    CHECK(p_seen.crate_bound);
    CHECK(p_seen.crate_synced);
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] PL1 a declared level placed under the "
    "session root stays the node every peer placed, is one scene on the server "
    "and on a client, and carries its placed entity to a late joiner"
) {
    UnconnectedPair peers(3);
    Node *hosted = peers.place_level(0, DECLARED_LEVEL);
    Node *seated = peers.place_level(1, DECLARED_LEVEL);
    const ObjectID host_placed = netw::gd::instance_id(hosted);
    const ObjectID seat_placed = netw::gd::instance_id(seated);
    peers.pump(2);
    peers.connect(0);
    peers.connect(1);

    netw::NetwMultiplayer *host = peers.apis[0].ptr();
    CHECK(netw::gd::instance_from_id(host_placed) == hosted);
    const RID opened = host->entity_of(hosted);
    REQUIRE(host->scene_list().size() == 1);
    CHECK(RID(host->scene_list()[0]) == opened);
    const Ref<netw::NetwScene> world = netw::Netw::scene(hosted);
    REQUIRE(world.is_valid());
    CHECK(netw::Netw::scene(crate_in(hosted)) == world);
    Object::cast_to<CanvasItem>(crate_in(hosted))->set_visible(false);
    const int seat_peer = peers.apis[1]->get_unique_id();
    NETW_CHECK_EQ(int(host->scene_watch(opened, seat_peer)), int(OK));
    peers.pump(12);
    REQUIRE(route_of(hosted) > 0);
    check_level_seat(
        read_level_seat(peers.apis[1].ptr(), seated, seat_placed, hosted)
    );

    Node *joined = peers.place_level(2, DECLARED_LEVEL);
    const ObjectID late_placed = netw::gd::instance_id(joined);
    peers.pump(2);
    peers.connect(2);
    const int late_peer = peers.apis[2]->get_unique_id();
    NETW_CHECK_EQ(int(host->scene_watch(opened, late_peer)), int(OK));
    peers.pump(12);
    check_level_seat(
        read_level_seat(peers.apis[2].ptr(), joined, late_placed, hosted)
    );
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] PL2 an isolated level placed under the "
    "session root is refused on the server once and sent to no client"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][scene] 'IsolatedLevel' is isolated and was placed in the "
        "editor. Open it with Netw.change_scene_to_file, or turn isolation "
        "off.",
        1
    );
    const netw_test::FaultWatch faults;
    UnconnectedPair peers;
    Node *hosted = peers.place_level(0, ISOLATED_LEVEL);
    Node *seated = peers.place_level(1, ISOLATED_LEVEL);
    peers.pump(2);
    peers.connect(0);
    peers.connect(1);
    peers.pump(6);
    peers.mounts[0]->remove_child(hosted);
    peers.mounts[0]->add_child(hosted);
    hosted->set_owner(peers.mounts[0]);
    peers.pump(6);

    const Dictionary sent
        = Dictionary(peers.apis[0]->spawn_plane()->counters());
    NETW_CHECK_EQ(int64_t(sent[StringName("spawn_book_spawned")]), int64_t(0));
    NETW_CHECK_EQ(route_of(seated), int64_t(0));
    NETW_CHECK_EQ(route_of(crate_in(seated)), int64_t(0));
    NETW_CHECK_EQ(faults.warnings_containing("IsolatedLevel"), int64_t(0));
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] PL3 a client that stops watching a placed "
    "level keeps its own copy, and watching again binds that same level and "
    "its placed entity with the state the server set meanwhile"
) {
    UnconnectedPair peers;
    Node *hosted = peers.place_level(0, DECLARED_LEVEL);
    Node *seated = peers.place_level(1, DECLARED_LEVEL);
    const ObjectID seat_placed = netw::gd::instance_id(seated);
    const ObjectID seat_crate = netw::gd::instance_id(crate_in(seated));
    peers.pump(2);
    peers.connect(0);
    peers.connect(1);
    netw::NetwMultiplayer *host = peers.apis[0].ptr();
    const RID opened = host->entity_of(hosted);
    const int seat_peer = peers.apis[1]->get_unique_id();
    NETW_CHECK_EQ(int(host->scene_watch(opened, seat_peer)), int(OK));
    peers.pump(12);
    REQUIRE(route_of(seated) == route_of(hosted));

    CHECK(host->scene_unwatch(opened, seat_peer));
    peers.pump(12);
    const bool kept_while_hidden
        = netw::gd::instance_from_id(seat_placed) == seated
        && netw::gd::instance_from_id(seat_crate) != nullptr;
    REQUIRE(kept_while_hidden);
    CHECK(seated->is_inside_tree());
    CHECK_FALSE(host->scene_watches(opened, seat_peer));

    Object::cast_to<CanvasItem>(crate_in(hosted))->set_visible(false);
    NETW_CHECK_EQ(int(host->scene_watch(opened, seat_peer)), int(OK));
    peers.pump(12);
    CHECK(host->scene_watches(opened, seat_peer));
    CHECK(netw::gd::instance_from_id(seat_crate) == crate_in(seated));
    check_level_seat(
        read_level_seat(peers.apis[1].ptr(), seated, seat_placed, hosted)
    );
}

} // namespace TestSpawnNestedIdentityLaws

#endif

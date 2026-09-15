#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/script/model.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSpawnNestedIdentityLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

const char *BALL = "ball|0";
const char *SPIN = "spin|0";

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
    NetwEntity::ensure(ball);
    Node2D *spin = rooted(ball, SPIN, arena);
    NetwEntity::ensure(spin);
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
    return p_rig.spawn_registered(
        p_id,
        p_build,
        one_arg(),
        one_type(),
        p_under
    );
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
    out.spin = route_of(descend(p_arena, "ball|0/spin|0"));
    return out;
}

int seat_late(LoopbackRig &p_rig, const PackedInt64Array &p_replay) {
    const int late = p_rig.add_client();
    p_rig.mount_late(late);
    p_rig.register_constructor(
        p_rig.client(late),
        StringName("nested_arena"),
        callable_mp_static(&build_nested_arena),
        one_type()
    );
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
        = rig.route_node(int(host.spin), 0) == descend(seat, "ball|0/spin|0");
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
    LoopbackRig rig(1);
    rig.mount();
    netw::NetwMultiplayer *api = rig.server();
    Node *level = api->scene_spawn(
        String("res://tests/support/declared_level.tscn"),
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

    const int late = seat_late(
        rig,
        rig.spawn_plane()->get_spawn_book()->ancestry_order()
    );

    Node2D *seat_ball = Object::cast_to<Node2D>(
        descend(rig.route_node(arena_route, late), BALL)
    );
    const bool ball_arrived = seat_ball != nullptr;
    REQUIRE(ball_arrived);
    const bool pose_travelled
        = seat_ball->get_position().distance_to(Vector2(12.0, 7.0)) < 0.01;
    CHECK(pose_travelled);
}

} // namespace TestSpawnNestedIdentityLaws

#endif

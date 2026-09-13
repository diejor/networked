#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"

namespace TestSpawnNestingLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

Node *build_player(const Variant &p_peer) {
    Node *made = memnew(Node);
    made->set_name("Player");
    const Ref<NetwEntity> entity = NetwEntity::ensure(made);
    if (entity.is_valid()) {
        entity->set_peer_id(int64_t(p_peer));
    }
    return made;
}

Node *build_body(const Variant &) {
    Node *made = memnew(Node);
    made->set_name("Body");
    return made;
}

struct Nested {
    int parent_route = 0;
    int child_route = 0;
};

Array one_arg() {
    Array out;
    out.push_back(String("body"));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Nested spawn_pair(LoopbackRig &p_rig, Node *p_under = nullptr) {
    Nested out;
    out.parent_route = p_rig.spawn_registered(
        StringName("parent"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        p_under
    );
    out.child_route = p_rig.spawn_registered(
        StringName("child"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        p_rig.route_node(out.parent_route)
    );
    return out;
}

int64_t state_of(LoopbackRig &p_rig, int p_client, int p_route) {
    netw::NetwMultiplayer *api = p_rig.client(p_client);
    return int64_t(api->entity_get_state(api->entity_from_route(p_route)));
}

TEST_CASE(
    "[Networked][Spawn] SN1 a child spawned under a parent entity arrives "
    "under the RECEIVER's own parent instance, not the sender's"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Nested pair = spawn_pair(rig);

    Node *seat_parent = rig.route_node(pair.parent_route, 0);
    Node *seat_child = rig.route_node(pair.child_route, 0);
    const bool parent_arrived = seat_parent != nullptr;
    const bool child_arrived = seat_child != nullptr;
    CHECK(parent_arrived);
    CHECK(child_arrived);
    if (!parent_arrived || !child_arrived) {
        return;
    }

    const bool nested = seat_child->get_parent() == seat_parent;
    CHECK(nested);

    const bool own_instance = seat_parent != rig.route_node(pair.parent_route);
    CHECK(own_instance);
}

TEST_CASE(
    "[Networked][Spawn] SNP a child frame that arrives before its parent "
    "PARKS, because a child has nowhere to go until its parent exists"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Nested pair = spawn_pair(rig);

    const PackedByteArray child_frame = rig.spawn_frame_of(pair.child_route);

    const int late = rig.add_client();
    rig.hold(late);
    rig.mount_late(late);

    rig.deliver_spawn(late, child_frame);

    const Dictionary parked = Dictionary(rig.spawn_plane(late)->counters());
    NETW_CHECK_EQ(int64_t(parked[StringName("spawn_deferrals")]), int64_t(1));
    NETW_CHECK_EQ(
        int64_t(parked[StringName("drops_spawn_unresolved")]),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Spawn] SN2 a spawn frame for a route this peer already holds "
    "is an idempotent drop, counted, because a duplicate is normal at a "
    "visibility edge and never an error"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Nested pair = spawn_pair(rig);

    const PackedByteArray frame = rig.spawn_frame_of(pair.parent_route);
    const Dictionary before = Dictionary(rig.spawn_plane(0)->counters());
    const int64_t duplicates
        = int64_t(before[StringName("drops_spawn_duplicate")]);

    rig.deliver_spawn(0, frame);

    const Dictionary after = Dictionary(rig.spawn_plane(0)->counters());
    NETW_CHECK_EQ(
        int64_t(after[StringName("drops_spawn_duplicate")]),
        duplicates + 1
    );
    NETW_CHECK_EQ(
        state_of(rig, 0, pair.parent_route),
        int64_t(netw::NetwLivenessCore::STATE_LIVE)
    );
}

TEST_CASE(
    "[Networked][Spawn] SN3 a spawn frame from a sender that is not the "
    "server is refused and counted, whatever it names"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Nested pair = spawn_pair(rig);

    const PackedByteArray frame = rig.spawn_frame_of(pair.parent_route);
    netw::spawn::Pipeline *pipeline = rig.spawn_plane(0);
    REQUIRE(pipeline != nullptr);

    const Dictionary before = Dictionary(rig.spawn_plane(0)->counters());
    const int64_t refused
        = int64_t(before[StringName("drops_spawn_bad_sender")]);

    pipeline->handle_spawn_frame(frame, 2);

    const Dictionary after = Dictionary(rig.spawn_plane(0)->counters());
    NETW_CHECK_EQ(
        int64_t(after[StringName("drops_spawn_bad_sender")]),
        refused + 1
    );
}

TEST_CASE(
    "[Networked][Spawn] SN4 a despawn that arrives while a spawn is parked "
    "cancels it with NET RESULT ZERO, so the parent lands holding nothing"
) {
    LoopbackRig rig(1);
    rig.mount();
    rig.hold(0);
    const Nested pair = spawn_pair(rig, rig.mirror_child("Arena"));

    const PackedByteArray parent_frame = rig.spawn_frame_of(pair.parent_route);
    const PackedByteArray child_frame = rig.spawn_frame_of(pair.child_route);

    rig.deliver_spawn(0, child_frame);
    rig.deliver_despawn(0, pair.child_route);

    const Dictionary cancelled = Dictionary(rig.spawn_plane(0)->counters());
    NETW_CHECK_EQ(
        int64_t(cancelled[StringName("spawn_parked_cancelled")]),
        int64_t(1)
    );

    rig.deliver_spawn(0, parent_frame);

    Node *seat_parent = rig.route_node(pair.parent_route, 0);
    const bool parent_landed = seat_parent != nullptr;
    CHECK(parent_landed);
    if (parent_landed) {
        NETW_CHECK_EQ(seat_parent->get_child_count(), 0);
    }
    NETW_CHECK_EQ(
        state_of(rig, 0, pair.child_route),
        int64_t(netw::NetwLivenessCore::STATE_UNKNOWN)
    );
}

TEST_CASE(
    "[Networked][Spawn] SN5 a move keeps the route and the receiver's own "
    "instance and MOVES it, because a cross-parent move is a reparent and "
    "not a despawn followed by a respawn"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *home = rig.mirror_child("Arena");
    rig.mirror_child("Arena2");

    const int route = rig.spawn_registered(
        StringName("mover"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        home
    );
    Node *seat_before = rig.route_node(route, 0);
    const bool arrived = seat_before != nullptr;
    CHECK(arrived);
    if (!arrived) {
        return;
    }

    Node *host_node = rig.route_node(route);
    Node *away = rig.branch(-1)->get_node_or_null(NodePath("Arena2"));
    REQUIRE(away != nullptr);
    host_node->get_parent()->remove_child(host_node);
    away->add_child(host_node);
    rig.pump(10);

    Node *seat_after = rig.route_node(route, 0);
    const bool same_instance = seat_after == seat_before;
    CHECK(same_instance);
    if (!same_instance || seat_after == nullptr) {
        return;
    }

    Node *seat_parent = seat_after->get_parent();
    const bool moved = seat_parent != nullptr
        && seat_parent->get_name() == StringName("Arena2");
    CHECK(moved);

    NETW_CHECK_EQ(
        state_of(rig, 0, route),
        int64_t(netw::NetwLivenessCore::STATE_LIVE)
    );
}

TEST_CASE(
    "[Networked][Spawn] SN6 the ancestry chain follows the TREE, so a route "
    "with no entity above it answers nothing and a move re-reads it"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Nested pair = spawn_pair(rig);

    netw::NetwMultiplayer *api = rig.server();
    const RID parent = api->entity_from_route(pair.parent_route);
    const RID child = api->entity_from_route(pair.child_route);

    const bool child_names_parent = api->entity_get_parent(child) == parent;
    CHECK(child_names_parent);
    const bool parent_names_nothing
        = !api->entity_get_parent(parent).is_valid();
    CHECK(parent_names_nothing);

    Node *child_node = rig.route_node(pair.child_route);
    child_node->get_parent()->remove_child(child_node);
    rig.branch(-1)->add_child(child_node);

    const bool moved_names_nothing = !api->entity_get_parent(child).is_valid();
    CHECK(moved_names_nothing);
}

TEST_CASE(
    "[Networked][Spawn] SN7 a parent despawn cascades to what it contains, "
    "and the seat counts no unknown despawn, because a child leaves before "
    "the ancestor that named it"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Nested pair = spawn_pair(rig, rig.mirror_child("Arena"));

    const bool child_arrived = rig.route_node(pair.child_route, 0) != nullptr;
    CHECK(child_arrived);

    const Dictionary before = Dictionary(rig.spawn_plane(0)->counters());
    const int64_t unknown
        = int64_t(before[StringName("drops_despawn_unknown")]);

    Node *host_parent = rig.route_node(pair.parent_route);
    host_parent->get_parent()->remove_child(host_parent);
    memdelete(host_parent);
    rig.pump(8);

    NETW_CHECK_EQ(
        state_of(rig, 0, pair.parent_route),
        int64_t(netw::NetwLivenessCore::STATE_DEAD)
    );
    NETW_CHECK_EQ(
        state_of(rig, 0, pair.child_route),
        int64_t(netw::NetwLivenessCore::STATE_DEAD)
    );

    const Dictionary after = Dictionary(rig.spawn_plane(0)->counters());
    NETW_CHECK_EQ(int64_t(after[StringName("drops_despawn_unknown")]), unknown);
}

TEST_CASE(
    "[Networked][Spawn] SN8 an entity nobody represents names peer zero, and "
    "one spawned FOR a peer names that peer on every copy of it"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Nested pair = spawn_pair(rig, arena);

    netw::NetwMultiplayer *api = rig.server();
    const RID owned = api->entity_from_route(pair.parent_route);
    NETW_CHECK_EQ(api->entity_get_peer(owned), int64_t(0));

    Array peer_arg;
    peer_arg.push_back(rig.peer_id(0));
    Array peer_type;
    peer_type.push_back(int(Variant::INT));
    const int player_route = rig.spawn_registered(
        StringName("player"),
        callable_mp_static(&build_player),
        peer_arg,
        peer_type,
        arena
    );

    const RID represented = api->entity_from_route(player_route);
    NETW_CHECK_EQ(api->entity_get_peer(represented), int64_t(rig.peer_id(0)));

    const Ref<NetwEntity> seat
        = NetwEntity::of(rig.route_node(player_route, 0));
    CHECK(seat.is_valid());
    if (seat.is_valid()) {
        NETW_CHECK_EQ(seat->get_peer_id(), int64_t(rig.peer_id(0)));
    }
}

TEST_CASE(
    "[Networked][Spawn] SN9 the world viewport an isolated scene owns is "
    "inserted by the peer that spawns it and never by a receiver, because "
    "only the spawning peer presents that world and routes input into it"
) {
    LoopbackRig rig(1);
    rig.mount();
    netw::NetwMultiplayer *api = rig.server();
    Node *authored = api->scene_spawn(
        String("res://tests/support/declared_level.tscn"),
        netw::NetwMultiplayer::SCENE_ISOLATION_OWN_WORLD
    );
    const bool spawned = authored != nullptr;
    REQUIRE(spawned);
    rig.pump(6);

    const RID scene = api->entity_of(authored);
    api->scene_admit(scene, rig.peer_id(0));
    rig.pump(6);

    const int route = int(api->entity_get_route(scene));
    NETW_CHECK_GT(route, 0);
    Node *received = rig.route_node(route, 0);
    const bool arrived = received != nullptr && received != authored;
    CHECK(arrived);

    if (arrived) {
        const bool authored_owns_a_world
            = netw::NetwMultiplayer::scene_world_of(authored) != nullptr;
        const bool received_shares_the_window
            = netw::NetwMultiplayer::scene_world_of(received) == nullptr;
        CHECK(authored_owns_a_world);
        CHECK(received_shares_the_window);
    }

    Node *outer = netw::NetwMultiplayer::scene_outer_of(authored);
    outer->get_parent()->remove_child(outer);
    memdelete(outer);
}

} // namespace TestSpawnNestingLaws

#endif

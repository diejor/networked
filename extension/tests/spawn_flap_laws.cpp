#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/interest/decl.hpp"
#include "netw/liveness_core.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSpawnFlapLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwLivenessCore;
using netw::NetwMultiplayer;
using netw::interest::Decl;

const char *FLAP_ID = "flap_body";

Node *build_flap_body(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
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

int64_t state_of(LoopbackRig &p_rig, int p_client, int p_route) {
    NetwMultiplayer *api
        = p_client < 0 ? p_rig.server() : p_rig.client(p_client);
    return int64_t(api->entity_get_state(api->entity_from_route(p_route)));
}

struct Flapped {
    int route = 0;
    Ref<NetwEntity> entity;
    Ref<NetwInterestLayer> layer;
};

Flapped a_hidden_body(
    LoopbackRig &p_rig,
    Node *p_parent,
    const char *p_layer,
    int p_leave_policy
) {
    p_rig.register_constructor(
        p_rig.server(),
        StringName(FLAP_ID),
        callable_mp_static(&build_flap_body),
        one_type()
    );
    for (int at = 0; at < p_rig.count(); ++at) {
        p_rig.register_constructor(
            p_rig.client(at),
            StringName(FLAP_ID),
            callable_mp_static(&build_flap_body),
            one_type()
        );
    }

    Flapped made;
    made.route = p_rig.spawn_registered(
        StringName(FLAP_ID),
        callable_mp_static(&build_flap_body),
        named("Flapper"),
        one_type(),
        p_parent,
        Variant(),
        false
    );
    made.entity = NetwEntity::of(p_rig.route_node(made.route));
    REQUIRE(made.entity.is_valid());

    made.layer = p_rig.server()->interest_layer(StringName(p_layer));
    REQUIRE(made.layer.is_valid());
    made.layer->set_default_leave_policy(p_leave_policy);
    made.layer->add_entity(made.entity);
    p_rig.flush_interest();
    p_rig.pump(10);
    return made;
}

TEST_CASE(
    "[Networked][Spawn] IF1 a route the peer lost and was re-admitted to comes "
    "back LIVE rather than staying dead, because an admission is a fresh "
    "answer about the entity and not a memory of the last one"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made = a_hidden_body(rig, arena, "flap", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);

    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_UNKNOWN)
    );

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_LIVE)
    );

    made.layer->remove_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_ABSENT)
    );

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_LIVE)
    );
    CHECK(rig.route_node(made.route, 0) != nullptr);
}

TEST_CASE(
    "[Networked][Spawn] IF2 a RETAIN layer leaves the peer holding the very "
    "instance it already had when visibility goes, so a retained copy is the "
    "same body rather than a rebuilt one"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made
        = a_hidden_body(rig, arena, "policy", Decl::LEAVE_RETAIN);
    const int64_t watcher = rig.peer_id(0);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    Node *seated = rig.route_node(made.route, 0);
    REQUIRE_MESSAGE(seated != nullptr, "the admission never reached the peer");

    made.layer->remove_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);

    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_LIVE)
    );
    CHECK(rig.route_node(made.route, 0) == seated);
}

TEST_CASE(
    "[Networked][Spawn] IF3 a HIDE layer ends the copy on the same loss a "
    "RETAIN layer survives, which is the whole difference the policy names"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made = a_hidden_body(rig, arena, "ending", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    REQUIRE(rig.route_node(made.route, 0) != nullptr);

    made.layer->remove_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);

    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_ABSENT)
    );
    CHECK(rig.route_node(made.route, 0) == nullptr);
    NETW_CHECK_EQ(
        state_of(rig, -1, made.route),
        int64_t(NetwLivenessCore::STATE_LIVE)
    );
}

TEST_CASE(
    "[Networked][Spawn] IF4 a hidden route keeps naming its entity, so a "
    "re-admission binds the same handle rather than minting a second identity "
    "for one route"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made = a_hidden_body(rig, arena, "naming", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    const RID handle = rig.client(0)->entity_from_route(made.route);
    REQUIRE(handle.is_valid());

    made.layer->remove_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    CHECK(rig.client(0)->entity_from_route(made.route) == handle);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    CHECK(rig.client(0)->entity_from_route(made.route) == handle);
    NETW_CHECK_EQ(rig.client(0)->entity_get_epoch(handle), 0);
}

TEST_CASE(
    "[Networked][Spawn] IF5 a real despawn of a route the peer holds still "
    "tombstones, which is the whole difference the two channels name"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made = a_hidden_body(rig, arena, "ending", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    REQUIRE(rig.route_node(made.route, 0) != nullptr);

    Node *body = rig.route_node(made.route);
    body->get_parent()->remove_child(body);
    rig.pump(10);

    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_DEAD)
    );
    CHECK(rig.route_node(made.route, 0) == nullptr);

    memdelete(body);
}

TEST_CASE(
    "[Networked][Spawn] IF6 a peer holding no copy is sent no despawn when "
    "the entity dies, so a death while hidden costs nothing on either side"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made = a_hidden_body(rig, arena, "quiet", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    made.layer->remove_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_ABSENT)
    );

    Node *body = rig.route_node(made.route);
    body->get_parent()->remove_child(body);
    rig.pump(10);

    NETW_CHECK_EQ(
        state_of(rig, -1, made.route),
        int64_t(NetwLivenessCore::STATE_DEAD)
    );
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_ABSENT)
    );

    memdelete(body);
}

TEST_CASE(
    "[Networked][Spawn] IF7 an interest path that re-adopts the wrapper a "
    "hide dropped does not lock the route out, because the adoption guard "
    "asks presence rather than identity"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped made = a_hidden_body(rig, arena, "readopt", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    const Ref<NetwEntity> stale = NetwEntity::of(rig.route_node(made.route, 0));
    REQUIRE(stale.is_valid());

    made.layer->remove_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    rig.client(0)->liveness_adopt(stale.ptr());
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_ABSENT)
    );

    made.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);
    NETW_CHECK_EQ(
        state_of(rig, 0, made.route),
        int64_t(NetwLivenessCore::STATE_LIVE)
    );
    CHECK(rig.route_node(made.route, 0) != nullptr);
}

int64_t counter_of(LoopbackRig &p_rig, int p_client, const char *p_key) {
    return int64_t(
        p_rig.spawn_plane(p_client)->counters().get(StringName(p_key), 0)
    );
}

TEST_CASE(
    "[Networked][Spawn] IF8 a frame whose anchor is absent parks for the "
    "anchor's return, and a frame whose anchor is a tombstone is dropped in "
    "the frame that carries it rather than held for the park timeout"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Flapped anchor
        = a_hidden_body(rig, arena, "anchoring", Decl::LEAVE_HIDE);
    const int64_t watcher = rig.peer_id(0);
    anchor.layer->add_viewer(watcher);
    rig.flush_interest();
    rig.pump(10);

    const int child = rig.spawn_registered(
        StringName(FLAP_ID),
        callable_mp_static(&build_flap_body),
        named("Hanger"),
        one_type(),
        rig.route_node(anchor.route),
        Variant(),
        false
    );
    rig.pump(10);
    const PackedByteArray held = rig.spawn_frame_of(child);
    REQUIRE_FALSE(held.is_empty());

    SUBCASE("an absent anchor parks the frame") {
        anchor.layer->remove_viewer(watcher);
        rig.flush_interest();
        rig.pump(10);
        NETW_CHECK_EQ(
            state_of(rig, 0, anchor.route),
            int64_t(NetwLivenessCore::STATE_ABSENT)
        );

        const int64_t dropped = counter_of(rig, 0, "drops_spawn_unresolved");
        rig.deliver_spawn(0, held);
        NETW_CHECK_EQ(rig.spawn_plane(0)->get_park().size(), 1);
        NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_unresolved"), dropped);
    }

    SUBCASE("a tombstoned anchor drops the frame at once") {
        Node *body = rig.route_node(anchor.route);
        body->get_parent()->remove_child(body);
        rig.pump(10);
        NETW_CHECK_EQ(
            state_of(rig, 0, anchor.route),
            int64_t(NetwLivenessCore::STATE_DEAD)
        );

        const int64_t dropped = counter_of(rig, 0, "drops_spawn_unresolved");
        rig.deliver_spawn(0, held);
        NETW_CHECK_EQ(rig.spawn_plane(0)->get_park().size(), 0);
        NETW_CHECK_EQ(
            counter_of(rig, 0, "drops_spawn_unresolved"),
            dropped + 1
        );

        memdelete(body);
    }
}

TEST_CASE(
    "[Networked][Spawn] IF9 a peer is never hidden from an entity it "
    "controls while that entity's anchor is visible to it, because a mover "
    "cannot be told its own body is gone"
) {
    LoopbackRig rig(1);
    rig.mount();
    rig.join(0, StringName("driver"));
    Node *arena = rig.mirror_child("Arena");

    const int route = rig.spawn_registered(
        StringName(FLAP_ID),
        callable_mp_static(&build_flap_body),
        named("Driven"),
        one_type(),
        arena,
        rig.participant(0),
        false
    );
    rig.pump(10);
    const Ref<NetwEntity> driven = NetwEntity::of(rig.route_node(route));
    REQUIRE(driven.is_valid());
    NETW_CHECK_EQ(driven->get_controller(), int64_t(rig.peer_id(0)));
    REQUIRE(rig.route_node(route, 0) != nullptr);

    const Ref<NetwInterestLayer> layer
        = rig.server()->interest_layer(StringName("elsewhere"));
    REQUIRE(layer.is_valid());
    layer->set_default_leave_policy(Decl::LEAVE_HIDE);
    layer->add_entity(driven);
    layer->add_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);

    layer->remove_viewer(rig.peer_id(0));
    rig.flush_interest();
    rig.pump(10);

    NETW_CHECK_EQ(
        state_of(rig, 0, route),
        int64_t(NetwLivenessCore::STATE_LIVE)
    );
    CHECK(rig.route_node(route, 0) != nullptr);
}

} // namespace TestSpawnFlapLaws

#endif

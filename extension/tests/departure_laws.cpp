#include "support/loopback_rig.h"
#include "support/netw_recorder.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestDeparture {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;

const char *BODY_ID = "departure_body";

constexpr int SETTLE_PUMPS = 8;
constexpr int WATCH_PUMPS = 30;

Node *build_body(const Variant &p_name) {
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

void teach_constructor(LoopbackRig &p_rig, NetwMultiplayer *p_api) {
    p_rig.register_constructor(
        p_api,
        StringName(BODY_ID),
        callable_mp_static(&build_body),
        one_type()
    );
}

int seat_body(LoopbackRig &p_rig, Node *p_parent, const char *p_name) {
    teach_constructor(p_rig, p_rig.server());
    for (int at = 0; at < p_rig.count(); ++at) {
        teach_constructor(p_rig, p_rig.client(at));
    }
    const RID entity = p_rig.server()->spawn_registered(
        StringName(BODY_ID),
        named(p_name),
        p_rig.participant(0).ptr()
    );
    REQUIRE_MESSAGE(entity.is_valid(), "the spawn verb minted no entity");
    Node *node = p_rig.server()->entity_get_node(entity);
    REQUIRE_MESSAGE(node != nullptr, "the spawn verb built no node");
    p_parent->add_child(node);
    p_rig.pump(SETTLE_PUMPS);
    return int(p_rig.server()->entity_get_route(entity));
}

struct Streaming {
    LoopbackRig rig;
    Node *arena = nullptr;
    int route = 0;
    RID streamed;

    Streaming() : rig(2) {
        rig.mount();
        arena = rig.mirror_child("Arena");
        rig.join(0, StringName("alpha"));
        rig.join(1, StringName("beta"));
        route = seat_body(rig, arena, "BodyOne");
        rig.declare_world(
            WorldDecl().clocked(30, 3).entity(
                EntityDecl()
                    .named("Drifter")
                    .on_schema("DrifterPose")
                    .synced("position")
                    .placed_at(Vector2())
                    .mounted()
            )
        );
        streamed = rig.entity_of(StringName("Drifter"));
        rig.step_ticks(SETTLE_PUMPS);
    }

    void stream(int p_pumps) {
        Node2D *body
            = Object::cast_to<Node2D>(rig.server()->entity_get_node(streamed));
        for (int at = 0; at < p_pumps; ++at) {
            if (body != nullptr) {
                body->set_position(Vector2(real_t(at), real_t(at)));
            }
            rig.step_ticks(1);
        }
    }

    Ref<NetwEntity> body() const {
        Node *node = rig.route_node(route, -1);
        return node == nullptr ? Ref<NetwEntity>() : NetwEntity::of(node);
    }

    bool addresses(int64_t p_peer) const {
        return rig.server()->rpc_get_recipients(body()).has(int32_t(p_peer));
    }

    bool streams() {
        rig.clear_refused_sends();
        stream(4);
        return rig.delivered_sends_at_server() > 0;
    }

    bool rosters(int64_t p_peer) const {
        return netw::gd::api_peer_ids(rig.server()->session_get_inner())
            .has(int32_t(p_peer));
    }
};

TEST_CASE(
    "[Networked][Session][SceneTree] DP1 a peer that leaves gracefully "
    "is addressed no more, because a roster the transport has already torn "
    "down is not a list of who can receive"
) {
    Streaming world;
    const int64_t departing = world.rig.peer_id(0);
    REQUIRE(world.addresses(departing));
    REQUIRE(world.streams());

    world.rig.drop_client(0);
    world.stream(2);
    world.rig.clear_refused_sends();
    world.stream(WATCH_PUMPS);

    CHECK_FALSE(world.addresses(departing));
    CHECK_FALSE(world.rosters(departing));
    NETW_CHECK_EQ(world.rig.refused_sends_at_server(), 0);
}

TEST_CASE(
    "[Networked][Session][SceneTree] DP2 a peer dropped with force is "
    "addressed no more either, and this is the row that matters, because "
    "MultiplayerPeer.disconnect_peer documents that force emits no "
    "peer_disconnected and every teardown here hangs off that signal"
) {
    Streaming world;
    const int64_t departing = world.rig.peer_id(0);
    REQUIRE(world.addresses(departing));
    REQUIRE(world.streams());

    world.rig.drop_client(0, true);
    world.stream(2);
    world.rig.clear_refused_sends();
    world.stream(WATCH_PUMPS);

    CHECK_FALSE(world.addresses(departing));
    NETW_CHECK_EQ(world.rig.refused_sends_at_server(), 0);
}

TEST_CASE(
    "[Networked][Session][SceneTree] DP3 a peer that stays is "
    "addressed exactly as before its neighbour left, so the retirement above "
    "is a departure and not a stall"
) {
    Streaming world;
    const int64_t staying = world.rig.peer_id(1);

    world.rig.drop_client(0);
    world.stream(4);

    CHECK(world.addresses(staying));
    CHECK(world.rosters(staying));
}

TEST_CASE(
    "[Networked][Session][SceneTree] DP4 a departure is announced "
    "exactly once however it is learned, because a refused send and the "
    "transport's own signal describe one event and a game counts what it is "
    "told"
) {
    Streaming world;
    const int64_t departing = world.rig.peer_id(0);
    REQUIRE(world.streams());

    Recorder seen(world.rig.server(), {"peer_disconnected"});

    world.rig.drop_client(0);
    world.stream(WATCH_PUMPS);

    NETW_CHECK_EQ(seen.count("peer_disconnected"), 1);
    CHECK_FALSE(world.addresses(departing));
}

TEST_CASE(
    "[Networked][Session][SceneTree] DP5 a fault the transport "
    "reports for the whole link retires nobody, because one condition that "
    "fails every send at once is the session ending and not every peer "
    "leaving at the same instant"
) {
    Streaming world;
    REQUIRE(world.streams());

    Recorder seen(world.rig.server(), {"peer_disconnected"});
    world.rig.close_link();
    world.stream(WATCH_PUMPS);

    NETW_CHECK_EQ(seen.count("peer_disconnected"), 0);
}

} // namespace TestDeparture

#endif

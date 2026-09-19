#include "support/loopback_rig.h"
#include "support/netw_recorder.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestLossSettlement {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;

const char *BODY_ID = "settlement_body";

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
    p_rig.join(0, StringName("alpha"));
    const RID entity = p_rig.server()->spawn_registered(
        StringName(BODY_ID),
        named(p_name),
        p_rig.player(0).ptr()
    );
    REQUIRE_MESSAGE(entity.is_valid(), "the spawn verb minted no entity");
    Node *node = p_rig.server()->entity_get_node(entity);
    REQUIRE_MESSAGE(node != nullptr, "the spawn verb built no node");
    p_parent->add_child(node);
    p_rig.pump(8);
    return int(p_rig.server()->entity_get_route(entity));
}

Ref<NetwEntity> entity_at(LoopbackRig &p_rig, int p_route, int p_client) {
    Node *node = p_rig.route_node(p_route, p_client);
    return node == nullptr ? Ref<NetwEntity>() : NetwEntity::of(node);
}

Array rows_of(const Array &p_drained, int64_t p_event) {
    Array kept;
    for (int at = 0; at < p_drained.size(); ++at) {
        const Dictionary row = p_drained[at];
        if (!row.is_empty()
            && int64_t(row[netw::event_key::event()]) == p_event) {
            kept.push_back(row);
        }
    }
    return kept;
}

TEST_CASE(
    "[Networked][Session][SceneTree] L1 a coordinator that leaves takes the "
    "session down even though transport peer 1 stays connected, because the "
    "relay staying up answers nothing about who was deciding"
) {
    LoopbackRig rig(1, 7);
    rig.mount();
    REQUIRE(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_ONLINE
    );

    Recorder seen(rig.server(), { "session_ended" });
    rig.server()->session_relay_peer_disconnected(7);

    NETW_CHECK_EQ(seen.count("session_ended"), 1);
    CHECK(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_OFFLINE
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] L2 transport peer 1 leaving under a "
    "coordinator of 7 is an ordinary departure settled once, because peer 1 "
    "is not the peer this session asks for authority"
) {
    LoopbackRig rig(1, 7);
    rig.mount();
    REQUIRE(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_ONLINE
    );

    Recorder seen(rig.server(), { "session_ended", "peer_disconnected" });
    rig.server()->session_relay_peer_disconnected(1);

    NETW_CHECK_EQ(seen.count("session_ended"), 0);
    NETW_CHECK_EQ(seen.count("peer_disconnected"), 1);
    CHECK(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_ONLINE
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] L3 a controller that departs through "
    "the real transport signal reverts its entity exactly once, because "
    "settlement invokes the entity consequence directly rather than a "
    "per-entity subscription that a duplicated signal could fire twice"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const int route = seat_body(rig, arena, "Steered");
    const Ref<NetwEntity> host = entity_at(rig, route, -1);
    REQUIRE(host.is_valid());
    const int64_t controller = rig.peer_id(1);
    host->grant_control(controller);
    rig.pump(8);
    NETW_CHECK_EQ(host->get_controller(), controller);

    rig.server()->event_arm(true);
    rig.server()->event_ring(route);
    rig.drop_client(1);
    rig.pump(8);

    const Array settled = rig.server()->event_ring(route);
    const Array reverted = rows_of(settled, netw::EventPlane::CONTROL_CHANGED);
    NETW_CHECK_EQ(reverted.size(), 1);
    NETW_CHECK_EQ(host->get_controller(), int64_t(0));
    rig.server()->event_arm(false);
}

TEST_CASE(
    "[Networked][Session][SceneTree] L4 a coordinator's loss settles once "
    "even when the same departure reaches settlement twice through two "
    "different entries, because a duplicate report and a reentrant terminal "
    "leave describe one session ending and not two"
) {
    LoopbackRig rig(1, 7);
    rig.mount();

    Recorder seen(rig.server(), { "session_ended" });
    rig.server()->session_relay_peer_disconnected(7);
    rig.server()->session_relay_peer_disconnected(7);
    rig.server()->peer_mark_unreachable(7);

    NETW_CHECK_EQ(seen.count("session_ended"), 1);
    CHECK(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_OFFLINE
    );
}

NetwMultiplayer *&leaving_session() {
    static NetwMultiplayer *held = nullptr;
    return held;
}

void leave_on_departure(int64_t) {
    NetwMultiplayer *session = leaving_session();
    if (session != nullptr) {
        session->session_leave();
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] L5 a departure handler that starts "
    "leaving cannot cancel the coordinator's terminal settlement, because "
    "the loss edge classifies the departure before any world consequence "
    "runs rather than re-reading the state the handler just moved"
) {
    LoopbackRig rig(1, 7);
    rig.mount();
    REQUIRE(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_ONLINE
    );

    leaving_session() = rig.server();
    rig.server()->connect(
        StringName("peer_disconnected"),
        callable_mp_static(&leave_on_departure)
    );

    rig.server()->session_relay_peer_disconnected(7);
    leaving_session() = nullptr;

    CHECK(
        rig.server()->session_get_state()
        == NetwMultiplayer::SESSION_STATE_OFFLINE
    );
}

} // namespace TestLossSettlement

#endif

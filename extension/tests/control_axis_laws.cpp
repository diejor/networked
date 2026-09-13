#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/entity/control.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestControlAxis {

using namespace godot;
using namespace netw_test;
using netw::NetwControlRequest;
using netw::NetwEntity;
using netw::NetwMultiplayer;

const char *PLAYER_ID = "control_player";

Node *build_player(const Variant &p_name) {
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

void deny_every_request(int64_t, Object *p_request) {
    NetwControlRequest *asked = Object::cast_to<NetwControlRequest>(p_request);
    if (asked != nullptr) {
        asked->deny();
    }
}

void teach_constructor(LoopbackRig &p_rig, NetwMultiplayer *p_api) {
    p_rig.register_constructor(
        p_api,
        StringName(PLAYER_ID),
        callable_mp_static(&build_player),
        one_type()
    );
}

int seat_player(LoopbackRig &p_rig, Node *p_parent, const char *p_name) {
    teach_constructor(p_rig, p_rig.server());
    for (int at = 0; at < p_rig.count(); ++at) {
        teach_constructor(p_rig, p_rig.client(at));
    }
    p_rig.join(0, StringName("alpha"));
    const RID entity = p_rig.server()->spawn_registered(
        StringName(PLAYER_ID),
        named(p_name),
        p_rig.participant(0).ptr()
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
    "[Networked][Entity] CA1 a granted request moves the controller and the "
    "node authority as one act on BOTH peers, so neither half of a transfer "
    "can land without the other"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = seat_player(rig, arena, "Steerable");
    const Ref<NetwEntity> host = entity_at(rig, route, -1);
    REQUIRE(host.is_valid());
    host->set_transfer(int64_t(netw::entity::Control::Transfer::REQUESTABLE));

    const Ref<NetwEntity> asking = entity_at(rig, route, 1);
    REQUIRE_MESSAGE(asking.is_valid(), "the spawn never reached the asker");
    const int64_t asker = rig.peer_id(1);

    asking->request_control();
    rig.pump(10);

    NETW_CHECK_EQ(host->get_controller(), asker);
    NETW_CHECK_EQ(
        rig.route_node(route, -1)->get_multiplayer_authority(),
        asker
    );
    NETW_CHECK_EQ(rig.route_node(route, 1)->get_multiplayer_authority(), asker);
    NETW_CHECK_EQ(entity_at(rig, route, 1)->get_controller(), asker);
}

TEST_CASE(
    "[Networked][Entity] CA2 a request the server denies steers nobody, and "
    "neither does one against an entity that never offered the transfer"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = seat_player(rig, arena, "Refusing");
    const Ref<NetwEntity> host = entity_at(rig, route, -1);
    REQUIRE(host.is_valid());
    const int64_t represented = rig.peer_id(0);
    NETW_CHECK_EQ(host->get_controller(), represented);

    host->set_transfer(int64_t(netw::entity::Control::Transfer::REQUESTABLE));
    host->connect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );

    entity_at(rig, route, 1)->request_control();
    rig.pump(10);

    NETW_CHECK_EQ(host->get_controller(), represented);
    NETW_CHECK_EQ(
        rig.route_node(route, -1)->get_multiplayer_authority(),
        represented
    );

    host->disconnect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
    host->set_transfer(int64_t(netw::entity::Control::Transfer::FIXED));

    entity_at(rig, route, 1)->request_control();
    rig.pump(10);

    NETW_CHECK_EQ(host->get_controller(), represented);
    NETW_CHECK_EQ(
        rig.route_node(route, 1)->get_multiplayer_authority(),
        represented
    );
}

TEST_CASE(
    "[Networked][Entity] CA3 a watched session names both halves of a "
    "transfer, and a refused request still rides a row of its own carrying "
    "the refusal rather than steering nothing silently"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    rig.server()->event_arm(true);
    const int route = seat_player(rig, arena, "Watched");
    const Ref<NetwEntity> host = entity_at(rig, route, -1);
    REQUIRE(host.is_valid());
    host->set_transfer(int64_t(netw::entity::Control::Transfer::REQUESTABLE));

    const int64_t asker = rig.peer_id(1);
    const int64_t represented = rig.peer_id(0);

    rig.server()->event_ring(route);
    entity_at(rig, route, 1)->request_control();
    rig.pump(10);

    const Array after_grant = rig.server()->event_ring(route);
    const Array granted
        = rows_of(after_grant, netw::EventPlane::CONTROL_REQUESTED);
    REQUIRE(granted.size() == 1);
    const Dictionary asked = granted[0];
    NETW_CHECK_EQ(int64_t(asked[netw::event_key::verdict()]), int64_t(OK));
    const Dictionary asked_detail = asked[netw::event_key::detail()];
    NETW_CHECK_EQ(
        int64_t(asked_detail.get(StringName("requester"), -1)),
        asker
    );

    const Array moved = rows_of(after_grant, netw::EventPlane::CONTROL_CHANGED);
    REQUIRE(moved.size() == 1);
    const Dictionary change = moved[0];
    const Dictionary change_detail = change[netw::event_key::detail()];
    NETW_CHECK_EQ(
        int64_t(change_detail.get(StringName("from"), -1)),
        represented
    );
    NETW_CHECK_EQ(int64_t(change_detail.get(StringName("to"), -1)), asker);

    host->connect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
    entity_at(rig, route, 1)->request_control();
    rig.pump(10);

    const Array after_denial = rig.server()->event_ring(route);
    const Array refused
        = rows_of(after_denial, netw::EventPlane::CONTROL_REQUESTED);
    REQUIRE(refused.size() == 1);
    const Dictionary denial = refused[0];
    NETW_CHECK_EQ(
        int64_t(denial[netw::event_key::verdict()]),
        int64_t(ERR_UNAUTHORIZED)
    );
    NETW_CHECK_EQ(
        rows_of(after_denial, netw::EventPlane::CONTROL_CHANGED).size(),
        0
    );
    NETW_CHECK_EQ(host->get_controller(), asker);

    host->disconnect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
    rig.server()->event_arm(false);
}

TEST_CASE(
    "[Networked][Entity] CA4 a controller that drops returns the entity to "
    "the server by default and ends it when the entity asked for that, "
    "because a steered body outlives its steerer only if it was told to"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = seat_player(rig, arena, "Steered");
    const Ref<NetwEntity> host = entity_at(rig, route, -1);
    REQUIRE(host.is_valid());
    const int64_t controller = rig.peer_id(1);

    host->grant_control(controller);
    rig.pump(8);
    NETW_CHECK_EQ(host->get_controller(), controller);

    host->_on_peer_disconnected(controller);
    rig.pump(4);

    NETW_CHECK_EQ(host->get_controller(), int64_t(0));
    NETW_CHECK_EQ(host->get_stage(), int64_t(netw::entity::Stage::LIVE));
    NETW_CHECK_EQ(
        rig.route_node(route, -1)->get_multiplayer_authority(),
        int64_t(1)
    );
}

TEST_CASE(
    "[Networked][Entity] CA5 an entity that asked to end with its controller "
    "begins despawning on that drop instead of reverting, which is the whole "
    "difference the rule buys over the default"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *pen = rig.mirror_child("Pen");

    const int route = seat_player(rig, pen, "Ending");
    const Ref<NetwEntity> doomed = entity_at(rig, route, -1);
    REQUIRE(doomed.is_valid());
    doomed->set_on_controller_disconnect(
        int64_t(netw::entity::Control::DisconnectRule::DESPAWN)
    );

    const int64_t steerer = rig.peer_id(1);
    doomed->grant_control(steerer);
    rig.pump(8);
    NETW_CHECK_EQ(doomed->get_controller(), steerer);
    NETW_CHECK_EQ(doomed->get_stage(), int64_t(netw::entity::Stage::LIVE));

    doomed->_on_peer_disconnected(steerer);
    rig.pump(4);

    NETW_CHECK_EQ(
        doomed->get_stage(),
        int64_t(netw::entity::Stage::DESPAWNING)
    );
    NETW_CHECK_EQ(doomed->get_controller(), steerer);

    Node *ending = rig.route_node(route, -1);
    REQUIRE(ending != nullptr);
    ending->get_parent()->remove_child(ending);
    memdelete(ending);
}

} // namespace TestControlAxis

#endif

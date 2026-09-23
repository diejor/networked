#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/entity/control.hpp"
#include "netw/session/frames.hpp"
#include "netw/wire/registry.hpp"
#include "support/netw_call_log.h"

#include <godot_cpp/classes/node2d.hpp>

namespace TestControlAxis {

using namespace godot;
using namespace netw_test;
using netw::NetwControlRequest;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using Control = netw::entity::Control;

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
    host->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);

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

    host->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);
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
    host->set_transfer(NetwEntity::TRANSFER_FIXED);

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
    host->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);

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

    host->grant_control(asker);
    rig.pump(10);
    rig.server()->event_ring(route);

    host->connect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
    entity_at(rig, route, 0)->request_control();
    rig.pump(10);

    const Array after_denial = rig.server()->event_ring(route);
    const Array refused
        = rows_of(after_denial, netw::EventPlane::CONTROL_REQUESTED);
    NETW_REQUIRE_EQ(int64_t(refused.size()), int64_t(1));
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
    doomed->set_on_controller_disconnect(NetwEntity::DISCONNECT_DESPAWN);

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

void teach_every_session(LoopbackRig &p_rig) {
    teach_constructor(p_rig, p_rig.server());
    for (int at = 0; at < p_rig.count(); ++at) {
        teach_constructor(p_rig, p_rig.client(at));
    }
}

int seat_free(LoopbackRig &p_rig, Node *p_parent, const char *p_name) {
    const RID entity = p_rig.server()->spawn_registered(
        StringName(PLAYER_ID),
        named(p_name),
        nullptr
    );
    REQUIRE_MESSAGE(entity.is_valid(), "the spawn verb minted no entity");
    Node *node = p_rig.server()->entity_get_node(entity);
    REQUIRE_MESSAGE(node != nullptr, "the spawn verb built no node");
    p_parent->add_child(node);
    p_rig.pump(8);
    NetwEntity::of(node)->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);
    return int(p_rig.server()->entity_get_route(entity));
}

int hold_at(LoopbackRig &p_rig, int p_route, int p_client) {
    return int(entity_at(p_rig, p_route, p_client)->get_hold());
}

Error deliver_apply(
    LoopbackRig &p_rig,
    int p_client,
    int p_route,
    const netw::session::ControlApply &p_applied
) {
    return p_rig.client(p_client)->receive_carrier(
        NetwMultiplayer::frame_pack(
            p_route,
            0,
            netw::wire::builtin_channel("CONTROL_APPLY"),
            netw::session::frame_write(p_applied),
            String()
        ),
        p_rig.peer_id(-1),
        true,
        -1,
        -1
    );
}

TEST_CASE(
    "[Networked][Entity] CA6 a request the confirmed decision already "
    "excludes is refused before anything is sent, so the coordinator never "
    "hears of it"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    rig.server()->event_arm(true);
    const int route = seat_free(rig, arena, "Held");

    const Ref<NetwPromise> first = entity_at(rig, route, 0)->request_control();
    rig.pump(10);
    CHECK(first->get_is_completed());
    NETW_CHECK_EQ(entity_at(rig, route, 1)->get_controller(), rig.peer_id(0));
    NETW_CHECK_EQ(hold_at(rig, route, 1), int(Control::HOLD_EXCLUSIVE));

    rig.server()->event_ring(route);
    const Ref<NetwPromise> refused
        = entity_at(rig, route, 1)->request_control();
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(int(refused->get_code()), int(ERR_UNAUTHORIZED));
    CHECK_FALSE(entity_at(rig, route, 1)->get_is_control_pending());
    rig.pump(10);

    const Array heard = rows_of(
        rig.server()->event_ring(route),
        netw::EventPlane::CONTROL_REQUESTED
    );
    NETW_CHECK_EQ(int64_t(heard.size()), int64_t(0));
    NETW_CHECK_EQ(entity_at(rig, route, -1)->get_controller(), rig.peer_id(0));
    rig.server()->event_arm(false);
}

TEST_CASE(
    "[Networked][Entity] CA7 an exclusive request takes an entity another "
    "peer holds yieldably, and a yieldable one does not"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Touched");

    entity_at(rig, route, 0)->request_control(Control::HOLD_YIELDABLE);
    rig.pump(10);
    NETW_CHECK_EQ(entity_at(rig, route, -1)->get_controller(), rig.peer_id(0));
    NETW_CHECK_EQ(hold_at(rig, route, 1), int(Control::HOLD_YIELDABLE));
    const int64_t touched = int64_t(
        entity_at(rig, route, -1)->get_control_tenure()
    );

    const Ref<NetwPromise> nudged
        = entity_at(rig, route, 1)->request_control(Control::HOLD_YIELDABLE);
    CHECK(nudged->get_is_failed());
    NETW_CHECK_EQ(int(nudged->get_code()), int(ERR_UNAUTHORIZED));

    const Ref<NetwPromise> grabbed
        = entity_at(rig, route, 1)->request_control(Control::HOLD_EXCLUSIVE);
    rig.pump(10);
    CHECK(grabbed->get_is_completed());
    for (int client = -1; client < 2; ++client) {
        NETW_CHECK_EQ(
            entity_at(rig, route, client)->get_controller(),
            rig.peer_id(1)
        );
        NETW_CHECK_EQ(
            hold_at(rig, route, client),
            int(Control::HOLD_EXCLUSIVE)
        );
    }
    NETW_CHECK_GT(
        int64_t(entity_at(rig, route, -1)->get_control_tenure()),
        touched
    );
}

TEST_CASE(
    "[Networked][Entity] CA8 the controller changing its own hold keeps its "
    "tenure and never asks the filter, because nobody else is asking for "
    "anything"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Regripped");
    const Ref<NetwEntity> host = entity_at(rig, route, -1);

    entity_at(rig, route, 0)->request_control(Control::HOLD_YIELDABLE);
    rig.pump(10);
    NETW_REQUIRE_EQ(host->get_controller(), int64_t(rig.peer_id(0)));
    const int64_t tenure = int64_t(host->get_control_tenure());
    const int64_t revision = int64_t(host->get_control_revision());

    CallLog log;
    entity_at(rig, route, 0)->connect(
        StringName("control_changed"),
        log.callable("changed")
    );
    host->connect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
    const Ref<NetwPromise> tightened
        = entity_at(rig, route, 0)->request_control(Control::HOLD_EXCLUSIVE);
    rig.pump(10);

    CHECK(tightened->get_is_completed());
    NETW_CHECK_EQ(int(host->get_hold()), int(Control::HOLD_EXCLUSIVE));
    NETW_CHECK_EQ(int64_t(host->get_control_tenure()), tenure);
    NETW_CHECK_EQ(int64_t(host->get_control_revision()), revision + 1);
    NETW_CHECK_EQ(hold_at(rig, route, 0), int(Control::HOLD_EXCLUSIVE));
    NETW_CHECK_EQ(
        int64_t(entity_at(rig, route, 0)->get_control_tenure()),
        tenure
    );
    NETW_CHECK_EQ(log.count("changed"), 0);
    host->disconnect(
        StringName("control_requested"),
        callable_mp_static(&deny_every_request)
    );
}

TEST_CASE(
    "[Networked][Entity] CA9 grant_control moves an entity a peer holds "
    "exclusively, and the session's own decision holds nothing"
) {
    LoopbackRig rig(2);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Seized");

    entity_at(rig, route, 0)->request_control(Control::HOLD_EXCLUSIVE);
    rig.pump(10);
    NETW_REQUIRE_EQ(
        entity_at(rig, route, -1)->get_controller(),
        int64_t(rig.peer_id(0))
    );

    entity_at(rig, route, -1)->grant_control(rig.peer_id(1));
    rig.pump(10);
    for (int client = -1; client < 2; ++client) {
        NETW_CHECK_EQ(
            entity_at(rig, route, client)->get_controller(),
            rig.peer_id(1)
        );
        NETW_CHECK_EQ(hold_at(rig, route, client), int(Control::HOLD_NONE));
    }
}

TEST_CASE(
    "[Networked][Entity] CA10 a late joiner starts on the revision, tenure "
    "and hold the session already decided"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Seeded");

    entity_at(rig, route, 0)->request_control(Control::HOLD_YIELDABLE);
    rig.pump(10);
    entity_at(rig, route, 0)->request_control(Control::HOLD_EXCLUSIVE);
    rig.pump(10);
    const Ref<NetwEntity> host = entity_at(rig, route, -1);
    NETW_REQUIRE_EQ(int64_t(host->get_control_revision()), int64_t(2));

    const int late = rig.add_client();
    teach_constructor(rig, rig.client(late));
    rig.hold(late);
    rig.mount_late(late);
    rig.mirror_late(late, "Arena");
    rig.release(late);
    rig.pump(10);

    const Ref<NetwEntity> joined = entity_at(rig, route, late);
    REQUIRE(joined.is_valid());
    NETW_CHECK_EQ(joined->get_controller(), int64_t(rig.peer_id(0)));
    NETW_CHECK_EQ(int64_t(joined->get_control_revision()), int64_t(2));
    NETW_CHECK_EQ(int64_t(joined->get_control_tenure()), int64_t(1));
    NETW_CHECK_EQ(int(joined->get_hold()), int(Control::HOLD_EXCLUSIVE));
}

TEST_CASE(
    "[Networked][Entity] CA11 an apply that reaches a joiner before its spawn "
    "waits for the spawn, and one older than the spawn's own decision is "
    "discarded"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Deferred");

    entity_at(rig, route, 0)->request_control(Control::HOLD_YIELDABLE);
    rig.pump(10);
    entity_at(rig, route, 0)->request_control(Control::HOLD_EXCLUSIVE);
    rig.pump(10);

    const int late = rig.add_client();
    teach_constructor(rig, rig.client(late));
    rig.hold(late);
    rig.mount_late(late);
    rig.mirror_late(late, "Arena");
    rig.client(late)->event_arm(true);

    netw::session::ControlApply stale;
    stale.controller = 0;
    stale.revision = 1;
    stale.tenure_changed = true;
    deliver_apply(rig, late, route, stale);

    netw::session::ControlApply newer;
    newer.controller = uint64_t(rig.peer_id(0));
    newer.revision = 3;
    newer.hold = uint8_t(Control::HOLD_YIELDABLE);
    deliver_apply(rig, late, route, newer);

    rig.release(late);
    rig.pump(10);

    const Ref<NetwEntity> joined = entity_at(rig, route, late);
    REQUIRE(joined.is_valid());
    NETW_CHECK_EQ(joined->get_controller(), int64_t(rig.peer_id(0)));
    NETW_CHECK_EQ(int64_t(joined->get_control_revision()), int64_t(3));
    NETW_CHECK_EQ(int64_t(joined->get_control_tenure()), int64_t(1));
    NETW_CHECK_EQ(int(joined->get_hold()), int(Control::HOLD_YIELDABLE));

    const Array moved = rows_of(
        rig.client(late)->event_ring(route),
        netw::EventPlane::CONTROL_CHANGED
    );
    for (int at = 0; at < moved.size(); ++at) {
        const Dictionary row = moved[at];
        const Dictionary detail = row[netw::event_key::detail()];
        NETW_CHECK_GT(int64_t(detail.get(StringName("to"), -1)), int64_t(0));
    }
    rig.client(late)->event_arm(false);
}

TEST_CASE(
    "[Networked][Entity] CA12 an apply that answers another op never settles "
    "a pending request, because settlement matches the op it names"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Matched");

    rig.hold(0);
    const Ref<NetwPromise> asked = entity_at(rig, route, 0)->request_control();
    rig.pump(10);
    REQUIRE_FALSE(asked->get_is_settled());

    netw::session::ControlApply unrelated;
    deliver_apply(rig, 0, route, unrelated);
    unrelated.op = 7;
    unrelated.outcome = uint8_t(Control::Outcome::UNAUTHORIZED);
    deliver_apply(rig, 0, route, unrelated);

    CHECK_FALSE(asked->get_is_settled());
    CHECK(entity_at(rig, route, 0)->get_is_control_pending());

    rig.release(0);
    rig.pump(10);
    CHECK(asked->get_is_completed());
    CHECK_FALSE(entity_at(rig, route, 0)->get_is_control_pending());
}

TEST_CASE(
    "[Networked][Entity] CA13 a request that times out at the issuer and is "
    "granted afterwards is a real decision, so the issuer gives it straight "
    "back and control_changed fires twice"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Late");
    const Ref<NetwEntity> issuer = entity_at(rig, route, 0);

    CallLog log;
    issuer->connect(StringName("control_changed"), log.callable("changed"));

    rig.hold(0);
    const Ref<NetwPromise> asked = issuer->request_control();
    rig.pump(6);
    NETW_REQUIRE_EQ(
        entity_at(rig, route, -1)->get_controller(),
        int64_t(rig.peer_id(0))
    );

    rig.step_ticks(int(rig.client(0)->clock_engine().get_tickrate()) + 1);
    CHECK(asked->get_is_failed());
    NETW_CHECK_EQ(int(asked->get_code()), int(ERR_TIMEOUT));
    CHECK_FALSE(issuer->get_is_control_pending());

    rig.release(0);
    rig.pump(12);

    NETW_CHECK_EQ(log.count("changed"), 2);
    NETW_CHECK_EQ(entity_at(rig, route, -1)->get_controller(), int64_t(0));
    NETW_CHECK_EQ(issuer->get_controller(), int64_t(0));
    NETW_CHECK_EQ(int(asked->get_code()), int(ERR_TIMEOUT));
}

TEST_CASE(
    "[Networked][Entity] CA14 a peer's requests reach the coordinator in the "
    "order it issued them across entities, however the link shuffles its "
    "datagrams"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);

    const int COUNT = 6;
    int routes[COUNT];
    CallLog log;
    const char *tags[COUNT] = {"e0", "e1", "e2", "e3", "e4", "e5"};
    for (int at = 0; at < COUNT; ++at) {
        routes[at] = seat_free(rig, arena, tags[at]);
        entity_at(rig, routes[at], -1)->connect(
            StringName("control_requested"),
            log.callable(StringName(tags[at]))
        );
    }

    const Ref<netw::LocalLinkConditions> shuffling
        = netw::LocalLinkConditions::create(11);
    shuffling->set_latency_ms(20.0);
    shuffling->set_jitter_ms(120.0);
    rig.session()->set_link_conditions(
        rig.session()->get_server_peer().ptr(),
        shuffling,
        rig.peer_id(0)
    );

    for (int at = 0; at < COUNT; ++at) {
        entity_at(rig, routes[at], 0)->request_control();
    }
    for (int step = 0; step < 30; ++step) {
        rig.advance(20.0);
        rig.pump();
    }

    const Vector<StringName> heard = log.order();
    NETW_REQUIRE_EQ(int64_t(heard.size()), int64_t(COUNT));
    for (int at = 0; at < COUNT; ++at) {
        int issued = -1;
        for (int tag = 0; tag < COUNT; ++tag) {
            if (heard[at] == StringName(tags[tag])) {
                issued = tag;
            }
        }
        NETW_CHECK_EQ(issued, at);
    }
}

TEST_CASE(
    "[Networked][Entity] CA15 a ninth request outstanding on one entity is "
    "refused before it is sent"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Crowded");
    const Ref<NetwEntity> issuer = entity_at(rig, route, 0);

    for (uint32_t at = 0; at < Control::MAX_PENDING; ++at) {
        CHECK_FALSE(issuer->request_control()->get_is_settled());
    }
    const Ref<NetwPromise> ninth = issuer->request_control();
    CHECK(ninth->get_is_failed());
    NETW_CHECK_EQ(int(ninth->get_code()), int(ERR_UNAVAILABLE));

    rig.pump(10);
    CHECK_FALSE(issuer->get_is_control_pending());
    NETW_CHECK_EQ(issuer->get_controller(), int64_t(rig.peer_id(0)));
}

TEST_CASE(
    "[Networked][Entity] CA16 an immediate request on an entity a "
    "MultiplayerSynchronizer replicates is refused before it is sent, "
    "because nothing it authors can run ahead of the decision"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Adopted");
    const Ref<NetwEntity> issuer = entity_at(rig, route, 0);
    issuer->set_transfer(NetwEntity::TRANSFER_IMMEDIATE);
    MultiplayerSynchronizer *adopted = memnew(MultiplayerSynchronizer);
    issuer->get_owner()->add_child(adopted);
    adopted->set_owner(issuer->get_owner());
    issuer->invalidate_synchronizers_cache();

    const Ref<NetwPromise> refused = issuer->request_control();
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(int(refused->get_code()), int(ERR_UNAVAILABLE));
    CHECK_FALSE(issuer->get_is_control_pending());

    issuer->set_transfer(NetwEntity::TRANSFER_REQUESTABLE);
    const Ref<NetwPromise> asked = issuer->request_control();
    rig.pump(10);
    CHECK(asked->get_is_completed());
}

TEST_CASE(
    "[Networked][Entity] CA17 a request still outstanding when its entity is "
    "freed fails as unavailable, because no decision can reach it any more"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    teach_every_session(rig);
    const int route = seat_free(rig, arena, "Dropped");

    rig.hold(0);
    const Ref<NetwPromise> asked
        = entity_at(rig, route, 0)->request_control();
    REQUIRE_FALSE(asked->get_is_settled());

    Node *copy = rig.route_node(route, 0);
    REQUIRE(copy != nullptr);
    copy->get_parent()->remove_child(copy);
    memdelete(copy);
    rig.step_ticks(1);

    CHECK(asked->get_is_failed());
    NETW_CHECK_EQ(int(asked->get_code()), int(ERR_UNAVAILABLE));
    rig.release(0);
    rig.pump(4);
}

} // namespace TestControlAxis

#endif

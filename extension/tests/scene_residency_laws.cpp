#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/scene_membership.hpp"
#include "support/joined_peer.h"
#include "support/netw_call_log.h"

namespace TestSceneResidencyLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::CallLog;
using netw_test::LoopbackRig;

Node *build_body(const Variant &p_marker) {
    Node2D *made = memnew(Node2D);
    made->set_name("Body");
    made->set_meta(StringName("marker"), p_marker);
    return made;
}

Node *build_level(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    const Ref<NetwEntity> facet = NetwEntity::ensure(made);
    REQUIRE(facet.is_valid());
    facet->set_declares_scene(true);
    facet->set_scene_label(StringName(String(p_name)));
    return made;
}

Array one_string(const String &p_value) {
    Array out;
    out.push_back(p_value);
    return out;
}

Array string_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

RID open_scene(LoopbackRig &p_rig, const StringName &p_name, Node *p_under) {
    const int route = p_rig.spawn_registered(
        StringName("level"),
        callable_mp_static(&build_level),
        one_string(String(p_name)),
        string_types(),
        p_under != nullptr ? p_under : p_rig.branch(-1),
        Variant()
    );
    REQUIRE(route > 0);
    const RID scene = p_rig.spawned_entity();
    p_rig.pump(6);
    REQUIRE(p_rig.server()->scene_is_declared(scene));
    return scene;
}

Node *level_of(LoopbackRig &p_rig, const RID &p_scene) {
    Node *held = p_rig.server()->entity_get_node(p_scene);
    REQUIRE(held != nullptr);
    return held;
}

int spawn_into(
    LoopbackRig &p_rig,
    Node *p_parent,
    const Ref<netw::NetwParticipant> &p_owner,
    const char *p_marker,
    bool p_pump = true
) {
    return p_rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        one_string(p_marker),
        string_types(),
        p_parent,
        p_owner,
        p_pump
    );
}

int edges_for(const CallLog &p_seen, const RID &p_scene, bool p_subscribed) {
    int total = 0;
    for (int at = 0; at < p_seen.count(StringName("edge")); ++at) {
        const Array row = p_seen.args(StringName("edge"), at);
        if (RID(row[0]) == p_scene && bool(row[2]) == p_subscribed) {
            total += 1;
        }
    }
    return total;
}

TEST_CASE(
    "[Networked][Scene] JR1 a body residing in a scene subscribes the player "
    "who owns it, with no seat and no admission call, and that player then "
    "receives the rest of that scene's content"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    Node *level = level_of(rig, arena);
    const int peer = rig.peer_id(0);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, peer, StringName("ana"));

    CHECK_FALSE(host->scene_subscribes(arena, peer));

    const int player = spawn_into(rig, level, ana, "player");
    NETW_CHECK_GT(player, 0);

    CHECK(host->scene_subscribes(arena, peer));
    CHECK_FALSE(host->scene_watches(arena, peer));
    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 1);

    const int furniture
        = spawn_into(rig, level, Ref<netw::NetwParticipant>(), "furniture");
    rig.pump(8);

    CHECK(rig.route_node(furniture, 0) != nullptr);
    CHECK(rig.route_node(player, 0) != nullptr);
}

TEST_CASE(
    "[Networked][Scene] JR2 two bodies of one player in one scene are two "
    "reasons, so the first one leaving keeps the player subscribed and the "
    "last one leaving takes the subscription away"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    Node *level = level_of(rig, arena);
    const int peer = rig.peer_id(0);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, peer, StringName("ana"));

    const int first = spawn_into(rig, level, ana, "first");
    const int second = spawn_into(rig, level, ana, "second");
    Node *first_node = rig.route_node(first);
    Node *second_node = rig.route_node(second);
    REQUIRE(first_node != nullptr);
    REQUIRE(second_node != nullptr);
    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 2);

    first_node->get_parent()->remove_child(first_node);
    rig.pump(8);

    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 1);
    CHECK(host->scene_subscribes(arena, peer));

    second_node->get_parent()->remove_child(second_node);
    rig.pump(8);

    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 0);
    CHECK_FALSE(host->scene_subscribes(arena, peer));

    memdelete(first_node);
    memdelete(second_node);
    rig.pump(4);
}

TEST_CASE(
    "[Networked][Scene] JR3 a body that leaves a scene and returns inside one "
    "settle never reports leaving it, because a move is classified from the "
    "tree the body ended in rather than from the hops it took"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    const RID annex = open_scene(rig, StringName("Annex"), nullptr);
    const int peer = rig.peer_id(0);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, peer, StringName("ana"));

    const int route = spawn_into(rig, level_of(rig, arena), ana, "mover");
    Node *body = rig.route_node(route);
    REQUIRE(body != nullptr);
    REQUIRE(host->scene_subscribes(arena, peer));

    const CallLog seen;
    host->set_scene_viewer_edge(seen.callable("edge"));

    body->reparent(level_of(rig, annex));
    body->reparent(level_of(rig, arena));
    rig.pump(8);

    NETW_CHECK_EQ(edges_for(seen, arena, false), 0);
    NETW_CHECK_EQ(edges_for(seen, annex, true), 0);
    CHECK(host->scene_subscribes(arena, peer));
    CHECK_FALSE(host->scene_subscribes(annex, peer));
    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 1);
}

TEST_CASE(
    "[Networked][Scene] JR4 moving an ordinary parent moves every represented "
    "descendant inside it, so both players it carried subscribe to where the "
    "parent landed and neither is left subscribed to where it was"
) {
    LoopbackRig rig(2);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    const RID annex = open_scene(rig, StringName("Annex"), nullptr);
    const int first = rig.peer_id(0);
    const int second = rig.peer_id(1);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, first, StringName("ana"));
    const Ref<netw::NetwParticipant> bo
        = netw_test::seated_peer(host, second, StringName("bo"));

    Node *carrier = memnew(Node2D);
    carrier->set_name("Carrier");
    level_of(rig, arena)->add_child(carrier);

    spawn_into(rig, carrier, ana, "ana");
    spawn_into(rig, carrier, bo, "bo");
    REQUIRE(host->scene_subscribes(arena, first));
    REQUIRE(host->scene_subscribes(arena, second));

    carrier->reparent(level_of(rig, annex));
    rig.pump(10);

    CHECK(host->scene_subscribes(annex, first));
    CHECK(host->scene_subscribes(annex, second));
    CHECK_FALSE(host->scene_subscribes(arena, first));
    CHECK_FALSE(host->scene_subscribes(arena, second));
}

TEST_CASE(
    "[Networked][Scene] JR5 an explicit watch outlives the body that happened "
    "to be there, because a departure drops the body's own reason and never "
    "the reason a caller asked for"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    const RID annex = open_scene(rig, StringName("Annex"), nullptr);
    const int peer = rig.peer_id(0);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, peer, StringName("ana"));

    const int route = spawn_into(rig, level_of(rig, arena), ana, "mover");
    Node *body = rig.route_node(route);
    REQUIRE(body != nullptr);
    REQUIRE(host->scene_admit_peer(arena, peer));
    REQUIRE(host->scene_watches(arena, peer));

    body->reparent(level_of(rig, annex));
    rig.pump(8);

    CHECK(host->scene_watches(arena, peer));
    CHECK(host->scene_subscribes(arena, peer));
    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 0);
    CHECK(host->scene_subscribes(annex, peer));
}

TEST_CASE(
    "[Networked][Scene] JR6 a body parented under another session's tree is "
    "refused rather than adopted, so the session it left revokes its "
    "membership and the destination claims nothing"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    const int peer = rig.peer_id(0);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, peer, StringName("ana"));

    const int route = spawn_into(rig, level_of(rig, arena), ana, "mover");
    Node *body = rig.route_node(route);
    REQUIRE(body != nullptr);
    REQUIRE(host->scene_subscribes(arena, peer));

    Node *foreign = rig.route_node(int(host->entity_get_route(arena)), 0);
    REQUIRE(foreign != nullptr);

    ERR_PRINT_OFF;
    body->reparent(foreign);
    rig.pump(8);
    ERR_PRINT_ON;

    CHECK_FALSE(host->scene_subscribes(arena, peer));
    NETW_CHECK_EQ(host->membership_book().subscriptions_of(peer), 0);

    body->reparent(level_of(rig, arena));
    rig.pump(8);
    CHECK(host->scene_subscribes(arena, peer));
}

TEST_CASE(
    "[Networked][Scene] JR7 a body whose player holds no live membership "
    "subscribes nobody when it lands, because an armed orphan can outlive the "
    "acceptance that asked for it"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    const int peer = rig.peer_id(0);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, peer, StringName("ana"));

    const int route
        = spawn_into(rig, level_of(rig, arena), ana, "orphan", false);
    NETW_CHECK_GT(route, 0);
    host->participant_forget(peer);
    REQUIRE_FALSE(host->participant_has(peer));

    rig.pump(8);

    CHECK_FALSE(host->scene_subscribes(arena, peer));
    NETW_CHECK_EQ(host->membership_book().bodies_in(peer, arena), 0);
}

TEST_CASE(
    "[Networked][Scene] JR9 watching a nested scene delivers that scene and "
    "the structural ancestors it hangs from, because a scene root has to "
    "exist on a receiver before anything inside it can"
) {
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID outer = open_scene(rig, StringName("Outer"), nullptr);
    const RID inner
        = open_scene(rig, StringName("Inner"), level_of(rig, outer));
    const int peer = rig.peer_id(0);
    REQUIRE(netw_test::seated_peer(host, peer, StringName("ana")).is_valid());
    const int inner_body
        = spawn_into(rig, level_of(rig, inner), Ref<netw::NetwParticipant>(),
            "inside");

    REQUIRE(host->scene_admit_peer(inner, peer));
    rig.pump(10);

    CHECK(host->scene_subscribes(inner, peer));
    CHECK_FALSE(host->scene_subscribes(outer, peer));
    CHECK(rig.route_node(int(host->entity_get_route(inner)), 0) != nullptr);
    CHECK(rig.route_node(int(host->entity_get_route(outer)), 0) != nullptr);
    CHECK(rig.route_node(inner_body, 0) != nullptr);
}

TEST_CASE(
    "[Networked][Scene] JR10 a nested watch reaches nothing else the ancestor "
    "holds, so the sibling content of an outer scene stays with the peers the "
    "outer scene admits and the nested watcher never sees it"
) {
    LoopbackRig rig(3);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID outer = open_scene(rig, StringName("Outer"), nullptr);
    const RID inner
        = open_scene(rig, StringName("Inner"), level_of(rig, outer));
    const int nested = rig.peer_id(0);
    const int whole = rig.peer_id(1);
    const int outside = rig.peer_id(2);
    REQUIRE(netw_test::seated_peer(host, nested, StringName("ana")).is_valid());
    REQUIRE(netw_test::seated_peer(host, whole, StringName("bo")).is_valid());
    REQUIRE(netw_test::seated_peer(host, outside, StringName("cy")).is_valid());

    const int sibling
        = spawn_into(rig, level_of(rig, outer), Ref<netw::NetwParticipant>(),
            "sibling");

    REQUIRE(host->scene_admit_peer(inner, nested));
    REQUIRE(host->scene_admit_peer(outer, whole));
    rig.pump(12);

    CHECK(rig.route_node(int(host->entity_get_route(inner)), 0) != nullptr);
    CHECK(rig.route_node(sibling, 0) == nullptr);
    CHECK(rig.route_node(sibling, 1) != nullptr);
    CHECK(rig.route_node(sibling, 2) == nullptr);
    CHECK(rig.route_node(int(host->entity_get_route(inner)), 2) == nullptr);
    CHECK_FALSE(host->scene_subscribes(outer, nested));
    CHECK_FALSE(host->scene_subscribes(inner, whole));
}

TEST_CASE(
    "[Networked][Scene] JR8 the run's placements agree with the tree, so "
    "every body reason names a body that resides in that scene and every "
    "resident body holds one"
) {
#if defined(NETW_TESTS)
    const int64_t opened = netw::residency_audit::ledger().observations;
#endif
    LoopbackRig rig(2);
    rig.mount();
    NetwMultiplayer *host = rig.server();
    const RID arena = open_scene(rig, StringName("Arena"), nullptr);
    const RID annex = open_scene(rig, StringName("Annex"), nullptr);
    const Ref<netw::NetwParticipant> ana
        = netw_test::seated_peer(host, rig.peer_id(0), StringName("ana"));
    const Ref<netw::NetwParticipant> bo
        = netw_test::seated_peer(host, rig.peer_id(1), StringName("bo"));

    const int mover = spawn_into(rig, level_of(rig, arena), ana, "ana");
    spawn_into(rig, level_of(rig, annex), bo, "bo");
    Node *body = rig.route_node(mover);
    REQUIRE(body != nullptr);

    body->reparent(level_of(rig, annex));
    rig.pump(8);
    body->reparent(level_of(rig, arena));
    rig.pump(8);

#if defined(NETW_TESTS)
    const int64_t audited
        = netw::residency_audit::ledger().observations - opened;
    NETW_CHECK_GT(int(audited), 0);
    NETW_CHECK_EQ(netw::residency_audit::ledger().divergences, 0);
#endif
}

} // namespace TestSceneResidencyLaws

#endif

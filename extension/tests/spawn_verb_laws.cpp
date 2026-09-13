#include "support/loopback_rig.h"
#include "support/spawn_probe.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"

#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>

namespace TestSpawnVerbLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::spawn::Book;

const char *VERB_ID = "verb_probe";
const char *PROBE_CHILD = "Probe";

Node *build_verb_probe(const Variant &p_name) {
    Node2D *root = memnew(Node2D);
    root->set_name(String(p_name));
    SpawnIdentityProbe *probe = memnew(SpawnIdentityProbe);
    probe->set_name(PROBE_CHILD);
    root->add_child(probe);
    probe->set_owner(root);
    return root;
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

SpawnIdentityProbe *probe_of(Node *p_root) {
    if (p_root == nullptr) {
        return nullptr;
    }
    return Object::cast_to<SpawnIdentityProbe>(
        p_root->get_node_or_null(NodePath(PROBE_CHILD))
    );
}

void teach(LoopbackRig &p_rig) {
    p_rig.register_constructor(
        p_rig.server(),
        StringName(VERB_ID),
        callable_mp_static(&build_verb_probe),
        one_type()
    );
    for (int at = 0; at < p_rig.count(); ++at) {
        p_rig.register_constructor(
            p_rig.client(at),
            StringName(VERB_ID),
            callable_mp_static(&build_verb_probe),
            one_type()
        );
    }
}

struct Seated {
    RID entity;
    int route = 0;
    Node *node = nullptr;
};

Seated seat(
    LoopbackRig &p_rig,
    Node *p_parent,
    const char *p_name,
    const Variant &p_owner = Variant()
) {
    teach(p_rig);
    Seated made;
    made.entity = p_rig.server()->spawn_registered(
        StringName(VERB_ID),
        named(p_name),
        netw::gd::live_object(p_owner)
    );
    REQUIRE_MESSAGE(made.entity.is_valid(), "the verb minted no entity");
    made.node = p_rig.server()->entity_get_node(made.entity);
    REQUIRE_MESSAGE(made.node != nullptr, "the verb built no node");
    made.route = int(p_rig.server()->entity_get_route(made.entity));
    p_parent->add_child(made.node);
    return made;
}

TEST_CASE(
    "[Networked][Spawn] SV1 a mirror's identity is already stamped at "
    "_enter_tree, so a receiver that derives anything from route or entity id "
    "inside its own tree notification reads the same answer the authority has"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Seated made = seat(rig, arena, "Stamped");
    rig.pump(8);

    SpawnIdentityProbe *mirror = probe_of(rig.route_node(made.route, 0));
    REQUIRE_MESSAGE(mirror != nullptr, "the spawn never reached the client");

    const Ref<NetwEntity> host = NetwEntity::of(made.node);
    REQUIRE(host.is_valid());

    const StringName ENTER("enter_tree");
    NETW_CHECK_EQ(mirror->route_at(ENTER), int64_t(made.route));
    const bool named_at_entry
        = mirror->entity_id_at(ENTER) == host->get_entity_id();
    CHECK(named_at_entry);
    CHECK(mirror->entity_id_at(ENTER) != StringName());
    NETW_CHECK_EQ(mirror->route_at(StringName("ready")), int64_t(made.route));
}

TEST_CASE(
    "[Networked][Spawn] SV2 an owner stamp is authority by the time the mirror "
    "enters the tree, so authority derived inside _enter_tree needs no pending "
    "spawn window to be correct"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    teach(rig);
    rig.join(0, StringName("alpha"));
    const int64_t owned_by = rig.peer_id(0);

    const Seated made = seat(rig, arena, "Owned", rig.participant(0));
    rig.pump(8);

    const Ref<NetwEntity> host = NetwEntity::of(made.node);
    REQUIRE(host.is_valid());
    NETW_CHECK_EQ(host->get_peer_id(), owned_by);
    NETW_CHECK_EQ(host->get_controller(), owned_by);

    SpawnIdentityProbe *mirror = probe_of(rig.route_node(made.route, 0));
    REQUIRE_MESSAGE(mirror != nullptr, "the spawn never reached the client");

    const StringName ENTER("enter_tree");
    NETW_CHECK_EQ(mirror->peer_at(ENTER), owned_by);
    NETW_CHECK_EQ(mirror->authority_at(ENTER), owned_by);
    NETW_CHECK_EQ(
        rig.route_node(made.route, 0)->get_multiplayer_authority(),
        owned_by
    );
}

TEST_CASE(
    "[Networked][Spawn] SV3 a duplicate spawn frame is dropped and counted, "
    "and leaves the receiver holding one instance, because a retransmit must "
    "not build a second body for one route"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Seated made = seat(rig, arena, "Once");
    rig.pump(8);
    REQUIRE(rig.route_node(made.route, 0) != nullptr);

    Node *client_arena = rig.branch(0)->get_node_or_null(NodePath("Arena"));
    REQUIRE(client_arena != nullptr);
    const int seated_children = client_arena->get_child_count();

    rig.deliver_spawn(0, rig.spawn_frame_of(made.route));
    rig.pump(4);

    NETW_CHECK_EQ(client_arena->get_child_count(), seated_children);
    CHECK(rig.route_node(made.route, 0) != nullptr);
}

} // namespace TestSpawnVerbLaws

#endif

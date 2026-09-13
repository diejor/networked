#include "support/loopback_rig.h"
#include "support/spawn_probe.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/entity/stage.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestSpawnPropertyTransport {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

const char *PROBE_CHILD = "Probe";

Node *build_probe_player(const Variant &p_name) {
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

Dictionary packet_of(const char *p_marker) {
    Dictionary out;
    out["marker"] = String(p_marker);
    return out;
}

void check_marker(const String &p_produced, const char *p_expected) {
    NETW_FORMAT_TEXT(produced, p_produced.utf8().get_data());
    NETW_FORMAT_TEXT(expected, p_expected);
    CAPTURE(produced);
    CAPTURE(expected);
    const bool matches = p_produced == String(p_expected);
    CHECK(matches);
}

SpawnIdentityProbe *probe_of(Node *p_root) {
    if (p_root == nullptr) {
        return nullptr;
    }
    return Object::cast_to<SpawnIdentityProbe>(
        p_root->get_node_or_null(NodePath(PROBE_CHILD))
    );
}

struct Spawned {
    RID entity;
    int route = 0;
    Node *node = nullptr;
    SpawnIdentityProbe *probe = nullptr;
};

Spawned spawn_marked(
    LoopbackRig &p_rig,
    Node *p_parent,
    const char *p_name,
    const char *p_marker
) {
    const StringName id("probe_player");
    p_rig.register_constructor(
        p_rig.server(),
        id,
        callable_mp_static(&build_probe_player),
        one_type()
    );
    for (int at = 0; at < p_rig.count(); ++at) {
        p_rig.register_constructor(
            p_rig.client(at),
            id,
            callable_mp_static(&build_probe_player),
            one_type()
        );
    }

    Spawned made;
    made.entity = p_rig.server()->spawn_registered(id, named(p_name), nullptr);
    REQUIRE_MESSAGE(made.entity.is_valid(), "the spawn verb minted no entity");
    made.node = p_rig.server()->entity_get_node(made.entity);
    REQUIRE_MESSAGE(made.node != nullptr, "the spawn verb built no node");
    made.probe = probe_of(made.node);
    REQUIRE_MESSAGE(made.probe != nullptr, "the built player carries no probe");

    made.probe->set_identity_packet(packet_of(p_marker));
    p_parent->add_child(made.node);
    p_rig.pump(6);
    made.route = int(p_rig.server()->entity_get_route(made.entity));
    return made;
}

TEST_CASE(
    "[Networked][Spawn] SP1 a mirror carries the authority's spawn-marked "
    "value at every hook from tree entry onward, and at none before, because "
    "the pipeline applies spawn state while the instance is still orphaned"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Spawned made = spawn_marked(rig, arena, "Ordering", "ordering");
    check_marker(made.probe->marker(), "ordering");

    SpawnIdentityProbe *mirror = probe_of(rig.route_node(made.route, 0));
    REQUIRE_MESSAGE(mirror != nullptr, "the spawn never reached the client");

    check_marker(mirror->marker(), "ordering");
    CHECK(mirror->saw(StringName("parented")));
    check_marker(mirror->marker_at(StringName("parented")), "");
    check_marker(mirror->marker_at(StringName("spawning")), "ordering");
    check_marker(mirror->marker_at(StringName("enter_tree")), "ordering");
    check_marker(mirror->marker_at(StringName("ready")), "ordering");
}

TEST_CASE(
    "[Networked][Spawn] SP2 a peer seated after the authority changed a "
    "spawn-marked value receives the value the authority holds NOW, because "
    "the frame is encoded off the live node rather than off what the spawn "
    "captured"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const Spawned made = spawn_marked(rig, arena, "Drifting", "initial");
    REQUIRE(probe_of(rig.route_node(made.route, 0)) != nullptr);

    made.probe->set_identity_packet(packet_of("current"));

    const int late = rig.add_client();
    rig.mount_late(late);
    rig.mirror_late(late, "Arena");
    rig.register_constructor(
        rig.client(late),
        StringName("probe_player"),
        callable_mp_static(&build_probe_player),
        one_type()
    );
    rig.deliver_spawn(late, rig.spawn_frame_of(made.route));
    rig.pump(6);

    SpawnIdentityProbe *seated = probe_of(rig.route_node(made.route, late));
    REQUIRE_MESSAGE(seated != nullptr, "the late seat never materialized");
    check_marker(seated->marker(), "current");
    check_marker(probe_of(rig.route_node(made.route, 0))->marker(), "initial");
}

TEST_CASE(
    "[Networked][Spawn] SP3 an instance nothing bound is UNBOUND and is not a "
    "template, so a bare copy never becomes a live entity by existing"
) {
    Node *bare = build_probe_player(String("Bare"));

    const Ref<NetwEntity> entity = NetwEntity::ensure(bare);
    REQUIRE(entity.is_valid());

    NETW_CHECK_EQ(entity->get_stage(), int64_t(netw::entity::Stage::UNBOUND));
    CHECK_FALSE(entity->get_is_template());
    check_marker(probe_of(bare)->marker(), "");

    memdelete(bare);
}

} // namespace TestSpawnPropertyTransport

#endif

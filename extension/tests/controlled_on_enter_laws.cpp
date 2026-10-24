#include "support/level_spawn.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/templates.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"

namespace TestControlledOnEnterLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw_test::LoopbackRig;

struct Entered {
    uint64_t camera = 0;
    bool controlled = false;
};

LocalVector<Entered> &entries() {
    static LocalVector<Entered> seen;
    return seen;
}

void note_entered(Node *p_camera) {
    const Ref<NetwEntity> entity = NetwEntity::of(p_camera);
    Entered entry;
    entry.camera = uint64_t(p_camera->get_instance_id());
    entry.controlled = entity.is_valid() && entity->get_is_controlled_locally();
    entries().push_back(entry);
}

Node *build_body(const Variant &p_peer) {
    Node *body = memnew(Node);
    body->set_name("Body");
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    entity->set_peer_id(int64_t(p_peer));
    entity->record_controller(int64_t(p_peer));
    Node *camera = memnew(Node);
    camera->set_name("Camera");
    camera->connect(
        StringName("tree_entered"),
        callable_mp_static(&note_entered).bind(camera)
    );
    body->add_child(camera);
    return body;
}

int enters_of(Node *p_camera) {
    int count = 0;
    for (const Entered &entry : entries()) {
        count += entry.camera == uint64_t(p_camera->get_instance_id()) ? 1 : 0;
    }
    return count;
}

bool last_answer_of(Node *p_camera) {
    bool answer = false;
    for (const Entered &entry : entries()) {
        if (entry.camera == uint64_t(p_camera->get_instance_id())) {
            answer = entry.controlled;
        }
    }
    return answer;
}

Node *isolated_scene(LoopbackRig &p_rig, const char *p_path) {
    Node *root = netw_test::spawn_level(p_rig, String(p_path), true);
    REQUIRE(root != nullptr);
    REQUIRE(NetwMultiplayer::scene_world_of(root) != nullptr);
    return root;
}

void retire(Node *p_root) {
    Node *outer = NetwMultiplayer::scene_outer_of(p_root);
    outer->get_parent()->remove_child(outer);
    memdelete(outer);
}

TEST_CASE(
    "[Networked][Entity][SceneTree] LC1 a body the host controls already "
    "reads as locally controlled when its child enters the tree during a "
    "host-side move into another isolated world"
) {
    entries().clear();
    LoopbackRig rig(1);
    rig.mount();
    NetwMultiplayer *api = rig.server();
    Node *first
        = isolated_scene(rig, "res://tests/support/declared_level.tscn");
    Node *second
        = isolated_scene(rig, "res://tests/support/isolated_level.tscn");
    rig.pump(6);

    Array host_peer;
    host_peer.push_back(rig.peer_id(-1));
    Array peer_type;
    peer_type.push_back(int(Variant::INT));
    const int route = rig.spawn_registered(
        StringName("body"),
        callable_mp_static(&build_body),
        host_peer,
        peer_type,
        first
    );
    Node *camera = rig.route_node(route)->get_node_or_null(NodePath("Camera"));
    REQUIRE(camera != nullptr);
    const int before = enters_of(camera);

    const Ref<NetwPromise> moved
        = api->entity_reparent(api->entity_from_route(route), second);
    rig.pump(6);

    CHECK(moved->get_is_settled());
    CHECK_FALSE(moved->get_is_failed());
    CHECK(NetwMultiplayer::scene_world_of(second)->is_ancestor_of(camera));
    NETW_CHECK_EQ(enters_of(camera), before + 1);
    CHECK(last_answer_of(camera));

    retire(first);
    retire(second);
}

} // namespace TestControlledOnEnterLaws

#endif

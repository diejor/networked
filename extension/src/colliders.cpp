#include "netw/colliders.hpp"

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/log.hpp"
#include "netw/tree_hook.hpp"

using namespace godot;

namespace netw::colliders {

namespace {

struct Book {
    ObjectID tree;
    HashMap<uint64_t, LocalVector<ObjectID>> lists;
    HashMap<uint64_t, uint8_t> visual_roots;
    HashMap<uint64_t, uint8_t> reported;
};

Book &book() {
    static Book held;
    return held;
}

uint64_t key_of(const Node *p_node) {
    return uint64_t(p_node->get_instance_id());
}

Node *node_of(ObjectID p_id) {
    return Object::cast_to<Node>(gd::object_of(p_id));
}

bool is_collider(Node *p_node) {
    return Object::cast_to<CollisionObject3D>(p_node) != nullptr
        || Object::cast_to<CollisionObject2D>(p_node) != nullptr;
}

bool is_kinematic(Node *p_node) {
    if (PhysicsBody3D *body = Object::cast_to<PhysicsBody3D>(p_node)) {
        return PhysicsServer3D::get_singleton()->body_get_mode(body->get_rid())
            == PhysicsServer3D::BODY_MODE_KINEMATIC;
    }
    if (PhysicsBody2D *body = Object::cast_to<PhysicsBody2D>(p_node)) {
        return PhysicsServer2D::get_singleton()->body_get_mode(body->get_rid())
            == PhysicsServer2D::BODY_MODE_KINEMATIC;
    }
    return false;
}

void forget_above(Node *p_node) {
    Book &held = book();
    for (Node *at = p_node->get_parent(); at != nullptr;
         at = at->get_parent()) {
        held.lists.erase(key_of(at));
    }
}

void on_node_added(Node *p_node) {
    if (!book().lists.is_empty() && is_collider(p_node)) {
        forget_above(p_node);
    }
}

void on_node_removed(Node *p_node) {
    Book &held = book();
    if (!held.visual_roots.is_empty()) {
        held.visual_roots.erase(key_of(p_node));
    }
    if (held.lists.is_empty()) {
        return;
    }
    held.lists.erase(key_of(p_node));
    if (is_collider(p_node)) {
        forget_above(p_node);
    }
}

void arm() {
    SceneTree *tree = gd::scene_tree();
    Book &held = book();
    if (tree == nullptr || held.tree == gd::instance_id(tree)) {
        return;
    }
    held.tree = gd::instance_id(tree);
    tree->connect(StringName("node_added"), callable_mp_static(&on_node_added));
    tree->connect(
        StringName("node_removed"),
        callable_mp_static(&on_node_removed)
    );
}

Node *entity_root_of(Node *p_node) {
    for (Node *at = p_node; at != nullptr; at = at->get_parent()) {
        if (tree::is_entity_root(at)) {
            return at;
        }
    }
    return p_node;
}

void collect(Node *p_node, LocalVector<ObjectID> &r_list) {
    const int count = p_node->get_child_count();
    for (int at = 0; at < count; ++at) {
        Node *child = p_node->get_child(at);
        if (tree::is_entity_root(child)
            || book().visual_roots.has(key_of(child))) {
            continue;
        }
        if (is_collider(child)) {
            r_list.push_back(gd::instance_id(child));
        }
        collect(child, r_list);
    }
}

const LocalVector<ObjectID> *list_of(Object *p_owner) {
    Node *node = Object::cast_to<Node>(p_owner);
    if (node == nullptr || !node->is_inside_tree()) {
        return nullptr;
    }
    arm();
    Node *root = entity_root_of(node);
    Book &held = book();
    const uint64_t key = key_of(root);
    if (const LocalVector<ObjectID> *listed = held.lists.getptr(key)) {
        return listed;
    }
    LocalVector<ObjectID> &made = held.lists[key];
    if (is_collider(root)) {
        made.push_back(gd::instance_id(root));
    }
    collect(root, made);
    return &made;
}

void force(Node *p_node) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        spatial->force_update_transform();
    } else if (Node2D *planar = Object::cast_to<Node2D>(p_node)) {
        planar->force_update_transform();
    }
}

bool syncs_to_physics(Node *p_node) {
    const bool animatable = Object::cast_to<AnimatableBody3D>(p_node) != nullptr
        || Object::cast_to<AnimatableBody2D>(p_node) != nullptr;
    return animatable && bool(p_node->get(StringName("sync_to_physics")));
}

bool collides_or_queries(Node *p_node) {
    return is_collider(p_node) || p_node->is_class(StringName("RayCast3D"))
        || p_node->is_class(StringName("RayCast2D"))
        || p_node->is_class(StringName("ShapeCast3D"))
        || p_node->is_class(StringName("ShapeCast2D"));
}

Node *first_collider_under(Node *p_node) {
    const int count = p_node->get_child_count();
    for (int at = 0; at < count; ++at) {
        Node *child = p_node->get_child(at);
        if (collides_or_queries(child)) {
            return child;
        }
        if (Node *below = first_collider_under(child)) {
            return below;
        }
    }
    return nullptr;
}

void push(Node *p_node) {
    if (syncs_to_physics(p_node)
        && first_report(p_node, Fault::SYNC_TO_PHYSICS)) {
        NETW_WARN(
            sys::LAGCOMP,
            "'%s' has sync_to_physics on, so a restore cannot move it. Turn "
            "sync_to_physics off and move it in _network_tick.",
            String(p_node->get_name())
        );
    }
    force(p_node);
    if (is_kinematic(p_node)) {
        gd::flush_kinematic(p_node);
    }
}

Variant server_pose_of(Node *p_node) {
    if (PhysicsBody3D *body = Object::cast_to<PhysicsBody3D>(p_node)) {
        return PhysicsServer3D::get_singleton()->body_get_state(
            body->get_rid(),
            PhysicsServer3D::BODY_STATE_TRANSFORM
        );
    }
    if (PhysicsBody2D *body = Object::cast_to<PhysicsBody2D>(p_node)) {
        return PhysicsServer2D::get_singleton()->body_get_state(
            body->get_rid(),
            PhysicsServer2D::BODY_STATE_TRANSFORM
        );
    }
    return Variant();
}

bool node_rests_at(Node *p_node, const Variant &p_pose) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        return spatial->get_global_transform().is_equal_approx(
            Transform3D(p_pose)
        );
    }
    if (Node2D *planar = Object::cast_to<Node2D>(p_node)) {
        return planar->get_global_transform().is_equal_approx(
            Transform2D(p_pose)
        );
    }
    return true;
}

void restore_pending(Node *p_node, const Variant &p_server) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        const Transform3D local = spatial->get_transform();
        spatial->set_global_transform(Transform3D(p_server));
        spatial->force_update_transform();
        gd::flush_kinematic(spatial);
        spatial->set_transform(local);
        spatial->force_update_transform();
    } else if (Node2D *planar = Object::cast_to<Node2D>(p_node)) {
        const Transform2D local = planar->get_transform();
        planar->set_global_transform(Transform2D(p_server));
        planar->force_update_transform();
        gd::flush_kinematic(planar);
        planar->set_transform(local);
        planar->force_update_transform();
    }
}

const ServerPose *held_for(
    const LocalVector<ServerPose> &p_held,
    ObjectID p_body
) {
    for (const ServerPose &pose : p_held) {
        if (pose.body == p_body) {
            return &pose;
        }
    }
    return nullptr;
}

} // namespace

void teleport(Object *p_owner) {
    const LocalVector<ObjectID> *list = list_of(p_owner);
    if (list == nullptr) {
        return;
    }
    for (const ObjectID &id : *list) {
        Node *collider = node_of(id);
        if (collider != nullptr && collider->is_inside_tree()) {
            push(collider);
        }
    }
}

void settle(Object *p_owner) {
    if (!gd::can_flush_kinematic()) {
        return;
    }
    const LocalVector<ObjectID> *list = list_of(p_owner);
    if (list == nullptr) {
        return;
    }
    for (const ObjectID &id : *list) {
        Node *collider = node_of(id);
        if (collider != nullptr && collider->is_inside_tree()
            && is_kinematic(collider)) {
            force(collider);
            gd::flush_kinematic(collider);
        }
    }
}

bool first_report(const Object *p_subject, Fault p_fault) {
    if (p_subject == nullptr) {
        return false;
    }
    const uint64_t key
        = (uint64_t(p_subject->get_instance_id()) << 3) | uint64_t(p_fault);
    HashMap<uint64_t, uint8_t> &reported = book().reported;
    if (reported.has(key)) {
        return false;
    }
    reported.insert(key, 1);
    return true;
}

Node *first_kinematic_body(Object *p_owner) {
    const LocalVector<ObjectID> *list = list_of(p_owner);
    if (list == nullptr) {
        return nullptr;
    }
    for (const ObjectID &id : *list) {
        Node *collider = node_of(id);
        if (Object::cast_to<CharacterBody3D>(collider) != nullptr
            || Object::cast_to<CharacterBody2D>(collider) != nullptr
            || Object::cast_to<AnimatableBody3D>(collider) != nullptr
            || Object::cast_to<AnimatableBody2D>(collider) != nullptr) {
            return collider;
        }
    }
    return nullptr;
}

void report_unseen(Object *p_owner) {
    if (gd::can_flush_kinematic()) {
        return;
    }
    const LocalVector<ObjectID> *list = list_of(p_owner);
    if (list == nullptr || list->is_empty()) {
        return;
    }
    Node *kinematic = nullptr;
    for (const ObjectID &id : *list) {
        Node *collider = node_of(id);
        if (collider == nullptr || !collider->is_inside_tree()) {
            continue;
        }
        if (!is_kinematic(collider)) {
            return;
        }
        kinematic = collider;
    }
    if (kinematic == nullptr
        || !first_report(kinematic, Fault::UNSEEN_KINEMATIC)) {
        return;
    }
    NETW_WARN(
        sys::LAGCOMP,
        "'%s' is a %s with no hitbox, so a query in a rewind sees it where it "
        "is now. Add an Area3D hitbox, or use the Networked engine build.",
        String(kinematic->get_name()),
        kinematic->get_class()
    );
}

void exclude_visual_root(Node *p_owner, Node *p_visual) {
    if (p_owner == nullptr || p_visual == nullptr || p_visual == p_owner) {
        return;
    }
    Book &held = book();
    const uint64_t key = key_of(p_visual);
    if (held.visual_roots.has(key)) {
        return;
    }
    held.visual_roots.insert(key, 1);
    held.lists.clear();
    Node *inside = collides_or_queries(p_visual)
        ? p_visual
        : first_collider_under(p_visual);
    if (inside == nullptr || !first_report(inside, Fault::VISUAL_ROOT)) {
        return;
    }
    NETW_WARN(
        sys::LAGCOMP,
        "'%s/%s' is under the visual root, so it moves with the drawn body. "
        "Move it out of NetwDisplay.visual_root.",
        String(p_owner->get_name()),
        String(p_owner->get_path_to(inside))
    );
}

void hold_server_poses(Object *p_owner, LocalVector<ServerPose> &r_held) {
    if (!gd::can_flush_kinematic()) {
        return;
    }
    const LocalVector<ObjectID> *list = list_of(p_owner);
    if (list == nullptr) {
        return;
    }
    for (const ObjectID &id : *list) {
        Node *collider = node_of(id);
        if (collider == nullptr || !collider->is_inside_tree()
            || !is_kinematic(collider)) {
            continue;
        }
        const Variant pose = server_pose_of(collider);
        if (!node_rests_at(collider, pose)) {
            r_held.push_back(ServerPose{id, pose});
        }
    }
}

void restore_back(Object *p_owner, const LocalVector<ServerPose> &p_held) {
    const LocalVector<ObjectID> *list = list_of(p_owner);
    if (list == nullptr) {
        return;
    }
    for (const ObjectID &id : *list) {
        Node *collider = node_of(id);
        if (collider == nullptr || !collider->is_inside_tree()) {
            continue;
        }
        const ServerPose *held = held_for(p_held, id);
        if (held != nullptr) {
            restore_pending(collider, held->pose);
        } else {
            push(collider);
        }
    }
}

} // namespace netw::colliders

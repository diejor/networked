#include "netw/sim/body.hpp"

#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"

using namespace godot;

namespace netw::sim {

namespace {

constexpr int FREEZE_MODE_KINEMATIC = 1;

const StringName &freeze_name() {
    static const StringName name("freeze");
    return name;
}

const StringName &freeze_mode_name() {
    static const StringName name("freeze_mode");
    return name;
}

const StringName &global_transform_name() {
    static const StringName name("global_transform");
    return name;
}

const StringName &transform_name() {
    static const StringName name("transform");
    return name;
}

Node *node_of(ObjectID p_id) {
    return Object::cast_to<Node>(gd::object_of(p_id));
}

void hold(Bodies &r_bodies, Node *p_body) {
    const ObjectID id = gd::instance_id(p_body);
    for (const AuthoredBody &held : r_bodies.held) {
        if (held.node == id) {
            return;
        }
    }
    AuthoredBody authored;
    authored.node = id;
    authored.freeze = p_body->get(freeze_name());
    authored.freeze_mode = p_body->get(freeze_mode_name());
    r_bodies.held.push_back(authored);
}

} // namespace

bool is_solver_body(Node *p_node) {
    return p_node != nullptr
        && (p_node->is_class("RigidBody2D") || p_node->is_class("RigidBody3D"));
}

void collect_bodies(
    Node *p_owner,
    const Vector<NodePath> &p_declared,
    LocalVector<ObjectID> &r_bodies
) {
    r_bodies.clear();
    if (p_owner == nullptr) {
        return;
    }
    if (is_solver_body(p_owner)) {
        r_bodies.push_back(gd::instance_id(p_owner));
    }
    for (const NodePath &path : p_declared) {
        Node *body = p_owner->get_node_or_null(path);
        if (!is_solver_body(body)) {
            continue;
        }
        const ObjectID id = gd::instance_id(body);
        if (r_bodies.find(id) < 0) {
            r_bodies.push_back(id);
        }
    }
}

void record_authored(
    Bodies &r_bodies,
    Node *p_owner,
    const Vector<NodePath> &p_declared,
    const NodePath &p_visual_root
) {
    r_bodies.held.clear();
    r_bodies.visual = ObjectID();
    r_bodies.visual_transform = Variant();
    r_bodies.recorded = true;
    if (p_owner == nullptr) {
        return;
    }
    LocalVector<ObjectID> bodies;
    collect_bodies(p_owner, p_declared, bodies);
    for (const ObjectID &id : bodies) {
        hold(r_bodies, node_of(id));
    }
    if (p_visual_root.is_empty()) {
        return;
    }
    Node *visual = p_owner->get_node_or_null(p_visual_root);
    if (visual == nullptr || visual == p_owner) {
        return;
    }
    r_bodies.visual = gd::instance_id(visual);
    r_bodies.visual_transform = visual->get(transform_name());
}

void enter_proxy(const Bodies &p_bodies) {
    for (const AuthoredBody &held : p_bodies.held) {
        Node *body = node_of(held.node);
        if (body == nullptr) {
            continue;
        }
        body->set(freeze_mode_name(), FREEZE_MODE_KINEMATIC);
        body->set(freeze_name(), true);
        if (body->is_inside_tree()) {
            body->set(
                global_transform_name(),
                body->get(global_transform_name())
            );
        }
    }
    if (p_bodies.visual_transform.get_type() == Variant::NIL) {
        return;
    }
    Node *visual = node_of(p_bodies.visual);
    if (visual != nullptr) {
        visual->set(transform_name(), p_bodies.visual_transform);
    }
}

void restore_authored(const Bodies &p_bodies) {
    for (const AuthoredBody &held : p_bodies.held) {
        Node *body = node_of(held.node);
        if (body == nullptr) {
            continue;
        }
        body->set(freeze_name(), held.freeze);
        body->set(freeze_mode_name(), held.freeze_mode);
    }
}

bool transition(Bodies &r_bodies, Mode p_mode) {
    const Mode previous = r_bodies.applied;
    if (previous == p_mode) {
        return false;
    }
    r_bodies.applied = p_mode;
    if (p_mode == Mode::PROXY) {
        enter_proxy(r_bodies);
        return false;
    }
    if (previous != Mode::PROXY) {
        return false;
    }
    restore_authored(r_bodies);
    return true;
}

void release(Bodies &r_bodies) {
    if (r_bodies.applied == Mode::PROXY) {
        restore_authored(r_bodies);
    }
    r_bodies = Bodies();
}

void hold_poses(
    const Bodies &p_bodies,
    const RID &p_space,
    LocalVector<HeldPose> &r_poses
) {
    for (const AuthoredBody &held : p_bodies.held) {
        Node *node = node_of(held.node);
        HeldPose pose;
        if (RigidBody3D *solid = Object::cast_to<RigidBody3D>(node)) {
            PhysicsServer3D *server = PhysicsServer3D::get_singleton();
            pose.body = solid->get_rid();
            pose.dimension = 3;
            if (server->body_get_space(pose.body) != p_space) {
                continue;
            }
            pose.transform = server->body_get_state(
                pose.body,
                PhysicsServer3D::BODY_STATE_TRANSFORM
            );
            pose.linear = server->body_get_state(
                pose.body,
                PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY
            );
            pose.angular = server->body_get_state(
                pose.body,
                PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY
            );
        } else if (RigidBody2D *flat = Object::cast_to<RigidBody2D>(node)) {
            PhysicsServer2D *server = PhysicsServer2D::get_singleton();
            pose.body = flat->get_rid();
            pose.dimension = 2;
            if (server->body_get_space(pose.body) != p_space) {
                continue;
            }
            pose.transform = server->body_get_state(
                pose.body,
                PhysicsServer2D::BODY_STATE_TRANSFORM
            );
            pose.linear = server->body_get_state(
                pose.body,
                PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY
            );
            pose.angular = server->body_get_state(
                pose.body,
                PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY
            );
        } else {
            continue;
        }
        r_poses.push_back(pose);
    }
}

void restore_poses(const LocalVector<HeldPose> &p_poses) {
    for (const HeldPose &pose : p_poses) {
        if (pose.dimension == 3) {
            PhysicsServer3D *server = PhysicsServer3D::get_singleton();
            server->body_set_state(
                pose.body,
                PhysicsServer3D::BODY_STATE_TRANSFORM,
                pose.transform
            );
            server->body_set_state(
                pose.body,
                PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY,
                pose.linear
            );
            server->body_set_state(
                pose.body,
                PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY,
                pose.angular
            );
            continue;
        }
        PhysicsServer2D *server = PhysicsServer2D::get_singleton();
        server->body_set_state(
            pose.body,
            PhysicsServer2D::BODY_STATE_TRANSFORM,
            pose.transform
        );
        server->body_set_state(
            pose.body,
            PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY,
            pose.linear
        );
        server->body_set_state(
            pose.body,
            PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY,
            pose.angular
        );
    }
}

} // namespace netw::sim

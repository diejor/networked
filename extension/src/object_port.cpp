#include "netw/object_port.hpp"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

bool ObjectPort::bound() const {
    return id.is_valid();
}

bool ObjectPort::bind(Object *p_owner) {
    if (p_owner == nullptr) {
        unbind();
        return false;
    }
    id = p_owner->get_instance_id();
    return true;
}

void ObjectPort::unbind() {
    id = ObjectID();
}

Object *ObjectPort::resolve(SubsystemName p_module) {
    if (!id.is_valid()) {
        return nullptr;
    }
    Object *found = gd::instance_from_id(id);
    if (found != nullptr) {
        return found;
    }
    id = ObjectID();
    lost += 1;
    NETW_WARN_ONCE(
        p_module,
        "a bound slot's owner was freed; the slot unbound itself and stopped "
        "touching properties"
    );
    return nullptr;
}

namespace {

enum class BodyKey { NONE, POSITION, TURN, LINEAR, ANGULAR, SLEEPING };

const StringName &position_name() {
    static const StringName name("position");
    return name;
}

const StringName &quaternion_name() {
    static const StringName name("quaternion");
    return name;
}

const StringName &rotation_name() {
    static const StringName name("rotation");
    return name;
}

const StringName &linear_velocity_name() {
    static const StringName name("linear_velocity");
    return name;
}

const StringName &angular_velocity_name() {
    static const StringName name("angular_velocity");
    return name;
}

const StringName &sleeping_name() {
    static const StringName name("sleeping");
    return name;
}

const StringName &entity_mark() {
    static const StringName name = NetwEntityRecord::entity_meta();
    return name;
}

BodyKey body_key_of(const StringName &p_key, const StringName &p_turn) {
    if (p_key == position_name()) {
        return BodyKey::POSITION;
    }
    if (p_key == p_turn) {
        return BodyKey::TURN;
    }
    if (p_key == linear_velocity_name()) {
        return BodyKey::LINEAR;
    }
    if (p_key == angular_velocity_name()) {
        return BodyKey::ANGULAR;
    }
    if (p_key == sleeping_name()) {
        return BodyKey::SLEEPING;
    }
    return BodyKey::NONE;
}

bool names_body_state(const StringName &p_key) {
    return p_key == position_name() || p_key == quaternion_name()
        || p_key == rotation_name() || p_key == linear_velocity_name()
        || p_key == angular_velocity_name() || p_key == sleeping_name();
}

template <typename T>
T *root_body(Object *p_owner) {
    T *body = Object::cast_to<T>(p_owner);
    if (body == nullptr || !body->is_inside_tree()
        || !body->has_meta(entity_mark())) {
        return nullptr;
    }
    return body;
}

Transform3D frame_of(RigidBody3D *p_body) {
    Node3D *parent = p_body->get_parent_node_3d();
    if (parent == nullptr || p_body->is_set_as_top_level()) {
        return Transform3D();
    }
    return parent->get_global_transform();
}

Transform2D frame_of(RigidBody2D *p_body) {
    CanvasItem *parent = Object::cast_to<CanvasItem>(p_body->get_parent());
    if (parent == nullptr || p_body->is_set_as_top_level()) {
        return Transform2D();
    }
    return parent->get_global_transform();
}

bool read_3d(RigidBody3D *p_body, BodyKey p_key, Variant &r_value) {
    PhysicsServer3D *server = PhysicsServer3D::get_singleton();
    const RID body = p_body->get_rid();
    switch (p_key) {
        case BodyKey::POSITION:
        case BodyKey::TURN: {
            if (p_body->is_freeze_enabled()) {
                return false;
            }
            const Transform3D global = server->body_get_state(
                body,
                PhysicsServer3D::BODY_STATE_TRANSFORM
            );
            const Transform3D local = frame_of(p_body).affine_inverse()
                * global;
            r_value = p_key == BodyKey::POSITION
                ? Variant(local.origin)
                : Variant(local.basis.get_rotation_quaternion());
            return true;
        }
        case BodyKey::LINEAR:
            r_value = server->body_get_state(
                body,
                PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY
            );
            return true;
        case BodyKey::ANGULAR:
            r_value = server->body_get_state(
                body,
                PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY
            );
            return true;
        case BodyKey::SLEEPING:
            r_value = server->body_get_state(
                body,
                PhysicsServer3D::BODY_STATE_SLEEPING
            );
            return true;
        case BodyKey::NONE:
            return false;
    }
    return false;
}

bool read_2d(RigidBody2D *p_body, BodyKey p_key, Variant &r_value) {
    PhysicsServer2D *server = PhysicsServer2D::get_singleton();
    const RID body = p_body->get_rid();
    switch (p_key) {
        case BodyKey::POSITION:
        case BodyKey::TURN: {
            if (p_body->is_freeze_enabled()) {
                return false;
            }
            const Transform2D global = server->body_get_state(
                body,
                PhysicsServer2D::BODY_STATE_TRANSFORM
            );
            const Transform2D local = frame_of(p_body).affine_inverse()
                * global;
            r_value = p_key == BodyKey::POSITION
                ? Variant(local.get_origin())
                : Variant(double(local.get_rotation()));
            return true;
        }
        case BodyKey::LINEAR:
            r_value = server->body_get_state(
                body,
                PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY
            );
            return true;
        case BodyKey::ANGULAR:
            r_value = server->body_get_state(
                body,
                PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY
            );
            return true;
        case BodyKey::SLEEPING:
            r_value = server->body_get_state(
                body,
                PhysicsServer2D::BODY_STATE_SLEEPING
            );
            return true;
        case BodyKey::NONE:
            return false;
    }
    return false;
}

bool write_3d(RigidBody3D *p_body, BodyKey p_key, const Variant &p_value) {
    const bool placed = p_key == BodyKey::POSITION
        && p_value.get_type() == Variant::VECTOR3;
    const bool turned = p_key == BodyKey::TURN
        && p_value.get_type() == Variant::QUATERNION;
    if ((!placed && !turned) || p_body->is_freeze_enabled()) {
        return false;
    }
    PhysicsServer3D *server = PhysicsServer3D::get_singleton();
    const RID body = p_body->get_rid();
    const Transform3D frame = frame_of(p_body);
    const Transform3D global
        = server->body_get_state(body, PhysicsServer3D::BODY_STATE_TRANSFORM);
    Transform3D local = frame.affine_inverse() * global;
    if (placed) {
        local.origin = p_value;
    } else {
        local.basis = Basis(Quaternion(p_value).normalized());
    }
    const Transform3D placed_global = frame * local;
    server->body_set_state(
        body,
        PhysicsServer3D::BODY_STATE_TRANSFORM,
        placed_global
    );
    netw::gd::set_ignore_transform_notification(p_body, true);
    p_body->set_global_transform(placed_global);
    netw::gd::set_ignore_transform_notification(p_body, false);
    return true;
}

bool write_2d(RigidBody2D *p_body, BodyKey p_key, const Variant &p_value) {
    const bool placed = p_key == BodyKey::POSITION
        && p_value.get_type() == Variant::VECTOR2;
    const bool turned = p_key == BodyKey::TURN
        && (p_value.get_type() == Variant::FLOAT
            || p_value.get_type() == Variant::INT);
    if ((!placed && !turned) || p_body->is_freeze_enabled()) {
        return false;
    }
    PhysicsServer2D *server = PhysicsServer2D::get_singleton();
    const RID body = p_body->get_rid();
    const Transform2D frame = frame_of(p_body);
    const Transform2D global
        = server->body_get_state(body, PhysicsServer2D::BODY_STATE_TRANSFORM);
    Transform2D local = frame.affine_inverse() * global;
    if (placed) {
        local.set_origin(p_value);
    } else {
        local = Transform2D(real_t(double(p_value)), local.get_origin());
    }
    const Transform2D placed_global = frame * local;
    server->body_set_state(
        body,
        PhysicsServer2D::BODY_STATE_TRANSFORM,
        placed_global
    );
    const bool notifies = p_body->is_transform_notification_enabled();
    p_body->set_notify_transform(false);
    p_body->set_global_transform(placed_global);
    p_body->set_notify_transform(notifies);
    return true;
}

} // namespace

Variant port_get(Object *p_owner, const StringName &p_key) {
    if (p_owner == nullptr) {
        return Variant();
    }
    if (names_body_state(p_key)) {
        Variant value;
        if (RigidBody3D *solid = root_body<RigidBody3D>(p_owner)) {
            if (read_3d(solid, body_key_of(p_key, quaternion_name()), value)) {
                return value;
            }
        } else if (RigidBody2D *flat = root_body<RigidBody2D>(p_owner)) {
            if (read_2d(flat, body_key_of(p_key, rotation_name()), value)) {
                return value;
            }
        }
    }
    return p_owner->get(p_key);
}

bool port_rests(Object *p_owner, const StringName &p_key) {
    if (p_owner == nullptr || !names_body_state(p_key)) {
        return false;
    }
    if (RigidBody3D *solid = Object::cast_to<RigidBody3D>(p_owner)) {
        return solid->is_inside_tree() && !solid->is_freeze_enabled()
            && bool(PhysicsServer3D::get_singleton()->body_get_state(
                solid->get_rid(),
                PhysicsServer3D::BODY_STATE_SLEEPING
            ));
    }
    if (RigidBody2D *flat = Object::cast_to<RigidBody2D>(p_owner)) {
        return flat->is_inside_tree() && !flat->is_freeze_enabled()
            && bool(PhysicsServer2D::get_singleton()->body_get_state(
                flat->get_rid(),
                PhysicsServer2D::BODY_STATE_SLEEPING
            ));
    }
    return false;
}

void port_set(
    Object *p_owner,
    const StringName &p_key,
    const Variant &p_value
) {
    if (p_owner == nullptr) {
        return;
    }
    if (names_body_state(p_key)) {
        if (RigidBody3D *solid = root_body<RigidBody3D>(p_owner)) {
            if (write_3d(
                    solid,
                    body_key_of(p_key, quaternion_name()),
                    p_value
                )) {
                return;
            }
        } else if (RigidBody2D *flat = root_body<RigidBody2D>(p_owner)) {
            if (write_2d(flat, body_key_of(p_key, rotation_name()), p_value)) {
                return;
            }
        }
    }
    p_owner->set(p_key, p_value);
}

Dictionary port_capture(
    ObjectPort &r_port,
    SubsystemName p_module,
    const Array &p_keys
) {
    Dictionary out;
    Object *owner = r_port.resolve(p_module);
    if (owner == nullptr) {
        return out;
    }
    for (int at = 0; at < p_keys.size(); ++at) {
        const StringName key = p_keys[at];
        out[key] = port_get(owner, key);
    }
    return out;
}

bool port_apply(
    ObjectPort &r_port,
    SubsystemName p_module,
    const Dictionary &p_payload
) {
    Object *owner = r_port.resolve(p_module);
    if (owner == nullptr) {
        return false;
    }
    const Array keys = p_payload.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        port_set(owner, key, p_payload[key]);
    }
    return true;
}

void port_sync_transform(Object *p_owner) {
    const StringName sync_transform("force_update_transform");
    Node *node = Object::cast_to<Node>(p_owner);
    if (node != nullptr && node->is_inside_tree()
        && node->has_method(sync_transform)) {
        node->call(sync_transform);
    }
}

} // namespace netw

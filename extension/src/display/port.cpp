#include "netw/display/port.hpp"

#include "godot/spatial_node.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

namespace {

const StringName &channel_position() {
    static const StringName name = StringName("position");
    return name;
}

const StringName &channel_rotation() {
    static const StringName name = StringName("rotation");
    return name;
}

const StringName &channel_quaternion() {
    static const StringName name = StringName("quaternion");
    return name;
}

const StringName &channel_basis() {
    static const StringName name = StringName("basis");
    return name;
}

const StringName &channel_global_position() {
    static const StringName name = StringName("global_position");
    return name;
}

const StringName &channel_global_rotation() {
    static const StringName name = StringName("global_rotation");
    return name;
}

Node3D *spatial_frame(Node *p_node) {
    Node3D *spatial = Object::cast_to<Node3D>(p_node);
    return spatial != nullptr && spatial->is_inside_tree() ? spatial : nullptr;
}

Transform3D unmounted_frame(Node *p_target, Node *p_host_parent) {
    Transform3D frame;
    for (Node *at = p_target->get_parent();
         at != nullptr && at != p_host_parent;
         at = at->get_parent()) {
        Node3D *spatial = Object::cast_to<Node3D>(at);
        if (spatial != nullptr) {
            frame = spatial->get_transform() * frame;
        }
    }
    return frame;
}

bool names_position(const StringName &p_prop) {
    return p_prop == channel_position() || p_prop == channel_global_position();
}

bool names_rotation(const StringName &p_prop) {
    return p_prop == channel_rotation() || p_prop == channel_global_rotation();
}

} // namespace

void Port::bind(Object *p_target, Object *p_host) {
    target.bind(p_target);
    host.bind(p_host);
}

void Port::unbind() {
    target.unbind();
    host.unbind();
}

bool Port::is_bound() const {
    return target.bound();
}

void Port::declare(
    const StringName &p_target_prop,
    const StringName &p_source_prop,
    bool p_global_space
) {
    target_prop = p_target_prop;
    source_prop = p_source_prop;
    global_space = p_global_space;
}

StringName Port::get_target_prop() const {
    return target_prop;
}

bool Port::get_global_space() const {
    return global_space;
}

int64_t Port::get_lost() const {
    return target.lost;
}

Node *Port::host_parent() {
    Node *node = Object::cast_to<Node>(host.resolve(sys::INTERPOLATION));
    return node != nullptr ? node->get_parent() : nullptr;
}

Vector2 Port::host_position_2d(const Vector2 &p_host_local) {
    if (source_prop == channel_global_position()) {
        return p_host_local;
    }
    Node2D *parent = Object::cast_to<Node2D>(host_parent());
    return parent != nullptr ? parent->to_global(p_host_local) : p_host_local;
}

Vector3 Port::host_position_3d(const Vector3 &p_host_local) {
    if (source_prop == channel_global_position()) {
        return p_host_local;
    }
    Node3D *parent = spatial_frame(host_parent());
    return parent != nullptr ? parent->to_global(p_host_local) : p_host_local;
}

double Port::host_rotation_2d(double p_host_local) {
    if (source_prop == channel_global_rotation()) {
        return p_host_local;
    }
    Node2D *parent = Object::cast_to<Node2D>(host_parent());
    if (parent == nullptr) {
        return p_host_local;
    }
    return double(parent->get_global_rotation()) + p_host_local;
}

Vector3 Port::host_rotation_3d(const Vector3 &p_host_local) {
    if (source_prop == channel_global_rotation()) {
        return p_host_local;
    }
    Node3D *parent = spatial_frame(host_parent());
    if (parent == nullptr) {
        return p_host_local;
    }
    const Basis composed = parent->get_global_transform().basis
        * Basis::from_euler(p_host_local);
    return composed.get_euler();
}

bool Port::turns() const {
    return names_rotation(target_prop) || target_prop == channel_quaternion()
        || target_prop == channel_basis();
}

void Port::seat_authored(const Variant &p_local_transform) {
    offset_seated = global_space
        &&p_local_transform.get_type() == Variant::TRANSFORM3D
        && Transform3D(p_local_transform) != Transform3D();
    authored_offset
        = offset_seated ? Transform3D(p_local_transform) : Transform3D();
}

Basis Port::body_basis(Node3D *p_target) {
    Basis shown = p_target->get_basis();
    if (spatial_frame(p_target) != nullptr) {
        shown = p_target->get_global_transform().basis;
    } else {
        shown = unmounted_frame(p_target, host_parent()).basis * shown;
    }
    return shown.orthonormalized() * authored_offset.basis.inverse();
}

Vector3 Port::offset_position(
    Node3D *p_target,
    const Vector3 &p_body_position
) {
    if (!offset_seated) {
        return p_body_position;
    }
    return p_body_position + body_basis(p_target).xform(authored_offset.origin);
}

int64_t Port::write_orientation(
    Node3D *p_target,
    const Basis &p_host_local,
    bool p_as_quaternion
) {
    Node3D *parent = spatial_frame(host_parent());
    const Basis parent_basis = parent != nullptr
        ? parent->get_global_transform().basis.orthonormalized()
        : Basis();
    const Basis composed = parent_basis * p_host_local
        * (offset_seated ? authored_offset.basis : Basis());
    if (spatial_frame(p_target) == nullptr) {
        const Basis frame = unmounted_frame(p_target, host_parent())
                                .basis.orthonormalized();
        const Basis local = frame.inverse() * composed;
        if (p_as_quaternion) {
            p_target->set_quaternion(local.get_rotation_quaternion());
        } else {
            p_target->set_basis(local);
        }
        return WRITE_LOCAL;
    }
    Transform3D global = p_target->get_global_transform();
    global.basis = p_as_quaternion ? composed.scaled(p_target->get_scale())
                                   : composed;
    p_target->set_global_transform(global);
    return WRITE_GLOBAL;
}

int64_t Port::write_global(Object *p_target, const Variant &p_value) {
    if (target_prop == channel_quaternion()
        || target_prop == channel_basis()) {
        Node3D *node_3d = Object::cast_to<Node3D>(p_target);
        const bool as_quaternion = target_prop == channel_quaternion();
        if (node_3d != nullptr && as_quaternion
            && p_value.get_type() == Variant::QUATERNION) {
            return write_orientation(
                node_3d,
                Basis(Quaternion(p_value)),
                true
            );
        }
        if (node_3d != nullptr && !as_quaternion
            && p_value.get_type() == Variant::BASIS) {
            return write_orientation(node_3d, Basis(p_value), false);
        }
        return WRITE_REFUSED;
    }
    if (names_position(target_prop)) {
        Node2D *node_2d = Object::cast_to<Node2D>(p_target);
        if (node_2d != nullptr && p_value.get_type() == Variant::VECTOR2) {
            node_2d->set_global_position(host_position_2d(p_value));
            return WRITE_GLOBAL;
        }
        Node3D *node_3d = Object::cast_to<Node3D>(p_target);
        if (node_3d != nullptr && p_value.get_type() == Variant::VECTOR3) {
            if (spatial_frame(node_3d) == nullptr) {
                node_3d->set_position(
                    unmounted_frame(node_3d, host_parent())
                        .affine_inverse()
                        .xform(offset_position(
                            node_3d,
                            host_position_3d(p_value)
                        ))
                );
                return WRITE_LOCAL;
            }
            node_3d->set_global_position(
                offset_position(node_3d, host_position_3d(p_value))
            );
            return WRITE_GLOBAL;
        }
    } else if (names_rotation(target_prop)) {
        Node2D *node_2d = Object::cast_to<Node2D>(p_target);
        if (node_2d != nullptr && p_value.get_type() == Variant::FLOAT) {
            node_2d->set_global_rotation(host_rotation_2d(p_value));
            return WRITE_GLOBAL;
        }
        Node3D *node_3d = Object::cast_to<Node3D>(p_target);
        if (node_3d != nullptr && p_value.get_type() == Variant::VECTOR3) {
            if (spatial_frame(node_3d) == nullptr) {
                const Basis frame = unmounted_frame(node_3d, host_parent())
                                        .basis.orthonormalized();
                node_3d->set_rotation(
                    (frame.inverse()
                     * Basis::from_euler(host_rotation_3d(p_value))
                     * authored_offset.basis)
                        .get_euler()
                );
                return WRITE_LOCAL;
            }
            node_3d->set_global_rotation(
                offset_seated ? (Basis::from_euler(host_rotation_3d(p_value))
                                 * authored_offset.basis)
                                    .get_euler()
                              : host_rotation_3d(p_value)
            );
            return WRITE_GLOBAL;
        }
    }
    return WRITE_REFUSED;
}

int64_t Port::write(const Variant &p_value) {
    Object *node = target.resolve(sys::INTERPOLATION);
    if (node == nullptr) {
        return WRITE_UNBOUND;
    }
    if (global_space) {
        const int64_t verdict = write_global(node, p_value);
        if (verdict != WRITE_REFUSED) {
            return verdict;
        }
        port_set(node, target_prop, p_value);
        return WRITE_REFUSED;
    }
    port_set(node, target_prop, p_value);
    return WRITE_LOCAL;
}

} // namespace netw::display

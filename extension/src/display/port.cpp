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

int64_t Port::write_global(Object *p_target, const Variant &p_value) {
    if (names_position(target_prop)) {
        Node2D *node_2d = Object::cast_to<Node2D>(p_target);
        if (node_2d != nullptr && p_value.get_type() == Variant::VECTOR2) {
            node_2d->set_global_position(host_position_2d(p_value));
            return WRITE_GLOBAL;
        }
        Node3D *node_3d = Object::cast_to<Node3D>(p_target);
        if (node_3d != nullptr && p_value.get_type() == Variant::VECTOR3) {
            if (spatial_frame(node_3d) == nullptr) {
                node_3d->set_position(host_position_3d(p_value));
                return WRITE_LOCAL;
            }
            node_3d->set_global_position(host_position_3d(p_value));
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
                node_3d->set_rotation(host_rotation_3d(p_value));
                return WRITE_LOCAL;
            }
            node_3d->set_global_rotation(host_rotation_3d(p_value));
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
        node->set(target_prop, p_value);
        return WRITE_REFUSED;
    }
    node->set(target_prop, p_value);
    return WRITE_LOCAL;
}

} // namespace netw::display

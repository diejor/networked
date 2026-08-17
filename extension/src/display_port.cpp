#include "netw/display_port.hpp"

#include "godot/class_db.hpp"
#include "godot/spatial_node.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

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

} // namespace

void NetwDisplayPort::bind(Object *p_target, Object *p_host) {
    target.bind(p_target);
    host.bind(p_host);
}

void NetwDisplayPort::unbind() {
    target.unbind();
    host.unbind();
}

bool NetwDisplayPort::is_bound() const {
    return target.bound();
}

void NetwDisplayPort::declare(
    const StringName &p_target_prop,
    const StringName &p_source_prop,
    bool p_global_space
) {
    target_prop = p_target_prop;
    source_prop = p_source_prop;
    global_space = p_global_space;
}

StringName NetwDisplayPort::get_target_prop() const {
    return target_prop;
}

bool NetwDisplayPort::get_global_space() const {
    return global_space;
}

int64_t NetwDisplayPort::get_lost() const {
    return target.lost;
}

Node *NetwDisplayPort::host_parent() {
    Node *node = Object::cast_to<Node>(host.resolve(sys::INTERPOLATION));
    return node != nullptr ? node->get_parent() : nullptr;
}

Vector2 NetwDisplayPort::host_position_2d(const Vector2 &p_host_local) {
    if (source_prop == channel_global_position()) {
        return p_host_local;
    }
    Node2D *parent = Object::cast_to<Node2D>(host_parent());
    return parent != nullptr ? parent->to_global(p_host_local) : p_host_local;
}

Vector3 NetwDisplayPort::host_position_3d(const Vector3 &p_host_local) {
    if (source_prop == channel_global_position()) {
        return p_host_local;
    }
    Node3D *parent = Object::cast_to<Node3D>(host_parent());
    return parent != nullptr ? parent->to_global(p_host_local) : p_host_local;
}

double NetwDisplayPort::host_rotation_2d(double p_host_local) {
    if (source_prop == channel_global_rotation()) {
        return p_host_local;
    }
    Node2D *parent = Object::cast_to<Node2D>(host_parent());
    if (parent == nullptr) {
        return p_host_local;
    }
    return double(parent->get_global_rotation()) + p_host_local;
}

Vector3 NetwDisplayPort::host_rotation_3d(const Vector3 &p_host_local) {
    if (source_prop == channel_global_rotation()) {
        return p_host_local;
    }
    Node3D *parent = Object::cast_to<Node3D>(host_parent());
    if (parent == nullptr) {
        return p_host_local;
    }
    const Basis composed
        = parent->get_global_transform().basis * Basis::from_euler(p_host_local);
    return composed.get_euler();
}

int64_t NetwDisplayPort::write_global(Object *p_target, const Variant &p_value) {
    if (target_prop == channel_position()) {
        Node2D *node_2d = Object::cast_to<Node2D>(p_target);
        if (node_2d != nullptr && p_value.get_type() == Variant::VECTOR2) {
            node_2d->set_global_position(host_position_2d(p_value));
            return WRITE_GLOBAL;
        }
        Node3D *node_3d = Object::cast_to<Node3D>(p_target);
        if (node_3d != nullptr && p_value.get_type() == Variant::VECTOR3) {
            node_3d->set_global_position(host_position_3d(p_value));
            return WRITE_GLOBAL;
        }
    } else if (target_prop == channel_rotation()) {
        Node2D *node_2d = Object::cast_to<Node2D>(p_target);
        if (node_2d != nullptr && p_value.get_type() == Variant::FLOAT) {
            node_2d->set_global_rotation(host_rotation_2d(p_value));
            return WRITE_GLOBAL;
        }
        Node3D *node_3d = Object::cast_to<Node3D>(p_target);
        if (node_3d != nullptr && p_value.get_type() == Variant::VECTOR3) {
            node_3d->set_global_rotation(host_rotation_3d(p_value));
            return WRITE_GLOBAL;
        }
    }
    return WRITE_REFUSED;
}

int64_t NetwDisplayPort::write(const Variant &p_value) {
    Object *node = target.resolve(sys::INTERPOLATION);
    if (node == nullptr) {
        return WRITE_UNBOUND;
    }
    if (global_space) {
        const int64_t verdict = write_global(node, p_value);
        if (verdict == WRITE_GLOBAL) {
            return verdict;
        }
        node->set(target_prop, p_value);
        return WRITE_REFUSED;
    }
    node->set(target_prop, p_value);
    return WRITE_LOCAL;
}

void NetwDisplayPort::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("bind", "target", "host"),
        &NetwDisplayPort::bind
    );
    ClassDB::bind_method(D_METHOD("unbind"), &NetwDisplayPort::unbind);
    ClassDB::bind_method(D_METHOD("is_bound"), &NetwDisplayPort::is_bound);
    ClassDB::bind_method(
        D_METHOD("declare", "target_prop", "source_prop", "global_space"),
        &NetwDisplayPort::declare
    );
    ClassDB::bind_method(
        D_METHOD("get_target_prop"),
        &NetwDisplayPort::get_target_prop
    );
    ClassDB::bind_method(
        D_METHOD("get_global_space"),
        &NetwDisplayPort::get_global_space
    );
    ClassDB::bind_method(D_METHOD("get_lost"), &NetwDisplayPort::get_lost);
    ClassDB::bind_method(D_METHOD("write", "value"), &NetwDisplayPort::write);

    BIND_ENUM_CONSTANT(WRITE_UNBOUND);
    BIND_ENUM_CONSTANT(WRITE_LOCAL);
    BIND_ENUM_CONSTANT(WRITE_GLOBAL);
    BIND_ENUM_CONSTANT(WRITE_REFUSED);
}

} // namespace netw

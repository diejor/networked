#include <cstring>

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/spatial_node.hpp"
#include "netw/schema_core.hpp"
#include "netw/store/port.hpp"

using namespace godot;

namespace netw::store {

namespace {

Transform3D frame_of(RigidBody3D *p_body, Node3D *p_parent) {
    if (p_parent == nullptr || p_body->is_set_as_top_level()) {
        return Transform3D();
    }
    return p_parent->get_global_transform();
}

Transform2D frame_of(RigidBody2D *p_body, CanvasItem *p_parent) {
    if (p_parent == nullptr || p_body->is_set_as_top_level()) {
        return Transform2D();
    }
    return p_parent->get_global_transform();
}

template <typename T> void put(uint8_t *p_at, const T &p_value) {
    memcpy(p_at, &p_value, sizeof(T));
}

} // namespace

void body_probe(Object *p_node, BodyState &r_state) {
    body_probe(Residents::derive_object(p_node), r_state);
}

void body_probe(const NodeCell &p_cell, BodyState &r_state) {
    r_state = BodyState();
    if (RigidBody3D *solid = p_cell.solid) {
        r_state.body = solid;
        r_state.solid = true;
        r_state.root = p_cell.root;
        if (!r_state.root) {
            return;
        }
        r_state.state_3d = p_cell.state_3d;
        r_state.frame_3d = p_cell.frame_3d;
        r_state.frozen = solid->is_freeze_enabled();
        r_state.sleeping
            = p_cell.state_3d != nullptr && p_cell.state_3d->is_sleeping();
        return;
    }
    if (RigidBody2D *flat = p_cell.flat) {
        r_state.body = flat;
        r_state.root = p_cell.root;
        if (!r_state.root) {
            return;
        }
        r_state.frame_2d = p_cell.frame_2d;
        r_state.frozen = flat->is_freeze_enabled();
        r_state.sleeping
            = bool(PhysicsServer2D::get_singleton()->body_get_state(
                p_cell.body,
                PhysicsServer2D::BODY_STATE_SLEEPING
            ));
    }
}

void body_read(BodyState &r_state) {
    if (r_state.read || !r_state.root) {
        return;
    }
    r_state.read = true;
    if (r_state.solid) {
        RigidBody3D *solid = static_cast<RigidBody3D *>(r_state.body);
        PhysicsDirectBodyState3D *state = r_state.state_3d;
        if (state == nullptr) {
            return;
        }
        r_state.local_3d = frame_of(solid, r_state.frame_3d).affine_inverse()
            * state->get_transform();
        r_state.linear_3d = state->get_linear_velocity();
        r_state.angular_3d = state->get_angular_velocity();
        r_state.sleeping = state->is_sleeping();
        return;
    }
    RigidBody2D *flat = static_cast<RigidBody2D *>(r_state.body);
    PhysicsServer2D *server = PhysicsServer2D::get_singleton();
    const RID body = flat->get_rid();
    const Transform2D global
        = server->body_get_state(body, PhysicsServer2D::BODY_STATE_TRANSFORM);
    r_state.local_2d
        = frame_of(flat, r_state.frame_2d).affine_inverse() * global;
    r_state.linear_2d = server->body_get_state(
        body,
        PhysicsServer2D::BODY_STATE_LINEAR_VELOCITY
    );
    r_state.angular_2d = double(server->body_get_state(
        body,
        PhysicsServer2D::BODY_STATE_ANGULAR_VELOCITY
    ));
}

Variant body_value(
    const BodyState &p_state,
    BodyKey p_key,
    Object *p_node,
    const StringName &p_name
) {
    if (!p_state.root || !p_state.read) {
        return object_value(p_node, p_name);
    }
    const bool placed = p_key == BodyKey::POSITION || p_key == BodyKey::TURN;
    if (placed && p_state.frozen) {
        return object_value(p_node, p_name);
    }
    if (p_state.solid) {
        switch (p_key) {
            case BodyKey::POSITION:
                return p_state.local_3d.origin;
            case BodyKey::TURN:
                return p_state.local_3d.basis.get_rotation_quaternion();
            case BodyKey::LINEAR:
                return p_state.linear_3d;
            case BodyKey::ANGULAR:
                return p_state.angular_3d;
            case BodyKey::SLEEPING:
                return p_state.sleeping;
            case BodyKey::NONE:
                break;
        }
        return object_value(p_node, p_name);
    }
    switch (p_key) {
        case BodyKey::POSITION:
            return p_state.local_2d.get_origin();
        case BodyKey::TURN:
            return double(p_state.local_2d.get_rotation());
        case BodyKey::LINEAR:
            return p_state.linear_2d;
        case BodyKey::ANGULAR:
            return p_state.angular_2d;
        case BodyKey::SLEEPING:
            return p_state.sleeping;
        case BodyKey::NONE:
            break;
    }
    return object_value(p_node, p_name);
}

bool body_typed(
    const BodyState &p_state,
    BodyKey p_key,
    int p_type,
    uint8_t *r_value
) {
    if (!p_state.root || !p_state.read) {
        return false;
    }
    const bool placed = p_key == BodyKey::POSITION || p_key == BodyKey::TURN;
    if (placed && p_state.frozen) {
        return false;
    }
    const bool scalar = p_type == SchemaCore::F32 || p_type == SchemaCore::F64;
    if (p_key == BodyKey::SLEEPING) {
        if (p_type != SchemaCore::BOOL) {
            return false;
        }
        *r_value = p_state.sleeping ? 1 : 0;
        return true;
    }
    if (p_state.solid) {
        if (p_key == BodyKey::TURN) {
            if (p_type != SchemaCore::QUATERNION) {
                return false;
            }
            put(r_value, p_state.local_3d.basis.get_rotation_quaternion());
            return true;
        }
        if (p_type != SchemaCore::VECTOR3) {
            return false;
        }
        switch (p_key) {
            case BodyKey::POSITION:
                put(r_value, p_state.local_3d.origin);
                return true;
            case BodyKey::LINEAR:
                put(r_value, p_state.linear_3d);
                return true;
            case BodyKey::ANGULAR:
                put(r_value, p_state.angular_3d);
                return true;
            default:
                return false;
        }
    }
    switch (p_key) {
        case BodyKey::POSITION:
            if (p_type != SchemaCore::VECTOR2) {
                return false;
            }
            put(r_value, p_state.local_2d.get_origin());
            return true;
        case BodyKey::LINEAR:
            if (p_type != SchemaCore::VECTOR2) {
                return false;
            }
            put(r_value, p_state.linear_2d);
            return true;
        case BodyKey::TURN:
            if (!scalar) {
                return false;
            }
            put(r_value, double(p_state.local_2d.get_rotation()));
            return true;
        case BodyKey::ANGULAR:
            if (!scalar) {
                return false;
            }
            put(r_value, p_state.angular_2d);
            return true;
        default:
            return false;
    }
}

} // namespace netw::store

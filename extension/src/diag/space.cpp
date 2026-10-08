#include "netw/diag/space.hpp"

#if defined(DEBUG_ENABLED)

#include "godot/camera.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"

using namespace godot;

namespace netw::diag {

bool point_of(const Variant &p_value, Vector3 &r_point) {
    switch (p_value.get_type()) {
        case Variant::VECTOR3:
            r_point = p_value;
            return true;
        case Variant::VECTOR2: {
            const Vector2 flat = p_value;
            r_point = Vector3(flat.x, flat.y, 0.0f);
            return true;
        }
        case Variant::TRANSFORM3D:
            r_point = Transform3D(p_value).origin;
            return true;
        case Variant::TRANSFORM2D: {
            const Vector2 origin = Transform2D(p_value).get_origin();
            r_point = Vector3(origin.x, origin.y, 0.0f);
            return true;
        }
        default:
            return false;
    }
}

bool parent_relative(const StringName &p_key) {
    return p_key == StringName("position") || p_key == StringName("transform");
}

bool global_key(const StringName &p_key) {
    return String(p_key).begins_with("global_");
}

Vector3 Lift::apply(const Vector3 &p_point) const {
    switch (kind) {
        case KIND_SPATIAL:
            return spatial.xform(p_point);
        case KIND_FLAT: {
            const Vector2 world = flat.xform(Vector2(p_point.x, p_point.y));
            return Vector3(world.x, world.y, 0.0f);
        }
        default:
            return p_point;
    }
}

Lift lift_of(Node *p_frame, bool p_parent_key, bool p_global_key) {
    Lift lift;
    if (p_global_key) {
        return lift;
    }
    if (Node3D *spatial = Object::cast_to<Node3D>(p_frame)) {
        Node3D *frame = p_parent_key ? spatial->get_parent_node_3d() : spatial;
        if (frame != nullptr) {
            lift.kind = Lift::KIND_SPATIAL;
            lift.spatial = frame->get_global_transform();
        }
        return lift;
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_frame)) {
        Node2D *frame
            = p_parent_key ? Object::cast_to<Node2D>(flat->get_parent()) : flat;
        lift.kind = Lift::KIND_FLAT;
        if (frame != nullptr) {
            lift.flat = frame->get_global_transform();
        }
    }
    return lift;
}

Vector3 lifted(
    Node *p_frame,
    bool p_parent_key,
    bool p_global_key,
    const Vector3 &p_point
) {
    return lift_of(p_frame, p_parent_key, p_global_key).apply(p_point);
}

bool global_point_of(Node *p_node, Vector3 &r_point) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        r_point = spatial->get_global_position();
        return true;
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_node)) {
        const Vector2 at = flat->get_global_position();
        r_point = Vector3(at.x, at.y, 0.0f);
        return true;
    }
    return false;
}

uint64_t world_of(Node *p_owner) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_owner)) {
        const Ref<World3D> world = spatial->get_world_3d();
        return world.is_valid() ? uint64_t(world->get_instance_id()) : 0;
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_owner)) {
        const Ref<World2D> world = flat->get_world_2d();
        return world.is_valid() ? uint64_t(world->get_instance_id()) : 0;
    }
    return 0;
}

Vector3 CameraBook::point(uint64_t p_world, Node *p_owner) {
    for (const WorldCamera &each : cameras) {
        if (each.world == p_world) {
            return each.point;
        }
    }
    WorldCamera found;
    found.world = p_world;
    Viewport *viewport = p_owner->get_viewport();
    if (viewport != nullptr) {
        if (Object::cast_to<Node3D>(p_owner) != nullptr) {
            Camera3D *camera = viewport->get_camera_3d();
            if (camera != nullptr) {
                found.point = camera->get_global_position();
            }
        } else {
            const Vector2 center = viewport->get_canvas_transform()
                                       .affine_inverse()
                                       .xform(
                                           viewport->get_visible_rect().size
                                           * 0.5f
                                       );
            found.point = Vector3(center.x, center.y, 0.0f);
        }
    }
    cameras.push_back(found);
    return found.point;
}

} // namespace netw::diag

#endif

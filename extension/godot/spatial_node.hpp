#pragma once

#if defined(NETW_MODULE)
#include "scene/2d/marker_2d.h"
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/canvas_item.h"

namespace godot {
using ::CanvasItem;
using ::Marker2D;
using ::Node2D;
using ::Node3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/canvas_item.hpp>
#include <godot_cpp/classes/marker2d.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline void set_ignore_transform_notification(
    godot::Node3D *p_node,
    bool p_ignore
) {
    struct Node3DAccess : public godot::Node3D {
        using godot::Node3D::set_ignore_transform_notification;
    };
    static_cast<Node3DAccess *>(p_node)->set_ignore_transform_notification(
        p_ignore
    );
}

} // namespace netw::gd

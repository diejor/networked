#pragma once

#if defined(NETW_MODULE)
#include "scene/2d/physics/collision_shape_2d.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/2d/rectangle_shape_2d.h"
#include "scene/resources/2d/world_boundary_shape_2d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/physics_material.h"

namespace godot {
using ::BoxShape3D;
using ::CollisionShape2D;
using ::CollisionShape3D;
using ::PhysicsMaterial;
using ::RectangleShape2D;
using ::SphereShape3D;
using ::WorldBoundaryShape2D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/box_shape3d.hpp>
#include <godot_cpp/classes/collision_shape2d.hpp>
#include <godot_cpp/classes/collision_shape3d.hpp>
#include <godot_cpp/classes/physics_material.hpp>
#include <godot_cpp/classes/rectangle_shape2d.hpp>
#include <godot_cpp/classes/sphere_shape3d.hpp>
#include <godot_cpp/classes/world_boundary_shape2d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

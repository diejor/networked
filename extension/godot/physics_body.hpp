#pragma once

#if defined(NETW_MODULE)
#include "scene/2d/physics/animatable_body_2d.h"
#include "scene/2d/physics/character_body_2d.h"
#include "scene/2d/physics/rigid_body_2d.h"
#include "scene/2d/physics/static_body_2d.h"
#include "scene/2d/tile_map_layer.h"
#include "scene/3d/physics/static_body_3d.h"

namespace godot {
using ::AnimatableBody2D;
using ::CharacterBody2D;
using ::RigidBody2D;
using ::StaticBody2D;
using ::StaticBody3D;
using ::TileMapLayer;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/animatable_body2d.hpp>
#include <godot_cpp/classes/character_body2d.hpp>
#include <godot_cpp/classes/rigid_body2d.hpp>
#include <godot_cpp/classes/static_body2d.hpp>
#include <godot_cpp/classes/static_body3d.hpp>
#include <godot_cpp/classes/tile_map_layer.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

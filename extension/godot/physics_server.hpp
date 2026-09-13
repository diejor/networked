#pragma once

#if defined(NETW_MODULE)
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace godot {
using ::PhysicsDirectBodyState3D;
using ::PhysicsServer2D;
using ::PhysicsServer3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/physics_direct_body_state3d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

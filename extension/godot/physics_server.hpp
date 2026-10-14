#pragma once

#if defined(NETW_MODULE)
#include "servers/physics_2d/physics_server_2d.h"
#include "servers/physics_3d/physics_server_3d.h"

namespace godot {
using ::PhysicsDirectBodyState2D;
using ::PhysicsDirectBodyState3D;
using ::PhysicsServer2D;
using ::PhysicsServer3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/physics_direct_body_state2d.hpp>
#include <godot_cpp/classes/physics_direct_body_state3d.hpp>
#include <godot_cpp/classes/physics_server2d.hpp>
#include <godot_cpp/classes/physics_server3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#include "godot/rid.hpp"

namespace netw::gd {

bool can_step_space(int p_dimension);
void step_space(const godot::RID &p_space, int p_dimension, double p_delta);
void flush_space_queries(const godot::RID &p_space, int p_dimension);

} // namespace netw::gd

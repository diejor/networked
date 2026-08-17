#pragma once

#if defined(NETW_MODULE)
#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"

namespace godot {
using ::Node2D;
using ::Node3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/node3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

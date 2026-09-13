#pragma once

#if defined(NETW_MODULE)
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/world_2d.h"

namespace godot {
using ::World2D;
using ::World3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/world2d.hpp>
#include <godot_cpp/classes/world3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#if defined(NETW_MODULE)
#include "scene/3d/physics/ray_cast_3d.h"

namespace godot {
using ::RayCast3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/ray_cast3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#if defined(NETW_MODULE)
#include "scene/2d/camera_2d.h"
#include "scene/3d/camera_3d.h"

namespace godot {
using ::Camera2D;
using ::Camera3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/camera2d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

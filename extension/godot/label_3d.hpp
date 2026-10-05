#pragma once

#if defined(NETW_MODULE)
#include "scene/3d/label_3d.h"

namespace godot {
using ::BaseMaterial3D;
using ::Label3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/label3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

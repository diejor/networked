#pragma once

#if defined(NETW_MODULE)
#include "scene/3d/mesh_instance_3d.h"
#include "scene/resources/mesh.h"

namespace godot {
using ::Mesh;
using ::MeshInstance3D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#include "godot/resource.hpp"

#if defined(NETW_MODULE)
#include "scene/resources/texture.h"

namespace godot {
using ::Texture2D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/texture2d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

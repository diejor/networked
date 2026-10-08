#pragma once

#include "godot/texture.hpp"

#if defined(NETW_MODULE)
#include "scene/2d/sprite_2d.h"

namespace godot {
using ::Sprite2D;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/sprite2d.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

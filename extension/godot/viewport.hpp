#pragma once

#if defined(NETW_MODULE)
#include "scene/main/viewport.h"

namespace godot {
using ::SubViewport;
using ::Viewport;
using ::ViewportTexture;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/sub_viewport.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

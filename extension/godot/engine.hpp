#pragma once

#if defined(NETW_MODULE)
#include "core/config/engine.h"

// Engine types are global, so the alias keeps `godot::` spellings compiling.
namespace godot {
using ::Engine;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/engine.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

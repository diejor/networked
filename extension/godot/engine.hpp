#pragma once

#if defined(NETW_MODULE)
#include "core/config/engine.h"

namespace godot {
using ::Engine;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/engine.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

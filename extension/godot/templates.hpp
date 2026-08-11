#pragma once

#if defined(NETW_MODULE)
#include "core/templates/vector.h"

// Engine types are global, so the aliases keep `godot::` spellings compiling.
namespace godot {
using ::Vector;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/templates/vector.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

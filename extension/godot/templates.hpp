#pragma once

#if defined(NETW_MODULE)
#include "core/templates/vector.h"

namespace godot {
using ::Vector;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/templates/vector.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

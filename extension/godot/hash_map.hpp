#pragma once

#if defined(NETW_MODULE)
#include "core/templates/hash_map.h"

namespace godot {
using ::HashMap;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/templates/hash_map.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

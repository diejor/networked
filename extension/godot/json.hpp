#pragma once

#if defined(NETW_MODULE)
#include "core/io/json.h"

namespace godot {
using ::JSON;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/json.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

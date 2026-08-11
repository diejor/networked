#pragma once

#if defined(NETW_MODULE)
#include "core/io/resource.h"

namespace godot {
using ::Resource;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/resource.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

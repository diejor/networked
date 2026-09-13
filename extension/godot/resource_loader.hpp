#pragma once

#if defined(NETW_MODULE)
#include "core/io/resource_loader.h"

namespace godot {
using ::ResourceLoader;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/resource_loader.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

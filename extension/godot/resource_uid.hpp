#pragma once

#if defined(NETW_MODULE)
#include "core/io/resource_uid.h"

namespace godot {
using ::ResourceUID;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/resource_uid.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

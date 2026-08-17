#pragma once

#if defined(NETW_MODULE)
#include "core/os/time.h"

namespace godot {
using ::Time;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/time.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

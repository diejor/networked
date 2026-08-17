#pragma once

#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "core/io/file_access.h"

namespace godot {
using ::FileAccess;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/file_access.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

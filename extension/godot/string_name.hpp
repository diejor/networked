#pragma once

#if defined(NETW_MODULE)
#include "core/string/string_name.h"

namespace godot {
using ::StringName;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/variant/string_name.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#if defined(NETW_MODULE)
#include "core/config/project_settings.h"

namespace godot {
using ::ProjectSettings;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/project_settings.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

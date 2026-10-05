#pragma once

#if defined(NETW_MODULE)
#include "scene/resources/font.h"
#include "scene/theme/theme_db.h"

namespace godot {
using ::Font;
using ::ThemeDB;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/font.hpp>
#include <godot_cpp/classes/theme_db.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

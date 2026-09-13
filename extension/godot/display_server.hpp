#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "servers/display/display_server.h"

namespace godot {
using ::DisplayServer;
} // namespace godot

namespace netw::gd {
constexpr DisplayServerEnums::WindowMode WINDOW_MODE_WINDOWED
    = DisplayServerEnums::WINDOW_MODE_WINDOWED;
constexpr DisplayServerEnums::WindowFlags WINDOW_FLAG_ALWAYS_ON_TOP
    = DisplayServerEnums::WINDOW_FLAG_ALWAYS_ON_TOP;
} // namespace netw::gd
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/display_server.hpp>

namespace netw::gd {
constexpr godot::DisplayServer::WindowMode WINDOW_MODE_WINDOWED
    = godot::DisplayServer::WINDOW_MODE_WINDOWED;
constexpr godot::DisplayServer::WindowFlags WINDOW_FLAG_ALWAYS_ON_TOP
    = godot::DisplayServer::WINDOW_FLAG_ALWAYS_ON_TOP;
} // namespace netw::gd
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

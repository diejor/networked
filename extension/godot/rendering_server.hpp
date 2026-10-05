#pragma once

#if defined(NETW_MODULE)
#include "servers/rendering/rendering_server.h"

namespace netw::gd {
namespace rse = ::RSE;
} // namespace netw::gd
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/rendering_server.hpp>

namespace netw::gd {
using rse = godot::RenderingServer;
} // namespace netw::gd
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

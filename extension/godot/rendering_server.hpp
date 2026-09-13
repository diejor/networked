#pragma once

#if defined(NETW_MODULE)
#include "servers/rendering/rendering_server.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/rendering_server.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

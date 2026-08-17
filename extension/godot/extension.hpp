#pragma once

#if defined(NETW_MODULE)
#elif defined(NETW_GDEXTENSION)
#include <gdextension_interface.h>

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

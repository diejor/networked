#pragma once

#if defined(NETW_MODULE)
#include "core/os/os.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/os.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#if defined(NETW_MODULE)
#include "core/object/class_db.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/class_db.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#if defined(NETW_MODULE)
#include "core/templates/hash_set.h"
#include "core/templates/local_vector.h"

namespace godot {
using ::HashSet;
using ::LocalVector;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

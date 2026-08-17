#pragma once

#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "core/math/random_number_generator.h"

namespace godot {
using ::RandomNumberGenerator;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/random_number_generator.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

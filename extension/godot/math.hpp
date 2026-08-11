#pragma once

#if defined(NETW_MODULE)
#include "core/math/math_funcs.h"

// The engine's math namespace is global, so the alias keeps `godot::` spellings
// compiling. Reach for it only where the engine's exact answer is the contract.
// `<cmath>` is the default everywhere else.
namespace godot {
namespace Math = ::Math;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/math.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

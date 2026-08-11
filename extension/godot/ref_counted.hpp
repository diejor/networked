#pragma once

#if defined(NETW_MODULE)
#include "core/object/ref_counted.h"

// Engine types are global, so the aliases keep `godot::` spellings compiling.
namespace godot {
using ::Ref;
using ::RefCounted;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

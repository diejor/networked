#pragma once

#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
// The generated macros need Object, which ref_counted.hpp brings.
#include "core/object/gdvirtual.gen.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/core/gdvirtual.gen.inc>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#pragma once

#if defined(NETW_MODULE)
#include "core/templates/hash_map.h"
#include "core/templates/rid.h"
#include "core/templates/rid_owner.h"

// Engine types are global, so the aliases keep `godot::` spellings compiling.
namespace godot {
using ::HashMap;
using ::KeyValue;
using ::RID;
using ::RID_Owner;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/rid_owner.hpp>
#include <godot_cpp/variant/rid.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

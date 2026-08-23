#pragma once

#if defined(NETW_MODULE)
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/local_vector.h"
#include "core/templates/vector.h"

namespace godot {
using ::HashMap;
using ::HashSet;
using ::KeyValue;
using ::List;
using ::LocalVector;
using ::Vector;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/templates/hash_map.hpp>
#include <godot_cpp/templates/hash_set.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/templates/local_vector.hpp>
#include <godot_cpp/templates/vector.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

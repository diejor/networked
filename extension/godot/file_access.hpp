#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/io/file_access.h"

namespace godot {
using ::FileAccess;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/file_access.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline bool file_exists(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return godot::FileAccess::exists(p_path);
#else
    return godot::FileAccess::file_exists(p_path);
#endif
}

} // namespace netw::gd

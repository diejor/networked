#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/io/resource.h"
#include "core/io/resource_uid.h"

namespace godot {
using ::Ref;
using ::Resource;
using ::ResourceUID;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_uid.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::String ensure_path(const godot::String &p_reference) {
#if defined(NETW_MODULE)
    return godot::ResourceUID::ensure_path(p_reference);
#else
    return godot::ResourceUID::get_singleton()->ensure_path(p_reference);
#endif
}

} // namespace netw::gd

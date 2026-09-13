#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/core_bind.h"

namespace godot {
using CoreBind::OS;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/os.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::PackedStringArray cmdline_args() {
    godot::OS *os = godot::OS::get_singleton();
    if (os == nullptr) {
        return godot::PackedStringArray();
    }
#if defined(NETW_MODULE)
    godot::PackedStringArray out;
    for (const godot::String &arg : os->get_cmdline_args()) {
        out.push_back(arg);
    }
    for (const godot::String &arg : os->get_cmdline_user_args()) {
        out.push_back(arg);
    }
    return out;
#else
    godot::PackedStringArray out = os->get_cmdline_args();
    out.append_array(os->get_cmdline_user_args());
    return out;
#endif
}

} // namespace netw::gd

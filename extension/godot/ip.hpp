#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/io/ip.h"

namespace godot {
using ::IP;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/ip.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::PackedStringArray local_addresses() {
    godot::IP *ip = godot::IP::get_singleton();
    if (ip == nullptr) {
        return godot::PackedStringArray();
    }
#if defined(NETW_MODULE)
    godot::PackedStringArray out;
    List<IPAddress> found;
    ip->get_local_addresses(&found);
    for (const IPAddress &address : found) {
        out.push_back(godot::String(address));
    }
    return out;
#else
    return ip->get_local_addresses();
#endif
}

} // namespace netw::gd

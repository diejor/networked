#pragma once

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
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

struct LocalAddress {
    godot::String interface_name;
    godot::String address;
};

inline godot::LocalVector<LocalAddress> local_interface_addresses() {
    godot::LocalVector<LocalAddress> out;
    godot::IP *ip = godot::IP::get_singleton();
    if (ip == nullptr) {
        return out;
    }
#if defined(NETW_MODULE)
    HashMap<String, IP::Interface_Info> interfaces;
    ip->get_local_interfaces(&interfaces);
    for (const KeyValue<String, IP::Interface_Info> &entry : interfaces) {
        const String named = entry.value.name + " " + entry.value.name_friendly;
        for (const IPAddress &address : entry.value.ip_addresses) {
            out.push_back({named, String(address)});
        }
    }
#else
    const godot::TypedArray<godot::Dictionary> interfaces
        = ip->get_local_interfaces();
    for (int64_t at = 0; at < interfaces.size(); at++) {
        const godot::Dictionary entry = interfaces[at];
        const godot::String named = godot::String(entry.get("name", "")) + " "
            + godot::String(entry.get("friendly", ""));
        const godot::Array addresses = entry.get("addresses", godot::Array());
        for (int64_t index = 0; index < addresses.size(); index++) {
            out.push_back({named, godot::String(addresses[index])});
        }
    }
#endif
    return out;
}

} // namespace netw::gd

#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "main/performance.h"

namespace godot {
using ::Performance;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/performance.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline int64_t object_count() {
    godot::Performance *performance = godot::Performance::get_singleton();
    if (performance == nullptr) {
        return -1;
    }
    return int64_t(performance->get_monitor(godot::Performance::OBJECT_COUNT));
}

inline int64_t resource_count() {
    godot::Performance *performance = godot::Performance::get_singleton();
    if (performance == nullptr) {
        return -1;
    }
    return int64_t(
        performance->get_monitor(godot::Performance::OBJECT_RESOURCE_COUNT)
    );
}

} // namespace netw::gd

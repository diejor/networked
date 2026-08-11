#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "main/performance.h"

// Engine types are global, so the alias keeps `godot::` spellings compiling.
namespace godot {
using ::Performance;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/performance.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

// The live Object and Resource counts, or -1 where no monitor is up. Both
// tiers spell the monitors the same; only the singleton's availability varies,
// and a headless run without the servers has none.
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

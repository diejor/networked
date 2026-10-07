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

inline bool has_custom_monitor(const godot::StringName &p_id) {
    godot::Performance *performance = godot::Performance::get_singleton();
    return performance != nullptr && performance->has_custom_monitor(p_id);
}

inline void add_custom_monitor(
    const godot::StringName &p_id,
    const godot::Callable &p_read,
    int p_type
) {
    godot::Performance *performance = godot::Performance::get_singleton();
    if (performance == nullptr || performance->has_custom_monitor(p_id)) {
        return;
    }
    const godot::Performance::MonitorType type
        = godot::Performance::MonitorType(p_type);
#if defined(NETW_MODULE)
    performance->add_custom_monitor(
        p_id,
        p_read,
        Vector<godot::Variant>(),
        type
    );
#else
    performance->add_custom_monitor(p_id, p_read, godot::Array(), type);
#endif
}

inline void remove_custom_monitor(const godot::StringName &p_id) {
    godot::Performance *performance = godot::Performance::get_singleton();
    if (performance != nullptr && performance->has_custom_monitor(p_id)) {
        performance->remove_custom_monitor(p_id);
    }
}

} // namespace netw::gd

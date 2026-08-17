#pragma once

#if defined(NETW_MODULE)
#include "core/io/resource_loader.h"
#include "core/object/script_language.h"

namespace godot {
using ::ResourceLoader;
using ::Script;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

#include "godot/variant.hpp"

namespace netw::gd {

inline int script_method_count(
    const godot::Ref<godot::Script> &p_script,
    const godot::StringName &p_name
) {
    if (p_script.is_null()) {
        return 0;
    }
    int total = 0;
#if defined(NETW_MODULE)
    List<MethodInfo> methods;
    p_script->get_script_method_list(&methods);
    for (const MethodInfo &method : methods) {
        total += method.name == p_name ? 1 : 0;
    }
#else
    const godot::TypedArray<godot::Dictionary> methods
        = p_script->get_script_method_list();
    for (int index = 0; index < methods.size(); index++) {
        const godot::Dictionary method = methods[index];
        total += godot::StringName(method.get("name", godot::StringName()))
                == p_name
            ? 1
            : 0;
    }
#endif
    return total;
}

} // namespace netw::gd

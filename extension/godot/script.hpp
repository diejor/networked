#pragma once

#include "godot/object.hpp"

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

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw::gd {

inline godot::Ref<godot::Script> load_script(const godot::String &p_path) {
#if defined(NETW_MODULE)
    return ::ResourceLoader::load(p_path);
#else
    return godot::ResourceLoader::get_singleton()->load(p_path);
#endif
}

inline bool script_is_or_extends(
    const godot::Ref<godot::Script> &p_script,
    const godot::Ref<godot::Script> &p_base
) {
    godot::Ref<godot::Script> walker = p_script;
    while (walker.is_valid()) {
        if (walker == p_base) {
            return true;
        }
        walker = walker->get_base_script();
    }
    return false;
}

inline bool scripts_as(
    godot::Object *p_object,
    const godot::StringName &p_global
) {
    if (p_object == nullptr) {
        return false;
    }
    godot::Ref<godot::Script> walked = p_object->get_script();
    while (walked.is_valid()) {
        if (walked->get_global_name() == p_global) {
            return true;
        }
        walked = walked->get_base_script();
    }
    return false;
}

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

inline godot::Array script_method_list(
    const godot::Ref<godot::Script> &p_script
) {
    if (p_script.is_null()) {
        return godot::Array();
    }
#if defined(NETW_MODULE)
    godot::Array declarations;
    List<MethodInfo> methods;
    p_script->get_script_method_list(&methods);
    for (const MethodInfo &method : methods) {
        declarations.push_back(godot::Dictionary(method));
    }
    return declarations;
#else
    return p_script->get_script_method_list();
#endif
}

inline godot::Array script_signal_list(
    const godot::Ref<godot::Script> &p_script
) {
    if (p_script.is_null()) {
        return godot::Array();
    }
#if defined(NETW_MODULE)
    godot::Array declarations;
    List<MethodInfo> signals;
    p_script->get_script_signal_list(&signals);
    for (const MethodInfo &declared : signals) {
        declarations.push_back(godot::Dictionary(declared));
    }
    return declarations;
#else
    return p_script->get_script_signal_list();
#endif
}

inline godot::Array script_property_list(
    const godot::Ref<godot::Script> &p_script
) {
    if (p_script.is_null()) {
        return godot::Array();
    }
#if defined(NETW_MODULE)
    godot::Array declarations;
    List<PropertyInfo> properties;
    p_script->get_script_property_list(&properties);
    for (const PropertyInfo &declared : properties) {
        declarations.push_back(godot::Dictionary(declared));
    }
    return declarations;
#else
    return p_script->get_script_property_list();
#endif
}

inline void script_rpc_method_names(
    const godot::Ref<godot::Script> &p_script,
    godot::PackedStringArray &r_names
) {
    if (p_script.is_null()) {
        return;
    }
    const godot::Dictionary config = p_script->get_rpc_config();
    const godot::Array keys = config.keys();
    for (int index = 0; index < keys.size(); index++) {
        r_names.push_back(godot::String(keys[index]));
    }
}

inline void script_property_and_signal_names(
    const godot::Ref<godot::Script> &p_script,
    godot::PackedStringArray &r_properties,
    godot::PackedStringArray &r_signals
) {
    godot::Ref<godot::Script> walked = p_script;
    while (walked.is_valid()) {
#if defined(NETW_MODULE)
        List<PropertyInfo> properties;
        walked->get_script_property_list(&properties);
        for (const PropertyInfo &property : properties) {
            if (!(property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
                continue;
            }
            const godot::String name = property.name;
            if (r_properties.find(name) == -1) {
                r_properties.push_back(name);
            }
        }
        List<MethodInfo> signals;
        walked->get_script_signal_list(&signals);
        for (const MethodInfo &declared : signals) {
            const godot::String name = declared.name;
            if (r_signals.find(name) == -1) {
                r_signals.push_back(name);
            }
        }
#else
        const godot::TypedArray<godot::Dictionary> properties
            = walked->get_script_property_list();
        for (int index = 0; index < properties.size(); index++) {
            const godot::Dictionary property = properties[index];
            const int64_t usage = property.get("usage", 0);
            if (!(usage & godot::PROPERTY_USAGE_SCRIPT_VARIABLE)) {
                continue;
            }
            const godot::String name = property.get("name", godot::String());
            if (r_properties.find(name) == -1) {
                r_properties.push_back(name);
            }
        }
        const godot::TypedArray<godot::Dictionary> signals
            = walked->get_script_signal_list();
        for (int index = 0; index < signals.size(); index++) {
            const godot::Dictionary declared = signals[index];
            const godot::String name = declared.get("name", godot::String());
            if (r_signals.find(name) == -1) {
                r_signals.push_back(name);
            }
        }
#endif
        walked = walked->get_base_script();
    }
}

} // namespace netw::gd

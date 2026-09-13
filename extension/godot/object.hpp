#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/object/object.h"
#include "core/object/object_id.h"

namespace godot {
using ::memdelete;
using ::Object;
using ::ObjectID;
using ::PropertyInfo;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/object.hpp>
#include <godot_cpp/core/object_id.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::Object *object_of(godot::ObjectID p_id) {
#if defined(NETW_MODULE)
    return ::ObjectDB::get_instance(p_id);
#else
    return godot::ObjectDB::get_instance(uint64_t(p_id));
#endif
}

inline godot::Variant held(godot::Object *p_object) {
    if (p_object == nullptr) {
        return godot::Variant();
    }
    godot::RefCounted *counted
        = godot::Object::cast_to<godot::RefCounted>(p_object);
    if (counted != nullptr) {
        return godot::Ref<godot::RefCounted>(counted);
    }
    return p_object;
}

inline godot::Object *live_object(const godot::Variant &value) {
    return value.get_validated_object();
}

inline void set_indexed(
    godot::Object *object,
    const godot::NodePath &path,
    const godot::Variant &value
) {
#if defined(NETW_MODULE)
    object->set_indexed(path.get_subnames(), value);
#else
    object->set_indexed(path, value);
#endif
}

inline godot::Variant get_indexed(
    const godot::Object *object,
    const godot::NodePath &path
) {
#if defined(NETW_MODULE)
    return object->get_indexed(path.get_subnames());
#else
    return object->get_indexed(path);
#endif
}

inline godot::Array property_list(const godot::Object *object) {
#if defined(NETW_MODULE)
    List<PropertyInfo> infos;
    object->get_property_list(&infos);
    godot::Array result;
    for (const PropertyInfo &info : infos) {
        result.push_back(godot::Dictionary(info));
    }
    return result;
#else
    return object->get_property_list();
#endif
}

inline godot::Array method_list(godot::Object *object) {
#if defined(NETW_MODULE)
    List<MethodInfo> infos;
    object->get_method_list(&infos);
    godot::Array result;
    for (const MethodInfo &info : infos) {
        result.push_back(godot::Dictionary(info.operator Dictionary()));
    }
    return result;
#else
    return object->get_method_list();
#endif
}

inline godot::Array bound_method_arg_types(
    godot::Object *object,
    const godot::StringName &name
) {
    godot::Array types;
    if (object == nullptr) {
        return types;
    }
    const godot::Array methods = method_list(object);
    for (int at = 0; at < methods.size(); ++at) {
        const godot::Dictionary method = methods[at];
        if (godot::StringName(method["name"]) != name) {
            continue;
        }
        const godot::Array args = method["args"];
        for (int index = 0; index < args.size(); ++index) {
            const godot::Dictionary arg = args[index];
            types.push_back(arg["type"]);
        }
        return types;
    }
    return types;
}

inline bool has_property(
    const godot::Object *object,
    const godot::StringName &name
) {
    if (object == nullptr) {
        return false;
    }
#if defined(NETW_MODULE)
    bool valid = false;
    object->get(name, &valid);
    return valid;
#else
    const godot::Array infos = property_list(object);
    for (int at = 0; at < infos.size(); ++at) {
        const godot::Dictionary info = infos[at];
        if (godot::StringName(info["name"]) == name) {
            return true;
        }
    }
    return false;
#endif
}

inline void add_user_signal(
    godot::Object *object,
    const godot::StringName &name,
    const godot::Array &args
) {
    if (object == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    MethodInfo info;
    info.name = name;
    for (int at = 0; at < args.size(); ++at) {
        const godot::Dictionary arg = args[at];
        info.arguments.push_back(PropertyInfo(
            godot::Variant::Type(int(arg["type"])),
            godot::String(arg["name"])
        ));
    }
    object->add_user_signal(info);
#else
    object->add_user_signal(godot::String(name), args);
#endif
}

} // namespace netw::gd

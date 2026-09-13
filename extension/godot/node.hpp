#pragma once

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "scene/main/node.h"

namespace godot {
using ::Node;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/node.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline bool node_ready(const godot::Node *p_node) {
    if (p_node == nullptr) {
        return false;
    }
#if defined(NETW_MODULE)
    return p_node->is_ready();
#else
    return p_node->is_node_ready();
#endif
}

struct NodeProperty {
    godot::Object *object = nullptr;
    godot::NodePath sub;

    bool is_property() const {
        return object != nullptr && !sub.is_empty();
    }
};

inline NodeProperty node_property(
    godot::Node *p_root,
    const godot::NodePath &p_path
) {
    NodeProperty out;
    if (p_root == nullptr) {
        return out;
    }
#if defined(NETW_MODULE)
    godot::Ref<godot::Resource> resource;
    godot::Vector<godot::StringName> leftover;
    out.object
        = p_root->get_node_and_resource(p_path, resource, leftover, false);
    out.sub
        = godot::NodePath(godot::Vector<godot::StringName>(), leftover, false);
#else
    const godot::Array resolved = p_root->get_node_and_resource(p_path);
    godot::Object *found = resolved[0];
    out.object = found;
    out.sub = resolved[2];
#endif
    return out;
}

inline godot::Variant get_property(
    godot::Object *p_object,
    const godot::NodePath &p_sub
) {
    if (p_object == nullptr) {
        return godot::Variant();
    }
#if defined(NETW_MODULE)
    godot::Vector<godot::StringName> names;
    for (int at = 0; at < p_sub.get_subname_count(); at++) {
        names.push_back(p_sub.get_subname(at));
    }
    return p_object->get_indexed(names);
#else
    return p_object->get_indexed(p_sub);
#endif
}

inline void set_property(
    godot::Object *p_object,
    const godot::NodePath &p_sub,
    const godot::Variant &p_value
) {
    if (p_object == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    godot::Vector<godot::StringName> names;
    for (int at = 0; at < p_sub.get_subname_count(); at++) {
        names.push_back(p_sub.get_subname(at));
    }
    p_object->set_indexed(names, p_value);
#else
    p_object->set_indexed(p_sub, p_value);
#endif
}

inline void add_signal(
    godot::Object *p_object,
    const godot::StringName &p_name,
    int p_arity = 0
) {
    if (p_object == nullptr) {
        return;
    }
#if defined(NETW_MODULE)
    ::MethodInfo signal(p_name);
    for (int at = 0; at < p_arity; at++) {
        signal.arguments.push_back(::PropertyInfo(godot::Variant::INT, "arg"));
    }
    p_object->add_user_signal(signal);
#else
    godot::Array arguments;
    for (int at = 0; at < p_arity; at++) {
        godot::Dictionary argument;
        argument["name"] = "arg";
        argument["type"] = godot::Variant::INT;
        arguments.push_back(argument);
    }
    p_object->add_user_signal(p_name, arguments);
#endif
}

} // namespace netw::gd

#pragma once

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw::script {

struct MethodArity {
    int minimum = 0;
    int maximum = 0;
    bool declared = false;
};

godot::LocalVector<godot::StringName> names_in_text_order(
    godot::PackedStringArray p_names
);

class Cache {
public:
    explicit Cache(const godot::Ref<godot::Script> &p_script);

    godot::Ref<godot::Script> script() const;

    const godot::Dictionary &rpc_config() const {
        return declared_rpc_config;
    }

    const godot::LocalVector<int> &arg_types(const godot::StringName &p_method);

    MethodArity arity(const godot::StringName &p_method);

    int prop_type(godot::Object *p_node, const godot::StringName &p_property);

    const godot::LocalVector<int> &signal_arg_types(
        const godot::StringName &p_signal
    );

    const godot::LocalVector<godot::StringName> &text_ordered_methods();
    const godot::LocalVector<godot::StringName> &text_ordered_properties();
    const godot::LocalVector<godot::StringName> &text_ordered_signals();

private:
    void walk_methods();
    void walk_declared_names();

    godot::ObjectID declaring_script_id;
    godot::Dictionary declared_rpc_config;

    godot::HashMap<godot::StringName, godot::LocalVector<int>> method_arg_types;
    godot::HashMap<godot::StringName, MethodArity> method_arity;
    bool methods_walked = false;

    godot::HashMap<godot::StringName, int> property_types;
    godot::HashMap<godot::StringName, godot::LocalVector<int>> signal_types;

    godot::LocalVector<godot::StringName> ordered_methods;
    bool methods_ordered = false;
    godot::LocalVector<godot::StringName> ordered_properties;
    godot::LocalVector<godot::StringName> ordered_signals;
    bool names_walked = false;
};

} // namespace netw::script

#include "netw/script/cache.hpp"

using namespace godot;

namespace netw::script {

namespace {

LocalVector<int> declared_arg_types(const Dictionary &p_declaration) {
    LocalVector<int> types;
    const Array args = p_declaration.get("args", Array());
    types.reserve(args.size());
    for (int index = 0; index < args.size(); index++) {
        const Dictionary arg = args[index];
        types.push_back(int(arg.get("type", int(Variant::NIL))));
    }
    return types;
}

const LocalVector<int> &no_types() {
    static const LocalVector<int> empty;
    return empty;
}

} // namespace

LocalVector<StringName> names_in_text_order(PackedStringArray p_names) {
    p_names.sort();
    LocalVector<StringName> ordered;
    ordered.reserve(p_names.size());
    for (int index = 0; index < p_names.size(); index++) {
        ordered.push_back(StringName(p_names[index]));
    }
    return ordered;
}

Cache::Cache(const Ref<Script> &p_script)
    : declaring_script_id(gd::instance_id(p_script.ptr())) {
    if (p_script.is_valid()) {
        declared_rpc_config = p_script->get_rpc_config();
    }
}

Ref<Script> Cache::script() const {
    return Ref<Script>(
        Object::cast_to<Script>(gd::object_of(declaring_script_id))
    );
}

void Cache::walk_methods() {
    if (methods_walked) {
        return;
    }
    methods_walked = true;
    Ref<Script> walked = script();
    while (walked.is_valid()) {
        const Array methods = gd::script_method_list(walked);
        for (int index = 0; index < methods.size(); index++) {
            const Dictionary declaration = methods[index];
            const StringName name = declaration.get("name", StringName());
            if (method_arg_types.has(name)) {
                continue;
            }
            method_arg_types.insert(name, declared_arg_types(declaration));
            const Array args = declaration.get("args", Array());
            const Array defaults = declaration.get("default_args", Array());
            MethodArity found;
            found.declared = true;
            found.maximum = int(args.size());
            found.minimum = int(args.size() - defaults.size());
            method_arity.insert(name, found);
        }
        walked = walked->get_base_script();
    }
}

const LocalVector<int> &Cache::arg_types(const StringName &p_method) {
    walk_methods();
    const HashMap<StringName, LocalVector<int>>::ConstIterator found
        = method_arg_types.find(p_method);
    return found ? found->value : no_types();
}

MethodArity Cache::arity(const StringName &p_method) {
    walk_methods();
    const HashMap<StringName, MethodArity>::ConstIterator found
        = method_arity.find(p_method);
    return found ? found->value : MethodArity();
}

int Cache::prop_type(Object *p_node, const StringName &p_property) {
    const HashMap<StringName, int>::ConstIterator cached
        = property_types.find(p_property);
    if (cached) {
        return cached->value;
    }
    int type = int(Variant::NIL);
    if (p_node != nullptr) {
        const Array properties = gd::property_list(p_node);
        for (int index = 0; index < properties.size(); index++) {
            const Dictionary property = properties[index];
            if (StringName(property.get("name", StringName())) != p_property) {
                continue;
            }
            type = int(property.get("type", int(Variant::NIL)));
            break;
        }
    }
    property_types.insert(p_property, type);
    return type;
}

const LocalVector<int> &Cache::signal_arg_types(const StringName &p_signal) {
    const HashMap<StringName, LocalVector<int>>::ConstIterator cached
        = signal_types.find(p_signal);
    if (cached) {
        return cached->value;
    }
    LocalVector<int> types;
    Ref<Script> walked = script();
    while (walked.is_valid()) {
        bool declared = false;
        const Array signals = gd::script_signal_list(walked);
        for (int index = 0; index < signals.size(); index++) {
            const Dictionary declaration = signals[index];
            if (StringName(declaration.get("name", StringName())) != p_signal) {
                continue;
            }
            types = declared_arg_types(declaration);
            declared = true;
            break;
        }
        if (declared) {
            break;
        }
        walked = walked->get_base_script();
    }
    signal_types.insert(p_signal, types);
    return signal_types[p_signal];
}

const LocalVector<StringName> &Cache::text_ordered_methods() {
    if (methods_ordered) {
        return ordered_methods;
    }
    methods_ordered = true;
    PackedStringArray names;
    const Array declared = declared_rpc_config.keys();
    for (int index = 0; index < declared.size(); index++) {
        const String name = String(declared[index]);
        if (names.find(name) == -1) {
            names.push_back(name);
        }
    }
    ordered_methods = names_in_text_order(names);
    return ordered_methods;
}

void Cache::walk_declared_names() {
    if (names_walked) {
        return;
    }
    names_walked = true;
    PackedStringArray properties;
    PackedStringArray signals;
    gd::script_property_and_signal_names(script(), properties, signals);
    ordered_properties = names_in_text_order(properties);
    ordered_signals = names_in_text_order(signals);
}

const LocalVector<StringName> &Cache::text_ordered_properties() {
    walk_declared_names();
    return ordered_properties;
}

const LocalVector<StringName> &Cache::text_ordered_signals() {
    walk_declared_names();
    return ordered_signals;
}

} // namespace netw::script

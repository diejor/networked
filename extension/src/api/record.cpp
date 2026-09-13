#include "netw/api/record.hpp"

#include "godot/utility.hpp"

using namespace godot;

namespace netw {

namespace {

bool is_resource_property(const StringName &p_property) {
    return p_property == StringName("script")
        || p_property == StringName("resource_local_to_scene")
        || p_property == StringName("resource_path")
        || p_property == StringName("resource_name")
        || p_property == StringName("resource_scene_unique_id");
}

} // namespace

void NetwRecord::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_value", "property", "value"),
        &NetwRecord::set_value
    );
    ClassDB::bind_method(
        D_METHOD("get_value", "property", "default"),
        &NetwRecord::get_value,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("has_value", "property"),
        &NetwRecord::has_value
    );
    ClassDB::bind_method(
        D_METHOD("get_property_names"),
        &NetwRecord::get_property_names
    );
    ClassDB::bind_method(D_METHOD("is_empty"), &NetwRecord::is_empty);
    ClassDB::bind_method(D_METHOD("to_dict"), &NetwRecord::to_dict);
    ClassDB::bind_method(D_METHOD("from_dict", "data"), &NetwRecord::from_dict);
    ClassDB::bind_method(
        D_METHOD("_iter_init", "cursor"),
        &NetwRecord::iterate_init
    );
    ClassDB::bind_method(
        D_METHOD("_iter_next", "cursor"),
        &NetwRecord::iterate_next
    );
    ClassDB::bind_method(
        D_METHOD("_iter_get", "cursor"),
        &NetwRecord::iterate_get
    );

    GDVIRTUAL_BIND(_set_value, "property", "value");
    GDVIRTUAL_BIND(_get_value, "property", "default");
    GDVIRTUAL_BIND(_has_value, "property");
    GDVIRTUAL_BIND(_get_property_names);
    GDVIRTUAL_BIND(_to_dict);
    GDVIRTUAL_BIND(_from_dict, "data");
}

bool NetwRecord::reserves_property(const StringName &p_property) const {
    return is_resource_property(p_property);
}

void NetwRecord::set_value(
    const StringName &p_property,
    const Variant &p_value
) {
    GDVIRTUAL_CALL(_set_value, p_property, p_value);
}

Variant NetwRecord::get_value(
    const StringName &p_property,
    const Variant &p_fallback
) const {
    Variant found;
    if (GDVIRTUAL_CALL(_get_value, p_property, p_fallback, found)) {
        return found;
    }
    return p_fallback;
}

bool NetwRecord::has_value(const StringName &p_property) const {
    bool found = false;
    if (GDVIRTUAL_CALL(_has_value, p_property, found)) {
        return found;
    }
    return false;
}

TypedArray<StringName> NetwRecord::get_property_names() const {
    TypedArray<StringName> names;
    if (GDVIRTUAL_CALL(_get_property_names, names)) {
        return names;
    }
    return TypedArray<StringName>();
}

bool NetwRecord::is_empty() const {
    return get_property_names().is_empty();
}

Dictionary NetwRecord::to_dict() const {
    Dictionary made;
    if (GDVIRTUAL_CALL(_to_dict, made)) {
        return made;
    }
    const TypedArray<StringName> names = get_property_names();
    for (int at = 0; at < names.size(); at++) {
        const StringName key = names[at];
        made[key] = get_value(key);
    }
    return made;
}

void NetwRecord::from_dict(const Dictionary &p_data) {
    if (GDVIRTUAL_CALL(_from_dict, p_data)) {
        return;
    }
    const Array keys = p_data.keys();
    for (int at = 0; at < keys.size(); at++) {
        const StringName key = keys[at];
        set_value(key, p_data[keys[at]]);
    }
}

bool NetwRecord::iterate_init(const Array &p_cursor) {
    (void)p_cursor;
    iter_keys = get_property_names();
    iter_index = 0;
    return iter_keys.size() > 0;
}

bool NetwRecord::iterate_next(const Array &p_cursor) {
    (void)p_cursor;
    iter_index += 1;
    return iter_index < iter_keys.size();
}

Variant NetwRecord::iterate_get(const Variant &p_cursor) const {
    (void)p_cursor;
    if (iter_index < 0 || iter_index >= iter_keys.size()) {
        return Variant();
    }
    return iter_keys[iter_index];
}

bool NetwRecord::_get(const StringName &p_property, Variant &r_ret) const {
    if (reserves_property(p_property) || !has_value(p_property)) {
        return false;
    }
    r_ret = get_value(p_property);
    return true;
}

bool NetwRecord::_set(const StringName &p_property, const Variant &p_value) {
    if (reserves_property(p_property)) {
        return false;
    }
    set_value(p_property, p_value);
    return true;
}

void NetwRecord::_get_property_list(List<PropertyInfo> *p_list) const {
    const TypedArray<StringName> names = get_property_names();
    for (int at = 0; at < names.size(); at++) {
        const StringName key = names[at];
        p_list->push_back(PropertyInfo(get_value(key).get_type(), String(key)));
    }
}

void DictionaryRecord::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_data", "data"),
        &DictionaryRecord::set_data
    );
    ClassDB::bind_method(D_METHOD("get_data"), &DictionaryRecord::get_data);
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "data"),
        "set_data",
        "get_data"
    );
}

DictionaryRecord::DictionaryRecord() {
    set_local_to_scene(true);
}

bool DictionaryRecord::reserves_property(const StringName &p_property) const {
    return p_property == StringName("data")
        || NetwRecord::reserves_property(p_property);
}

void DictionaryRecord::set_data(const Dictionary &p_values) {
    data = p_values;
}

Dictionary DictionaryRecord::get_data() const {
    return data;
}

PackedByteArray DictionaryRecord::serialize() {
    return gd::var_to_bytes(data);
}

void DictionaryRecord::deserialize(const PackedByteArray &p_bytes) {
    data = gd::bytes_to_var(p_bytes);
}

void DictionaryRecord::set_value(
    const StringName &p_property,
    const Variant &p_value
) {
    data[p_property] = p_value;
}

Variant DictionaryRecord::get_value(
    const StringName &p_property,
    const Variant &p_fallback
) const {
    return data.get(p_property, p_fallback);
}

bool DictionaryRecord::has_value(const StringName &p_property) const {
    return data.has(p_property);
}

TypedArray<StringName> DictionaryRecord::get_property_names() const {
    TypedArray<StringName> names;
    const Array keys = data.keys();
    names.resize(keys.size());
    for (int at = 0; at < keys.size(); at++) {
        names[at] = StringName(keys[at]);
    }
    return names;
}

} // namespace netw

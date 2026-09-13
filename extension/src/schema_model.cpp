#include "netw/schema_model.hpp"

#include "godot/templates.hpp"

using namespace godot;

namespace netw::schema_model {

namespace {

HashMap<StringName, Ref<NetwSchema>> &registry() {
    static HashMap<StringName, Ref<NetwSchema>> book;
    return book;
}

} // namespace

Ref<NetwSchema> declare(const StringName &p_name) {
    if (String(p_name).is_empty()) {
        return Ref<NetwSchema>();
    }
    HashMap<StringName, Ref<NetwSchema>>::Iterator found
        = registry().find(p_name);
    if (found) {
        return found->value;
    }
    const Ref<NetwSchema> fresh = NetwSchema::create(p_name);
    registry().insert(p_name, fresh);
    return fresh;
}

Ref<NetwSchema> find(const StringName &p_name) {
    HashMap<StringName, Ref<NetwSchema>>::Iterator found
        = registry().find(p_name);
    return found ? found->value : Ref<NetwSchema>();
}

void adopt(const Ref<NetwSchema> &p_declaration) {
    if (p_declaration.is_null()
        || String(p_declaration->get_schema_name()).is_empty()) {
        return;
    }
    if (!registry().has(p_declaration->get_schema_name())) {
        registry().insert(p_declaration->get_schema_name(), p_declaration);
    }
}

TypedArray<NetwSchema> declarations() {
    PackedStringArray names;
    for (const KeyValue<StringName, Ref<NetwSchema>> &row : registry()) {
        names.push_back(String(row.key));
    }
    names.sort();
    TypedArray<NetwSchema> out;
    for (int at = 0; at < names.size(); ++at) {
        out.push_back(registry().get(StringName(names[at])));
    }
    return out;
}

void clear() {
    registry().clear();
}

} // namespace netw::schema_model

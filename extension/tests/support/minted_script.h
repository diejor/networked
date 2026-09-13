#pragma once

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"

namespace netw_test {

inline godot::Ref<godot::Script> minted_script(const char *p_source) {
    godot::Ref<godot::Script> script
        = godot::Ref<godot::Script>(godot::ClassDB::instantiate("GDScript"));
    if (script.is_null()) {
        return script;
    }
    script->set_source_code(godot::String(p_source));
    if (script->reload() != godot::OK) {
        return godot::Ref<godot::Script>();
    }
    return script;
}

inline godot::Ref<godot::Script> script_from(const char *p_source_or_path) {
    const godot::String text(p_source_or_path);
    if (text.begins_with("res://")) {
        return netw::gd::load_script(text);
    }
    return minted_script(p_source_or_path);
}

inline godot::Node *minted_node(const char *p_source) {
    const godot::Ref<godot::Script> script = script_from(p_source);
    if (script.is_null()) {
        return nullptr;
    }
    return godot::Object::cast_to<godot::Node>(script->call("new"));
}

} // namespace netw_test

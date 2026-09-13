#pragma once

#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw::gd {

inline godot::String document_origin() {
    godot::Object *bridge
        = named_singleton(godot::StringName("JavaScriptBridge"));
    if (bridge == nullptr) {
        return godot::String();
    }
    const godot::Variant answered = bridge->call(
        godot::StringName("eval"),
        godot::String("location.origin"),
        true
    );
    if (answered.get_type() != godot::Variant::STRING) {
        return godot::String();
    }
    return godot::String(answered).strip_edges();
}

} // namespace netw::gd

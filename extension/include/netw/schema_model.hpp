#pragma once

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_model.hpp"

namespace netw::schema_model {

godot::Ref<NetwSchema> declare(const godot::StringName &p_name);
godot::Ref<NetwSchema> find(const godot::StringName &p_name);
void adopt(const godot::Ref<NetwSchema> &p_declaration);
godot::TypedArray<NetwSchema> declarations();
void clear();

} // namespace netw::schema_model

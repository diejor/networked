#pragma once

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {
namespace authoring {

bool declares_prediction(godot::Node *p_root);

void apply(godot::Node *p_root, godot::Object *p_sync);
void apply_spawner(godot::Node *p_node, godot::Object *p_spawner);

} // namespace authoring
} // namespace netw

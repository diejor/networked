#pragma once

#include "godot/node.hpp"

namespace netw::view {

godot::Node *first_camera(godot::Node *p_root, const godot::String &p_type);

bool adopt_camera(godot::Node *p_player, godot::Node *p_level);

} // namespace netw::view

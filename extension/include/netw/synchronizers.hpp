#pragma once

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw::synchronizers {

godot::StringName meta_key();

godot::TypedArray<godot::MultiplayerSynchronizer> of_node(
    godot::Object *p_target
);
godot::TypedArray<godot::MultiplayerSynchronizer> owned_by(
    godot::Object *p_target
);
godot::Dictionary synchronized_properties(godot::Object *p_target);
godot::Array governed_targets(godot::Object *p_sync, godot::Object *p_root);
godot::Array display_bindings(godot::Object *p_sync, godot::Object *p_root);

godot::Variant resolve_value(
    godot::Object *p_target,
    const godot::NodePath &p_path
);
void assign_value(
    godot::Object *p_target,
    const godot::NodePath &p_path,
    const godot::Variant &p_value
);

bool visibility_verdict(
    godot::Object *p_root,
    int64_t p_peer,
    int64_t p_local_id
);

godot::Array spawn_state(godot::Object *p_root, int64_t p_local_id);

void sync_only_server(godot::Object *p_target);
void clear_cache(godot::Object *p_target);

} // namespace netw::synchronizers

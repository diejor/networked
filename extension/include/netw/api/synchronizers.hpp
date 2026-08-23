#pragma once

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwSynchronizers : public godot::Object {
    GDCLASS(NetwSynchronizers, godot::Object)

protected:
    static void _bind_methods();

public:
    static godot::StringName meta_key();

    static godot::TypedArray<godot::MultiplayerSynchronizer> of_node(
        godot::Object *p_target
    );
    static godot::TypedArray<godot::MultiplayerSynchronizer> owned_by(
        godot::Object *p_target
    );
    static godot::Dictionary synchronized_properties(godot::Object *p_target);
    static godot::Array governed_targets(
        godot::Object *p_sync,
        godot::Object *p_root
    );
    static godot::Array display_bindings(
        godot::Object *p_sync,
        godot::Object *p_root
    );

    static godot::Variant resolve_value(
        godot::Object *p_target,
        const godot::NodePath &p_path
    );
    static void assign_value(
        godot::Object *p_target,
        const godot::NodePath &p_path,
        const godot::Variant &p_value
    );

    static void sync_only_server(godot::Object *p_target);
    static void clear_cache(godot::Object *p_target);
};

} // namespace netw

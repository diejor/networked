#pragma once

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "netw/api/scene_handle.hpp"

namespace netw {

class NetwMultiplayer;

class SpawnSlot : public godot::RefCounted {
    GDCLASS(SpawnSlot, godot::RefCounted)

    godot::Ref<NetwSceneHandle> scene;
    godot::ObjectID parent_id;

    godot::Node *parent() const;

protected:
    static void _bind_methods();

public:
    static godot::Ref<SpawnSlot> in_scene(
        const godot::Ref<NetwSceneHandle> &p_scene
    );
    static godot::Ref<SpawnSlot> under(godot::Node *p_parent);
    static godot::Ref<SpawnSlot> for_scene(
        NetwMultiplayer *p_session,
        const godot::StringName &p_scene_stem
    );

    bool has_scene() const;
    bool is_valid() const;
    godot::Ref<NetwSceneHandle> get_scene() const;
    void place_player(godot::Node *p_player);
};

} // namespace netw

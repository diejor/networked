#pragma once

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwMultiplayer;

class NetwDefaultJoin : public godot::RefCounted {
    GDCLASS(NetwDefaultJoin, godot::RefCounted)

    godot::ObjectID session_id;

    NetwMultiplayer *session() const;
    void settle_entry(
        godot::Node *p_player,
        godot::Node *p_container,
        const godot::Ref<NetwPromise> &p_answer
    );

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    godot::Ref<NetwPromise> spawn(
        const godot::Ref<NetwParticipant> &p_participant,
        const godot::StringName &p_scene_stem,
        const godot::NodePath &p_spawner_path
    );
};

} // namespace netw

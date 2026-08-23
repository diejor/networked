#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/resolved_join.hpp"

namespace netw {

class NetwMultiplayerCore;

class NetwParticipant : public godot::RefCounted {
    GDCLASS(NetwParticipant, godot::RefCounted)

    godot::ObjectID core_id;
    int64_t peer_id = 0;

    NetwMultiplayerCore *core() const;
    static godot::RID scene_entity_of(const godot::Variant &p_scene);

protected:
    static void _bind_methods();

public:
    void seat_at(NetwMultiplayerCore *p_core, int64_t p_peer);

    int64_t get_peer_id() const;
    godot::Ref<ResolvedJoin> get_join() const;
    godot::Ref<godot::RefCounted> get_identity() const;
    godot::StringName get_username() const;
    godot::Array get_arg_values() const;
    bool get_is_debug() const;

    godot::Variant get_current_scene() const;
    void set_current_scene(const godot::Variant &p_scene);
    void move_to(const godot::Variant &p_dest);
};

} // namespace netw

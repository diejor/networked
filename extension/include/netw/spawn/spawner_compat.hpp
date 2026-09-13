#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/spawn/spawner_roster.hpp"

namespace netw {
class NetwMultiplayer;
}

namespace netw::spawn {

class SpawnerCompat {
private:
    godot::ObjectID core_id;
    SpawnerRoster roster;
    godot::HashMap<int64_t, godot::Variant> custom_args;
    godot::HashMap<int64_t, godot::Callable> originals;
    godot::Callable booked_reader;
    godot::Callable arm_consumed;
    int64_t drops_uncaptured_custom = 0;

    NetwMultiplayer *core() const;
    godot::Node *session_root() const;
    void wrap_when_ready(
        godot::MultiplayerSpawner *p_spawner,
        NetwMultiplayer *p_plane
    );

public:
    void set_core(godot::Object *p_core);
    void set_spawn_seams(
        const godot::Callable &p_booked_reader,
        const godot::Callable &p_arm_consumed
    );
    SpawnerRoster &get_roster() {
        return roster;
    }

    godot::Error consume(godot::Node *p_node, godot::Object *p_spawner);
    godot::Error consume_remove(godot::Node *p_node, godot::Object *p_spawner);

    void register_spawner(godot::Object *p_spawner);
    void wrap_spawner(godot::Object *p_spawner);
    godot::Node *wrapped_spawn(
        const godot::Variant &p_data,
        const godot::Callable &p_original
    );

    godot::Node *instantiate(
        godot::Object *p_spawner,
        int p_scene_index,
        const godot::Variant &p_data
    );

    void note_recv(int64_t p_route, godot::Object *p_spawner);
    void emit_spawned(godot::Object *p_spawner, godot::Node *p_node);
    void emit_despawned(int64_t p_route, godot::Node *p_node);

    void on_session_entered();
    void on_session_ended();
    void on_node_added(godot::Node *p_node);
    void clear_session_state();

    godot::Dictionary counters() const;
};

} // namespace netw::spawn

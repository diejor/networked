#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/scene_handle.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;

class NetwPlayer : public godot::RefCounted {
    GDCLASS(NetwPlayer, godot::RefCounted)

    godot::ObjectID core_id;
    int64_t peer_id = 0;
    int64_t incarnation = 0;
    godot::StringName username;

    NetwMultiplayer *core() const;

protected:
    static void _bind_methods();

public:
    void bind_to(
        NetwMultiplayer *p_core,
        int64_t p_peer,
        int64_t p_incarnation = 0,
        const godot::StringName &p_username = godot::StringName()
    );
    void rebind_peer(int64_t p_peer);

    int64_t get_peer_id() const;
    int64_t player_id() const;
    bool get_is_active() const;
    godot::StringName get_username() const;
    godot::TypedArray<NetwEntity> get_bodies() const;
};

} // namespace netw

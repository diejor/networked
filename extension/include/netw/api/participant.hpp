#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/scene_handle.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;

class NetwParticipant : public godot::RefCounted {
    GDCLASS(NetwParticipant, godot::RefCounted)

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
    int64_t membership() const;
    bool get_is_active() const;
    godot::Ref<NetwIdentity> get_identity() const;
    godot::StringName get_username() const;
    godot::TypedArray<NetwEntity> get_players() const;
};

} // namespace netw

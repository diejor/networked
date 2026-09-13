#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/api/scene_handle.hpp"

namespace netw {

class NetwMultiplayer;

class NetwParticipant : public godot::RefCounted {
    GDCLASS(NetwParticipant, godot::RefCounted)

    godot::ObjectID core_id;
    int64_t peer_id = 0;

    NetwMultiplayer *core() const;

protected:
    static void _bind_methods();

public:
    void seat_at(NetwMultiplayer *p_core, int64_t p_peer);

    int64_t get_peer_id() const;
    godot::Ref<ResolvedJoin> get_join() const;
    godot::Ref<NetwIdentity> get_identity() const;
    godot::StringName get_username() const;
    godot::Array get_arg_values() const;

    godot::Ref<NetwSceneHandle> get_current_scene() const;
    godot::Ref<NetwPromise> move_to(
        const godot::Ref<NetwSceneHandle> &p_destination
    );
};

} // namespace netw

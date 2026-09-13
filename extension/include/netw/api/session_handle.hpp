#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwAuthFlow;
class NetwEntity;
class NetwMultiplayer;
class NetwParticipant;
class NetwSceneHandle;
class NetwServerInfo;
class NetwSessionConfig;

class NetwSessionHandle : public godot::RefCounted {
    GDCLASS(NetwSessionHandle, godot::RefCounted)

    godot::ObjectID session_id;

    NetwMultiplayer *session() const;

    void relay_entered();
    void relay_ended();
    void relay_disconnected();
    void relay_disconnecting(const godot::String &p_reason);
    void relay_participant_joined(const godot::Ref<NetwParticipant> &p_who);
    void relay_local_joined(const godot::Ref<NetwParticipant> &p_who);
    void relay_scene_live(const godot::Ref<NetwSceneHandle> &p_scene);
    void relay_local_scene_changed(
        const godot::Ref<NetwSceneHandle> &p_from,
        const godot::Ref<NetwSceneHandle> &p_to
    );

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    godot::TypedArray<NetwParticipant> get_participants() const;
    godot::Ref<NetwParticipant> get_local_participant() const;
    godot::Ref<NetwEntity> get_local_player() const;
    godot::Ref<NetwParticipant> participant_of(int64_t p_peer) const;
    godot::Variant bucket_of(
        int64_t p_peer,
        const godot::Variant &p_type
    ) const;

    int64_t get_role() const;
    bool get_is_online() const;
    bool get_is_local_client() const;
    godot::Node *get_root() const;
    godot::TypedArray<NetwSceneHandle> get_scenes() const;

    godot::Ref<NetwSessionConfig> get_config() const;
    void set_server_info(const godot::Ref<NetwServerInfo> &p_info);
    godot::Ref<NetwAuthFlow> get_auth_flow() const;
    godot::Dictionary get_stats() const;

    godot::Ref<NetwPromise> leave();
    godot::Ref<NetwSceneHandle> activate_scene(
        const godot::Variant &p_destination
    );
    godot::Ref<NetwPromise> request_scene(
        const godot::String &p_path,
        int64_t p_scope
    );
};

} // namespace netw

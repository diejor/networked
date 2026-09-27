#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;
class NetwPlayer;
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
    void relay_player_joined(const godot::Ref<NetwPlayer> &p_who);
    void relay_local_joined(const godot::Ref<NetwPlayer> &p_who);
    void relay_player_left(const godot::Ref<NetwPlayer> &p_who);
    void relay_join_failed(int64_t p_code, const godot::String &p_reason);
    void relay_scene_live(const godot::Ref<NetwSceneHandle> &p_scene);
    void relay_scene_changed(
        const godot::Ref<NetwSceneHandle> &p_scene,
        const godot::TypedArray<NetwPlayer> &p_arrived
    );
    void relay_presentation_changed(
        const godot::Ref<NetwSceneHandle> &p_from,
        const godot::Ref<NetwSceneHandle> &p_to
    );

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    godot::TypedArray<NetwPlayer> get_players() const;
    godot::Ref<NetwPlayer> get_local_player() const;
    godot::Ref<NetwSceneHandle> get_presented_scene() const;
    godot::Ref<NetwPlayer> player_of(int64_t p_peer) const;
    godot::Variant bucket_of(
        int64_t p_peer,
        const godot::Variant &p_type
    ) const;

    enums::NetwMultiplayer::Role get_role() const;
    bool get_is_online() const;
    bool get_is_local_client() const;
    godot::Node *get_root() const;
    godot::TypedArray<NetwSceneHandle> get_scenes() const;

    godot::Ref<NetwSessionConfig> get_config() const;
    void set_server_info(const godot::Ref<NetwServerInfo> &p_info);
    godot::Dictionary get_stats() const;

    godot::Ref<NetwPromise> leave();
    godot::Ref<NetwPromise> save_entities();
    godot::Error kick(
        const godot::Ref<NetwPlayer> &p_who,
        const godot::String &p_reason
    );
    godot::Ref<NetwPromise> request_scene(
        const godot::String &p_path,
        enums::NetwMultiplayer::SceneChange p_scope
    );
};

} // namespace netw

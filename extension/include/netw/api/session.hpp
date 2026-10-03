#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "godot/world.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwClock;
class NetwConnection;
class NetwDatabase;
class NetwEntity;
class NetwInterestLayer;
class NetwMultiplayer;
class NetwTable;
class NetwPhysicsStepper;
class NetwPlayer;
class NetwScene;
class NetwServerInfo;
class NetwSessionConfig;

class NetwSession : public godot::RefCounted, public enums::NetwSession {
    GDCLASS(NetwSession, godot::RefCounted)

    godot::ObjectID session_id;

    NetwMultiplayer *session() const;

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);
    void announce_entered();
    void announce_exiting(ExitReason p_reason, const godot::String &p_detail);
    void announce_exited(ExitReason p_reason, const godot::String &p_detail);
    void announce_player_joined(const godot::Ref<NetwPlayer> &p_who);
    void announce_local_joined(const godot::Ref<NetwPlayer> &p_who);
    void announce_player_left(const godot::Ref<NetwPlayer> &p_who);
    void announce_join_failed(int64_t p_code, const godot::String &p_reason);
    void announce_scene_ready(const godot::Ref<NetwScene> &p_scene);

    godot::TypedArray<NetwPlayer> get_players() const;
    godot::Ref<NetwPlayer> get_local_player() const;
    godot::Ref<NetwPlayer> player_of(int64_t p_peer) const;
    godot::Variant bucket_of(
        int64_t p_peer,
        const godot::Variant &p_type
    ) const;

    enums::NetwSession::Role get_role() const;
    bool get_is_online() const;
    bool get_is_local_client() const;
    godot::Node *get_root() const;
    godot::TypedArray<NetwScene> get_scenes() const;

    godot::Ref<NetwSessionConfig> get_config() const;
    void set_server_info(const godot::Ref<NetwServerInfo> &p_info);
    godot::Dictionary get_stats() const;
    void set_peer_bandwidth(int64_t p_peer, int64_t p_kbps);

    godot::Ref<NetwPromise> leave();
    godot::Ref<NetwPromise> save_entities();
    godot::Error kick(
        const godot::Ref<NetwPlayer> &p_who,
        const godot::String &p_reason
    );
    godot::Ref<NetwPromise> request_scene(
        const godot::String &p_path,
        enums::NetwScene::SceneChange p_scope
    );
    godot::Ref<NetwScene> create_scene(
        const godot::Ref<godot::PackedScene> &p_scene,
        enums::NetwScene::SceneIsolation p_isolation
    );
    godot::Ref<NetwScene> find_scene(const godot::StringName &p_label);
    godot::Error install_physics_stepper(
        const godot::Ref<godot::World3D> &p_world,
        const godot::Ref<NetwPhysicsStepper> &p_stepper
    );
    godot::Error install_physics_stepper_2d(
        const godot::Ref<godot::World2D> &p_world,
        const godot::Ref<NetwPhysicsStepper> &p_stepper
    );

    godot::Ref<NetwClock> get_clock() const;
    godot::Ref<NetwConnection> get_connection() const;
    godot::Ref<NetwDatabase> database(const godot::StringName &p_name) const;
    godot::Ref<NetwTable> table(const godot::StringName &p_name) const;
    godot::Ref<NetwInterestLayer> interest_layer(
        const godot::StringName &p_name
    ) const;
    godot::Node *service(godot::Object *p_type) const;
    void register_service(godot::Object *p_service, godot::Object *p_type);
    void unregister_service(godot::Object *p_service, godot::Object *p_type);
};

} // namespace netw

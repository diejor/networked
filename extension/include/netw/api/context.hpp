#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/action.hpp"
#include "netw/api/channel.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/clock_handle.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/despawn_config.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/record.hpp"
#include "netw/api/scene_config.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/scene_core.hpp"
#include "netw/scene_decl.hpp"

namespace netw {

class Netw : public godot::RefCounted {
    GDCLASS(Netw, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    enum SceneChange {
        SCENE_CHANGE_SESSION = NetwSceneCore::SCOPE_SESSION,
        SCENE_CHANGE_PARTICIPANT = NetwSceneCore::SCOPE_PARTICIPANT,
        SCENE_CHANGE_SCENE = NetwSceneCore::SCOPE_SCENE,
    };

    enum SceneIsolation {
        SCENE_ISOLATION_NONE = NetwSceneCore::ISOLATION_NONE,
        SCENE_ISOLATION_OWN_WORLD = NetwSceneCore::ISOLATION_OWN_WORLD,
    };

    static godot::Ref<NetwMultiplayer> of(godot::Node *p_node);
    static godot::Ref<NetwConnectHandle> connection(godot::Node *p_node);
    static godot::Ref<NetwSessionHandle> session(godot::Node *p_node);
    static godot::Ref<NetwClockHandle> clock(godot::Node *p_node);
    static godot::Variant service(godot::Node *p_node, godot::Object *p_type);
    static void service_register(
        godot::Node *p_node,
        godot::Object *p_service,
        godot::Object *p_type
    );
    static void service_unregister(
        godot::Node *p_node,
        godot::Object *p_service,
        godot::Object *p_type
    );
    static godot::Ref<NetwPromise> join(
        godot::Node *p_node,
        const godot::StringName &p_username,
        const godot::Array &p_args = godot::Array()
    );
    static godot::Ref<NetwSceneHandle> scene(
        godot::Node *p_node,
        const godot::StringName &p_named
    );

    static godot::Error configure_server_info(
        godot::Node *p_node,
        const godot::Callable &p_provider
    );
    static godot::Ref<NetwJoinConfig> configure_join(
        godot::Node *p_node,
        const godot::Callable &p_handler
    );
    static godot::Error configure_auth(
        godot::Node *p_node,
        const godot::Callable &p_factory
    );
    static godot::Ref<NetwSessionConfig> configure_session(
        godot::Node *p_node,
        const godot::Ref<NetwSessionConfig> &p_preset
        = godot::Ref<NetwSessionConfig>()
    );
    static godot::Ref<NetwClockConfig> configure_clock(
        godot::Node *p_node,
        const godot::Ref<NetwClockConfig> &p_preset
        = godot::Ref<NetwClockConfig>()
    );
    static godot::Ref<NetwLagCompensationConfig> configure_lagcomp(
        godot::Node *p_node,
        const godot::Ref<NetwLagCompensationConfig> &p_preset
        = godot::Ref<NetwLagCompensationConfig>()
    );

    static godot::Ref<NetwPropertyConfig> configure_property(
        godot::Node *p_node,
        const godot::StringName &p_property,
        bool p_warn_late
    );
    static godot::Ref<NetwMemberConfig> configure_rpc(
        const godot::Callable &p_callable
    );
    static godot::Ref<NetwMemberConfig> configure_signal(
        const godot::Signal &p_signal
    );
    static godot::Ref<NetwMemberConfig> configure_spawn(
        const godot::Callable &p_callable
    );
    static godot::Ref<NetwEntity> configure_entity(godot::Node *p_node);
    static godot::Ref<NetwDespawnConfig> configure_despawn(godot::Node *p_node);
    static godot::Ref<NetwPersistenceConfig> configure_persistence(
        godot::Node *p_node
    );
    static godot::Ref<NetwInterestHandle> configure_interest(
        godot::Node *p_node
    );
    static godot::Ref<NetwSchema> configure_schema(
        const godot::StringName &p_name
    );

    static void rpc(
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static void rpc_id(
        int64_t p_peer_id,
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static void rpc_controller(
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static godot::Ref<NetwPromise> request(
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static godot::Ref<NetwPromise> request_id(
        int64_t p_peer_id,
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static godot::Ref<NetwGroupPromise> request_all(
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static godot::Ref<NetwPromise> request_controller(
        const godot::Callable &p_callable,
        const godot::Array &p_args
    );
    static void sync_property(
        godot::Node *p_node,
        const godot::StringName &p_property
    );
    static void emit_entity_signal(
        const godot::Signal &p_signal,
        const godot::Array &p_args
    );
    static godot::Ref<NetwChannel> channel(
        godot::Node *p_node,
        int64_t p_channel_id
    );

    static godot::Ref<NetwEntity> replicate(
        godot::Node *p_node,
        const godot::Ref<NetwParticipant> &p_owner
    );
    static godot::Node *spawn(
        const godot::Callable &p_fn,
        const godot::Array &p_args
    );
    static godot::Node *spawn_player(
        const godot::Ref<NetwParticipant> &p_player,
        const godot::Callable &p_fn,
        const godot::Array &p_args
    );
    static godot::Error despawn(
        godot::Node *p_node,
        const godot::Ref<NetwDespawnOpts> &p_opts
    );

    static godot::Ref<NetwAction> action(const godot::Callable &p_authority);
    static godot::Ref<DictionaryRecord> sample(
        const godot::Ref<NetwEntity> &p_entity,
        int64_t p_tick
    );
    static void rewind(
        const godot::TypedArray<godot::RID> &p_entities,
        int64_t p_tick,
        const godot::Callable &p_body
    );

    static godot::Ref<NetwPromise> change_scene_to_file(
        godot::Node *p_node,
        const godot::String &p_path,
        SceneChange p_scope
    );
    static godot::Ref<NetwPromise> change_scene_to_packed(
        godot::Node *p_node,
        const godot::Ref<godot::PackedScene> &p_packed,
        SceneChange p_scope
    );
    static godot::Ref<NetwPromise> reload_current_scene(
        godot::Node *p_node,
        SceneChange p_scope
    );
    static godot::Ref<NetwSceneConfig> configure_multiplayer_scene(
        godot::Node *p_node
    );
    static godot::Error configure_scene_requests(
        godot::Node *p_node,
        const godot::Callable &p_handler
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::Netw::SceneChange);
VARIANT_ENUM_CAST(netw::Netw::SceneIsolation);

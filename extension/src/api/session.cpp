#include "netw/api/session.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/clock.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/database.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/scene.hpp"
#include "netw/api/server_info.hpp"
#include "netw/api/session_config.hpp"
#include "netw/api/table.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_STATE_CHANGED = "state_changed";
const char *SIG_ENTERED = "entered";
const char *SIG_EXITING = "exiting";
const char *SIG_EXITED = "exited";
const char *SIG_PLAYER_JOINED = "player_joined";
const char *SIG_LOCAL_JOINED = "local_joined";
const char *SIG_SCENE_READY = "scene_ready";
const char *SIG_PLAYER_LEFT = "player_left";
const char *SIG_JOIN_FAILED = "join_failed";
const char *SIG_SERVER_TRANSFERRED = "server_transferred";
const char *SIG_PAUSED_CHANGED = "paused_changed";

} // namespace

NetwMultiplayer *NetwSession::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwSession::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
}

void NetwSession::announce_state_changed(State p_state) {
    emit_signal(StringName(SIG_STATE_CHANGED), p_state);
}

void NetwSession::announce_paused_changed(
    bool p_paused,
    const String &p_reason
) {
    emit_signal(StringName(SIG_PAUSED_CHANGED), p_paused, p_reason);
}

void NetwSession::announce_entered() {
    emit_signal(StringName(SIG_ENTERED));
}

void NetwSession::announce_exiting(
    ExitReason p_reason,
    const String &p_detail
) {
    emit_signal(StringName(SIG_EXITING), p_reason, p_detail);
}

void NetwSession::announce_exited(ExitReason p_reason, const String &p_detail) {
    emit_signal(StringName(SIG_EXITED), p_reason, p_detail);
}

void NetwSession::announce_player_joined(const Ref<NetwPlayer> &p_who) {
    emit_signal(StringName(SIG_PLAYER_JOINED), p_who);
}

void NetwSession::announce_local_joined(const Ref<NetwPlayer> &p_who) {
    emit_signal(StringName(SIG_LOCAL_JOINED), p_who);
}

void NetwSession::announce_player_left(
    const Ref<NetwPlayer> &p_who,
    enums::NetwPlayer::LeaveReason p_reason
) {
    emit_signal(StringName(SIG_PLAYER_LEFT), p_who, p_reason);
}

void NetwSession::announce_join_failed(int64_t p_code, const String &p_reason) {
    emit_signal(StringName(SIG_JOIN_FAILED), p_code, p_reason);
}

Error NetwSession::kick(const Ref<NetwPlayer> &p_who, const String &p_reason) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_kick(p_who, p_reason)
                          : ERR_UNCONFIGURED;
}

Error NetwSession::transfer_server(int64_t p_peer) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_transfer_server(p_peer)
                          : ERR_UNCONFIGURED;
}

void NetwSession::announce_scene_ready(const Ref<NetwScene> &p_scene) {
    emit_signal(StringName(SIG_SCENE_READY), p_scene);
}

void NetwSession::announce_server_transferred(
    int64_t p_successor_was,
    int64_t p_former_now
) {
    emit_signal(
        StringName(SIG_SERVER_TRANSFERRED),
        p_successor_was,
        p_former_now
    );
}

TypedArray<NetwPlayer> NetwSession::get_players() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_joined_all() : TypedArray<NetwPlayer>();
}

Ref<NetwPlayer> NetwSession::get_local_player() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_local() : Ref<NetwPlayer>();
}

Ref<NetwPlayer> NetwSession::player_of(int64_t p_peer) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_get_player(p_peer) : Ref<NetwPlayer>();
}

Ref<NetwPlayer> NetwSession::find_player(const String &p_account) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_find_account(p_account)
                          : Ref<NetwPlayer>();
}

Variant NetwSession::bucket_of(int64_t p_peer, const Variant &p_type) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_get_bucket(p_peer, p_type) : Variant();
}

NetwSession::State NetwSession::get_state() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_get_computed_state()
                          : NetwSession::STATE_OFFLINE;
}

NetwSession::Role NetwSession::get_role() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_get_role() : NetwSession::ROLE_NONE;
}

bool NetwSession::get_is_online() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->is_online();
}

bool NetwSession::is_paused() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->session_is_paused();
}

bool NetwSession::get_is_local_client() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->is_local_client();
}

Node *NetwSession::get_root() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_root() : nullptr;
}

TypedArray<NetwScene> NetwSession::get_scenes() const {
    TypedArray<NetwScene> handles;
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return handles;
    }
    const TypedArray<RID> live = api->scene_list();
    handles.resize(live.size());
    for (int at = 0; at < live.size(); ++at) {
        handles[at] = api->scene_handle_of(RID(live[at]));
    }
    return handles;
}

Ref<NetwSessionConfig> NetwSession::get_config() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_get_config()
                          : Ref<NetwSessionConfig>();
}

void NetwSession::set_server_info(const Ref<NetwServerInfo> &p_info) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->session_set_server_info(p_info);
    }
}

Dictionary NetwSession::get_stats() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->stats_snapshot() : Dictionary();
}

void NetwSession::set_peer_bandwidth(int64_t p_peer, int64_t p_kbps) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->session_set_peer_bandwidth(p_peer, p_kbps);
    }
}

Ref<NetwPromise> NetwSession::leave() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_leave()
                          : NetwPromise::resolved(ERR_UNCONFIGURED);
}

Ref<NetwPromise> NetwSession::step_away() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_step_away()
                          : NetwPromise::resolved(ERR_UNCONFIGURED);
}

void NetwSession::pause(const String &p_reason) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->session_pause(p_reason);
    }
}

void NetwSession::unpause() {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->session_unpause();
    }
}

Ref<NetwPromise> NetwSession::save_entities() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->persist_flush_all()
                          : NetwPromise::resolved(ERR_UNCONFIGURED);
}

Ref<NetwScene> NetwSession::find_scene(const StringName &p_label) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return Ref<NetwScene>();
    }
    return api->scene_handle_of(api->scene_find(p_label));
}

Error NetwSession::install_physics_stepper(
    const Ref<World3D> &p_world,
    const Ref<NetwPhysicsStepper> &p_stepper
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_UNCONFIGURED;
    }
    NETW_ERR_COND_V(
        p_world.is_null(),
        ERR_INVALID_PARAMETER,
        sys::PREDICTION,
        "NetwSession.install_physics_stepper needs a World3D. Pass "
        "get_world_3d() of a node in the scene."
    );
    return api->predict_stepper_install(p_world->get_space(), p_stepper);
}

Error NetwSession::install_physics_stepper_2d(
    const Ref<World2D> &p_world,
    const Ref<NetwPhysicsStepper> &p_stepper
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_UNCONFIGURED;
    }
    NETW_ERR_COND_V(
        p_world.is_null(),
        ERR_INVALID_PARAMETER,
        sys::PREDICTION,
        "NetwSession.install_physics_stepper_2d needs a World2D. Pass "
        "get_world_2d() of a node in the scene."
    );
    return api->predict_stepper_install(p_world->get_space(), p_stepper);
}

Ref<NetwClock> NetwSession::get_clock() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->get_clock() : Ref<NetwClock>();
}

Ref<NetwConnection> NetwSession::get_connection() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->get_connection() : Ref<NetwConnection>();
}

Ref<NetwDatabase> NetwSession::database(const StringName &p_name) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? NetwDatabase::named(api, p_name)
                          : Ref<NetwDatabase>();
}

Ref<NetwTable> NetwSession::table(const StringName &p_name) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? NetwTable::named(api, p_name) : Ref<NetwTable>();
}

Ref<NetwInterestLayer> NetwSession::interest_layer(
    const StringName &p_name
) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->interest_layer(p_name)
                          : Ref<NetwInterestLayer>();
}

Node *NetwSession::service(Object *p_type) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? Object::cast_to<Node>(api->get_service(p_type))
                          : nullptr;
}

void NetwSession::register_service(Object *p_service, Object *p_type) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->service_register(p_service, p_type);
    }
}

void NetwSession::unregister_service(Object *p_service, Object *p_type) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->service_unregister(p_service, p_type);
    }
}

void NetwSession::_bind_methods() {
    BIND_ENUM_CONSTANT(STATE_OFFLINE);
    BIND_ENUM_CONSTANT(STATE_CREATING);
    BIND_ENUM_CONSTANT(STATE_CONNECTING);
    BIND_ENUM_CONSTANT(STATE_ONLINE);
    BIND_ENUM_CONSTANT(STATE_DISCONNECTING);
    BIND_ENUM_CONSTANT(ROLE_NONE);
    BIND_ENUM_CONSTANT(ROLE_CLIENT);
    BIND_ENUM_CONSTANT(ROLE_DEDICATED_SERVER);
    BIND_ENUM_CONSTANT(ROLE_LISTEN_SERVER);
    ClassDB::bind_method(D_METHOD("get_players"), &NetwSession::get_players);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "players",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwPlayer"
        ),
        "",
        "get_players"
    );
    ClassDB::bind_method(
        D_METHOD("get_local_player"),
        &NetwSession::get_local_player
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "local_player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_local_player"
    );
    ClassDB::bind_method(
        D_METHOD("player_of", "peer"),
        &NetwSession::player_of
    );
    ClassDB::bind_method(
        D_METHOD("find_player", "account"),
        &NetwSession::find_player
    );
    ClassDB::bind_method(
        D_METHOD("bucket_of", "peer", "type"),
        &NetwSession::bucket_of
    );

    ClassDB::bind_method(D_METHOD("get_state"), &NetwSession::get_state);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "state",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSession.State"
        ),
        "",
        "get_state"
    );
    ClassDB::bind_method(D_METHOD("get_role"), &NetwSession::get_role);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "role",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSession.Role"
        ),
        "",
        "get_role"
    );
    ClassDB::bind_method(
        D_METHOD("get_is_online"),
        &NetwSession::get_is_online
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_online"), "", "get_is_online");
    ClassDB::bind_method(D_METHOD("is_paused"), &NetwSession::is_paused);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "paused"), "", "is_paused");
    ClassDB::bind_method(
        D_METHOD("get_is_local_client"),
        &NetwSession::get_is_local_client
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_local_client"),
        "",
        "get_is_local_client"
    );
    ClassDB::bind_method(D_METHOD("get_root"), &NetwSession::get_root);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "root",
            PROPERTY_HINT_NODE_TYPE,
            "Node",
            PROPERTY_USAGE_NONE,
            "Node"
        ),
        "",
        "get_root"
    );
    ClassDB::bind_method(D_METHOD("get_scenes"), &NetwSession::get_scenes);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "scenes",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwScene"
        ),
        "",
        "get_scenes"
    );

    ClassDB::bind_method(D_METHOD("get_config"), &NetwSession::get_config);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "config",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSessionConfig",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_config"
    );
    ClassDB::bind_method(
        D_METHOD("set_server_info", "info"),
        &NetwSession::set_server_info
    );
    ClassDB::bind_method(D_METHOD("get_stats"), &NetwSession::get_stats);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "stats"), "", "get_stats");

    ClassDB::bind_method(
        D_METHOD("set_peer_bandwidth", "peer", "kbps"),
        &NetwSession::set_peer_bandwidth
    );
    ClassDB::bind_method(
        D_METHOD("transfer_server", "peer_id"),
        &NetwSession::transfer_server
    );

    ClassDB::bind_method(D_METHOD("leave"), &NetwSession::leave);
    ClassDB::bind_method(D_METHOD("step_away"), &NetwSession::step_away);
    ClassDB::bind_method(
        D_METHOD("pause", "reason"),
        &NetwSession::pause,
        DEFVAL(String())
    );
    ClassDB::bind_method(D_METHOD("unpause"), &NetwSession::unpause);
    ClassDB::bind_method(
        D_METHOD("save_entities"),
        &NetwSession::save_entities
    );
    ClassDB::bind_method(
        D_METHOD("kick", "player", "reason"),
        &NetwSession::kick,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("find_scene", "label"),
        &NetwSession::find_scene
    );
    ClassDB::bind_method(
        D_METHOD("install_physics_stepper", "world", "stepper"),
        &NetwSession::install_physics_stepper
    );
    ClassDB::bind_method(
        D_METHOD("install_physics_stepper_2d", "world", "stepper"),
        &NetwSession::install_physics_stepper_2d
    );
    ClassDB::bind_method(D_METHOD("get_clock"), &NetwSession::get_clock);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "clock",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwClock",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_clock"
    );
    ClassDB::bind_method(
        D_METHOD("get_connection"),
        &NetwSession::get_connection
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "connection",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwConnection",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_connection"
    );
    ClassDB::bind_method(D_METHOD("database", "name"), &NetwSession::database);
    ClassDB::bind_method(D_METHOD("table", "name"), &NetwSession::table);
    ClassDB::bind_method(
        D_METHOD("interest_layer", "name"),
        &NetwSession::interest_layer
    );
    ClassDB::bind_method(D_METHOD("service", "type"), &NetwSession::service);
    ClassDB::bind_method(
        D_METHOD("register_service", "service", "type"),
        &NetwSession::register_service,
        DEFVAL(Variant())
    );
    ClassDB::bind_method(
        D_METHOD("unregister_service", "service", "type"),
        &NetwSession::unregister_service,
        DEFVAL(Variant())
    );

    BIND_ENUM_CONSTANT(LOCAL_LEAVE);
    BIND_ENUM_CONSTANT(SERVER_SHUTDOWN);
    BIND_ENUM_CONSTANT(CONNECTION_LOST);
    BIND_ENUM_CONSTANT(KICKED);
    BIND_ENUM_CONSTANT(REPLACED);
    BIND_ENUM_CONSTANT(TRANSFER_REJOIN);
    BIND_ENUM_CONSTANT(TRANSFER_CONTINUE);

    const PropertyInfo exit_reason(
        Variant::INT,
        "reason",
        PROPERTY_HINT_NONE,
        "",
        PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
        "NetwSession.ExitReason"
    );
    ADD_SIGNAL(MethodInfo(
        SIG_STATE_CHANGED,
        PropertyInfo(
            Variant::INT,
            "state",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwSession.State"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PAUSED_CHANGED,
        PropertyInfo(Variant::BOOL, "paused"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(SIG_ENTERED));
    ADD_SIGNAL(MethodInfo(
        SIG_EXITING,
        exit_reason,
        PropertyInfo(Variant::STRING, "detail")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_EXITED,
        exit_reason,
        PropertyInfo(Variant::STRING, "detail")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PLAYER_JOINED,
        PropertyInfo(
            Variant::OBJECT,
            "player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LOCAL_JOINED,
        PropertyInfo(
            Variant::OBJECT,
            "player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PLAYER_LEFT,
        PropertyInfo(
            Variant::OBJECT,
            "player",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPlayer"
        ),
        PropertyInfo(
            Variant::INT,
            "reason",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "NetwPlayer.LeaveReason"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_JOIN_FAILED,
        PropertyInfo(
            Variant::INT,
            "code",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "Error"
        ),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_READY,
        PropertyInfo(
            Variant::OBJECT,
            "scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwScene"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SERVER_TRANSFERRED,
        PropertyInfo(Variant::INT, "successor_was"),
        PropertyInfo(Variant::INT, "former_now")
    ));
}

} // namespace netw

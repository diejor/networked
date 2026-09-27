#include "netw/api/session_handle.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/api/server_info.hpp"
#include "netw/api/session_config.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_ENTERED = "entered";
const char *SIG_ENDED = "ended";
const char *SIG_DISCONNECTED = "disconnected";
const char *SIG_DISCONNECTING = "disconnecting";
const char *SIG_PLAYER_JOINED = "player_joined";
const char *SIG_LOCAL_JOINED = "local_joined";
const char *SIG_SCENE_LIVE = "scene_live";
const char *SIG_SCENE_CHANGED = "scene_changed";
const char *SIG_PRESENTATION_CHANGED = "presentation_changed";
const char *SIG_PLAYER_LEFT = "player_left";
const char *SIG_JOIN_FAILED = "join_failed";

} // namespace

NetwMultiplayer *NetwSessionHandle::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwSessionHandle::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
    if (p_session == nullptr) {
        return;
    }
    p_session->connect(
        StringName("session_entered"),
        callable_mp(this, &NetwSessionHandle::relay_entered)
    );
    p_session->connect(
        StringName("session_ended"),
        callable_mp(this, &NetwSessionHandle::relay_ended)
    );
    p_session->connect(
        StringName("server_disconnected"),
        callable_mp(this, &NetwSessionHandle::relay_disconnected)
    );
    p_session->connect(
        StringName("session_server_disconnecting"),
        callable_mp(this, &NetwSessionHandle::relay_disconnecting)
    );
    p_session->connect(
        StringName("player_joined"),
        callable_mp(this, &NetwSessionHandle::relay_player_joined)
    );
    p_session->connect(
        StringName("player_local_joined"),
        callable_mp(this, &NetwSessionHandle::relay_local_joined)
    );
    p_session->connect(
        StringName("player_left"),
        callable_mp(this, &NetwSessionHandle::relay_player_left)
    );
    p_session->connect(
        StringName("session_join_failed"),
        callable_mp(this, &NetwSessionHandle::relay_join_failed)
    );
    p_session->connect(
        StringName("scene_live"),
        callable_mp(this, &NetwSessionHandle::relay_scene_live)
    );
    p_session->connect(
        StringName("scene_changed"),
        callable_mp(this, &NetwSessionHandle::relay_scene_changed)
    );
    p_session->connect(
        StringName("scene_presentation_changed"),
        callable_mp(this, &NetwSessionHandle::relay_presentation_changed)
    );
}

void NetwSessionHandle::relay_entered() {
    emit_signal(StringName(SIG_ENTERED));
}

void NetwSessionHandle::relay_ended() {
    emit_signal(StringName(SIG_ENDED));
}

void NetwSessionHandle::relay_disconnected() {
    emit_signal(StringName(SIG_DISCONNECTED));
}

void NetwSessionHandle::relay_disconnecting(const String &p_reason) {
    emit_signal(StringName(SIG_DISCONNECTING), p_reason);
}

void NetwSessionHandle::relay_player_joined(const Ref<NetwPlayer> &p_who) {
    emit_signal(StringName(SIG_PLAYER_JOINED), p_who);
}

void NetwSessionHandle::relay_local_joined(const Ref<NetwPlayer> &p_who) {
    emit_signal(StringName(SIG_LOCAL_JOINED), p_who);
}

void NetwSessionHandle::relay_player_left(const Ref<NetwPlayer> &p_who) {
    emit_signal(StringName(SIG_PLAYER_LEFT), p_who);
}

void NetwSessionHandle::relay_join_failed(
    int64_t p_code,
    const String &p_reason
) {
    emit_signal(StringName(SIG_JOIN_FAILED), p_code, p_reason);
}

Error NetwSessionHandle::kick(
    const Ref<NetwPlayer> &p_who,
    const String &p_reason
) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_kick(p_who, p_reason)
                          : ERR_UNCONFIGURED;
}

void NetwSessionHandle::relay_scene_live(const Ref<NetwSceneHandle> &p_scene) {
    emit_signal(StringName(SIG_SCENE_LIVE), p_scene);
}

void NetwSessionHandle::relay_scene_changed(
    const Ref<NetwSceneHandle> &p_scene,
    const TypedArray<NetwPlayer> &p_arrived
) {
    emit_signal(StringName(SIG_SCENE_CHANGED), p_scene, p_arrived);
}

void NetwSessionHandle::relay_presentation_changed(
    const Ref<NetwSceneHandle> &p_from,
    const Ref<NetwSceneHandle> &p_to
) {
    emit_signal(StringName(SIG_PRESENTATION_CHANGED), p_from, p_to);
}

Ref<NetwSceneHandle> NetwSessionHandle::get_presented_scene() const {
    NetwMultiplayer *api = session();
    return api == nullptr ? Ref<NetwSceneHandle>()
                          : api->scene_handle_of(api->scene_presented());
}

TypedArray<NetwPlayer> NetwSessionHandle::get_players() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_joined_all() : TypedArray<NetwPlayer>();
}

Ref<NetwPlayer> NetwSessionHandle::get_local_player() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->player_local() : Ref<NetwPlayer>();
}

Ref<NetwPlayer> NetwSessionHandle::player_of(int64_t p_peer) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_get_player(p_peer) : Ref<NetwPlayer>();
}

Variant NetwSessionHandle::bucket_of(
    int64_t p_peer,
    const Variant &p_type
) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->peer_get_bucket(p_peer, p_type) : Variant();
}

int64_t NetwSessionHandle::get_role() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? int64_t(api->session_get_role())
                          : int64_t(NetwMultiplayer::ROLE_NONE);
}

bool NetwSessionHandle::get_is_online() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->is_online();
}

bool NetwSessionHandle::get_is_local_client() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->is_local_client();
}

Node *NetwSessionHandle::get_root() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_root() : nullptr;
}

TypedArray<NetwSceneHandle> NetwSessionHandle::get_scenes() const {
    TypedArray<NetwSceneHandle> handles;
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

Ref<NetwSessionConfig> NetwSessionHandle::get_config() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_get_config()
                          : Ref<NetwSessionConfig>();
}

void NetwSessionHandle::set_server_info(const Ref<NetwServerInfo> &p_info) {
    NetwMultiplayer *api = session();
    if (api != nullptr) {
        api->session_set_server_info(p_info);
    }
}

Dictionary NetwSessionHandle::get_stats() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->stats_snapshot() : Dictionary();
}

Ref<NetwPromise> NetwSessionHandle::leave() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->session_leave()
                          : NetwPromise::resolved(ERR_UNCONFIGURED);
}

Ref<NetwPromise> NetwSessionHandle::save_entities() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->persist_flush_all()
                          : NetwPromise::resolved(ERR_UNCONFIGURED);
}

Ref<NetwPromise> NetwSessionHandle::request_scene(
    const String &p_path,
    int64_t p_scope
) {
    NetwMultiplayer *api = session();
    return api != nullptr
        ? api->scene_request(p_path, NetwMultiplayer::SceneChange(p_scope))
        : NetwPromise::resolved(ERR_UNCONFIGURED);
}

void NetwSessionHandle::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_players"),
        &NetwSessionHandle::get_players
    );
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
        &NetwSessionHandle::get_local_player
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
        D_METHOD("get_presented_scene"),
        &NetwSessionHandle::get_presented_scene
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "presented_scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_presented_scene"
    );
    ClassDB::bind_method(
        D_METHOD("player_of", "peer"),
        &NetwSessionHandle::player_of
    );
    ClassDB::bind_method(
        D_METHOD("bucket_of", "peer", "type"),
        &NetwSessionHandle::bucket_of
    );

    ClassDB::bind_method(D_METHOD("get_role"), &NetwSessionHandle::get_role);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "role"), "", "get_role");
    ClassDB::bind_method(
        D_METHOD("get_is_online"),
        &NetwSessionHandle::get_is_online
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_online"), "", "get_is_online");
    ClassDB::bind_method(
        D_METHOD("get_is_local_client"),
        &NetwSessionHandle::get_is_local_client
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_local_client"),
        "",
        "get_is_local_client"
    );
    ClassDB::bind_method(D_METHOD("get_root"), &NetwSessionHandle::get_root);
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
    ClassDB::bind_method(
        D_METHOD("get_scenes"),
        &NetwSessionHandle::get_scenes
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "scenes",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwSceneHandle"
        ),
        "",
        "get_scenes"
    );

    ClassDB::bind_method(
        D_METHOD("get_config"),
        &NetwSessionHandle::get_config
    );
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
        &NetwSessionHandle::set_server_info
    );
    ClassDB::bind_method(D_METHOD("get_stats"), &NetwSessionHandle::get_stats);
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "stats"), "", "get_stats");

    ClassDB::bind_method(D_METHOD("leave"), &NetwSessionHandle::leave);
    ClassDB::bind_method(
        D_METHOD("save_entities"),
        &NetwSessionHandle::save_entities
    );
    ClassDB::bind_method(
        D_METHOD("kick", "player", "reason"),
        &NetwSessionHandle::kick,
        DEFVAL(String())
    );
    ClassDB::bind_method(
        D_METHOD("request_scene", "path", "scope"),
        &NetwSessionHandle::request_scene,
        DEFVAL(int64_t(NetwMultiplayer::SCENE_CHANGE_SESSION))
    );

    ADD_SIGNAL(MethodInfo(SIG_ENTERED));
    ADD_SIGNAL(MethodInfo(SIG_ENDED));
    ADD_SIGNAL(MethodInfo(SIG_DISCONNECTED));
    ADD_SIGNAL(
        MethodInfo(SIG_DISCONNECTING, PropertyInfo(Variant::STRING, "reason"))
    );
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
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_JOIN_FAILED,
        PropertyInfo(Variant::INT, "code"),
        PropertyInfo(Variant::STRING, "reason")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_LIVE,
        PropertyInfo(
            Variant::OBJECT,
            "scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCENE_CHANGED,
        PropertyInfo(
            Variant::OBJECT,
            "scene",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        ),
        PropertyInfo(
            Variant::ARRAY,
            "arrived",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwPlayer"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PRESENTATION_CHANGED,
        PropertyInfo(
            Variant::OBJECT,
            "from",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        ),
        PropertyInfo(
            Variant::OBJECT,
            "to",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSceneHandle"
        )
    ));
}

} // namespace netw

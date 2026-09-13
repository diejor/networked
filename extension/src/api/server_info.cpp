#include "netw/api/server_info.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

namespace {

const char *KEY_MOTD = "motd";
const char *KEY_VISIBILITY = "visibility";
const char *KEY_PLAYERS = "players";
const char *KEY_MAX_PLAYERS = "max_players";
const char *KEY_GAME_MODE = "game_mode";
const char *KEY_VERSION = "version";
const char *KEY_APP_ID = "app_id";
const char *KEY_IS_LOCAL_LISTENER = "is_local_listener";
const char *KEY_METADATA = "metadata";

} // namespace

Ref<NetwServerInfo> NetwServerInfo::from_session(NetwMultiplayer *p_api) {
    Ref<NetwServerInfo> info;
    if (p_api != nullptr) {
        const Ref<NetwSessionConfig> config = p_api->session_get_config();
        const Ref<NetwServerInfo> declared = config.is_valid()
            ? config->get_server_info()
            : Ref<NetwServerInfo>();
        if (declared.is_valid()) {
            info = declared->duplicate();
        }
    }
    if (info.is_null()) {
        info.instantiate();
    }
    info->set_is_local_listener(true);
    if (p_api != nullptr) {
        info->set_players(p_api->get_connected_participants().size());
        info->set_app_id(p_api->session_get_app_id());
    }
    return info;
}

PackedByteArray NetwServerInfo::to_payload(const Ref<NetwServerInfo> &p_info) {
    if (p_info.is_null()) {
        return PackedByteArray();
    }
    Dictionary out;
    out[KEY_MOTD] = p_info->get_motd();
    out[KEY_PLAYERS] = p_info->get_players();
    out[KEY_MAX_PLAYERS] = p_info->get_max_players();
    out[KEY_GAME_MODE] = p_info->get_game_mode();
    out[KEY_VERSION] = p_info->get_version();
    out[KEY_APP_ID] = p_info->get_app_id();
    out[KEY_IS_LOCAL_LISTENER] = p_info->get_is_local_listener();
    out[KEY_VISIBILITY] = p_info->get_visibility();
    out[KEY_METADATA] = p_info->get_metadata();
    return gd::var_to_bytes(out);
}

Ref<NetwServerInfo> NetwServerInfo::from_payload(
    const PackedByteArray &p_bytes
) {
    if (p_bytes.is_empty()) {
        return Ref<NetwServerInfo>();
    }
    const Variant decoded = gd::bytes_to_var(p_bytes);
    if (decoded.get_type() != Variant::DICTIONARY) {
        return Ref<NetwServerInfo>();
    }
    const Dictionary row = decoded;
    Ref<NetwServerInfo> info;
    info.instantiate();
    info->set_motd(row.get(KEY_MOTD, String()));
    info->set_visibility(row.get(KEY_VISIBILITY, int64_t(0)));
    info->set_players(int64_t(row.get(KEY_PLAYERS, 0)));
    info->set_max_players(int64_t(row.get(KEY_MAX_PLAYERS, 0)));
    info->set_game_mode(StringName(row.get(KEY_GAME_MODE, String())));
    info->set_version(row.get(KEY_VERSION, String()));
    info->set_app_id(StringName(row.get(KEY_APP_ID, String())));
    info->set_is_local_listener(bool(row.get(KEY_IS_LOCAL_LISTENER, false)));
    const Variant carried = row.get(KEY_METADATA, Dictionary());
    info->set_metadata(
        carried.get_type() == Variant::DICTIONARY ? Dictionary(carried)
                                                  : Dictionary()
    );
    return info;
}

void NetwServerInfo::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_motd"), &NetwServerInfo::get_motd);
    ClassDB::bind_method(
        D_METHOD("set_motd", "motd"),
        &NetwServerInfo::set_motd
    );
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "motd"), "set_motd", "get_motd");

    ClassDB::bind_method(D_METHOD("get_players"), &NetwServerInfo::get_players);
    ClassDB::bind_method(
        D_METHOD("set_players", "players"),
        &NetwServerInfo::set_players
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "players"),
        "set_players",
        "get_players"
    );

    ClassDB::bind_method(
        D_METHOD("get_max_players"),
        &NetwServerInfo::get_max_players
    );
    ClassDB::bind_method(
        D_METHOD("set_max_players", "max_players"),
        &NetwServerInfo::set_max_players
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "max_players"),
        "set_max_players",
        "get_max_players"
    );

    ClassDB::bind_method(
        D_METHOD("get_game_mode"),
        &NetwServerInfo::get_game_mode
    );
    ClassDB::bind_method(
        D_METHOD("set_game_mode", "game_mode"),
        &NetwServerInfo::set_game_mode
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "game_mode"),
        "set_game_mode",
        "get_game_mode"
    );

    ClassDB::bind_method(D_METHOD("get_version"), &NetwServerInfo::get_version);
    ClassDB::bind_method(
        D_METHOD("set_version", "version"),
        &NetwServerInfo::set_version
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "version"),
        "set_version",
        "get_version"
    );

    ClassDB::bind_method(D_METHOD("get_app_id"), &NetwServerInfo::get_app_id);
    ClassDB::bind_method(
        D_METHOD("set_app_id", "app_id"),
        &NetwServerInfo::set_app_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "app_id"),
        "set_app_id",
        "get_app_id"
    );

    ClassDB::bind_method(
        D_METHOD("get_is_local_listener"),
        &NetwServerInfo::get_is_local_listener
    );
    ClassDB::bind_method(
        D_METHOD("set_is_local_listener", "is_local_listener"),
        &NetwServerInfo::set_is_local_listener
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "is_local_listener"),
        "set_is_local_listener",
        "get_is_local_listener"
    );

    ClassDB::bind_method(
        D_METHOD("get_metadata"),
        &NetwServerInfo::get_metadata
    );
    ClassDB::bind_method(
        D_METHOD("set_metadata", "metadata"),
        &NetwServerInfo::set_metadata
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "metadata"),
        "set_metadata",
        "get_metadata"
    );

    ClassDB::bind_method(
        D_METHOD("get_latency_ms"),
        &NetwServerInfo::get_latency_ms
    );
    ClassDB::bind_method(
        D_METHOD("set_latency_ms", "latency_ms"),
        &NetwServerInfo::set_latency_ms
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "latency_ms"),
        "set_latency_ms",
        "get_latency_ms"
    );

    ClassDB::bind_method(
        D_METHOD("get_visibility"),
        &NetwServerInfo::get_visibility
    );
    ClassDB::bind_method(
        D_METHOD("set_visibility", "visibility"),
        &NetwServerInfo::set_visibility
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "visibility",
            PROPERTY_HINT_ENUM,
            "Public,Friends Only,Private"
        ),
        "set_visibility",
        "get_visibility"
    );

    BIND_ENUM_CONSTANT(VISIBILITY_PUBLIC);
    BIND_ENUM_CONSTANT(VISIBILITY_FRIENDS_ONLY);
    BIND_ENUM_CONSTANT(VISIBILITY_PRIVATE);

    ClassDB::bind_static_method(
        "NetwServerInfo",
        D_METHOD("from_session", "api"),
        &NetwServerInfo::from_session
    );
    ClassDB::bind_static_method(
        "NetwServerInfo",
        D_METHOD("to_payload", "info"),
        &NetwServerInfo::to_payload
    );
    ClassDB::bind_static_method(
        "NetwServerInfo",
        D_METHOD("from_payload", "bytes"),
        &NetwServerInfo::from_payload
    );
}

void NetwServerInfo::copy_values_from(const NetwServerInfo &p_source) {
    motd = p_source.motd;
    players = p_source.players;
    max_players = p_source.max_players;
    game_mode = p_source.game_mode;
    version = p_source.version;
    app_id = p_source.app_id;
    is_local_listener = p_source.is_local_listener;
    latency_ms = p_source.latency_ms;
    visibility = p_source.visibility;
    metadata = p_source.metadata.duplicate(true);
}

} // namespace netw

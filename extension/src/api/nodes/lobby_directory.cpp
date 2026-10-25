#include "netw/api/nodes/lobby_directory.hpp"

#include "godot/class_db.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_PEER_READY = "lobby_peer_ready";
const char *SIG_FAILED = "lobby_failed";
const char *SIG_LIST_PUBLISHED = "lobby_list_published";
const char *SIG_PROVIDER_UNAVAILABLE = "provider_unavailable";

} // namespace

void LobbyDirectory::deliver(const Ref<MultiplayerPeer> &p_peer) {
    emit_signal(StringName(SIG_PEER_READY), p_peer);
}

void LobbyDirectory::fail(Error p_error, const String &p_message) {
    emit_signal(StringName(SIG_FAILED), p_error, p_message);
}

void LobbyDirectory::publish_lobbies(
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const TypedArray<NetwServerInfo> &p_infos
) {
    emit_signal(StringName(SIG_LIST_PUBLISHED), p_addresses, p_names, p_infos);
}

void LobbyDirectory::invite(
    const String &p_address,
    const String &p_from_name
) {
    NetwMultiplayer *session
        = Object::cast_to<NetwMultiplayer>(gd::object_of(watcher));
    if (session == nullptr) {
        NETW_WARN(
            sys::TRANSPORT,
            "'%s' got an invite before it joined a session, so the invite was "
            "dropped. Place the directory under a MultiplayerViewport.",
            String(get_name())
        );
        return;
    }
    session->get_connection()->invite(peer_class(), p_address, p_from_name);
}

void LobbyDirectory::watched_by(NetwMultiplayer *p_session) {
    watcher = gd::instance_id(p_session);
}

void LobbyDirectory::unwatched_by(NetwMultiplayer *p_session) {
    if (watcher == gd::instance_id(p_session)) {
        watcher = ObjectID();
    }
}

bool LobbyDirectory::supports(Capability p_capability) {
    return (get_capabilities() & int64_t(p_capability)) != 0;
}

String LobbyDirectory::get_display_name() {
    return display_name_value.is_empty() ? String(peer_class())
                                         : display_name_value;
}

StringName LobbyDirectory::peer_class() {
    StringName answered;
    if (GDVIRTUAL_CALL(_peer_class, answered)) {
        return answered;
    }
    return StringName();
}

bool LobbyDirectory::is_available() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_is_available, answered)) {
        return answered;
    }
    return true;
}

Dictionary LobbyDirectory::host_settings() {
    Dictionary answered;
    if (GDVIRTUAL_CALL(_host_settings, answered)) {
        return answered;
    }
    return Dictionary();
}

Dictionary LobbyDirectory::client_settings() {
    Dictionary answered;
    if (GDVIRTUAL_CALL(_client_settings, answered)) {
        return answered;
    }
    return Dictionary();
}

String LobbyDirectory::join_address() {
    String answered;
    if (GDVIRTUAL_CALL(_join_address, answered)) {
        return answered;
    }
    return String();
}

String LobbyDirectory::member_name_default(int64_t p_peer_id) {
    return String("Player ") + String::num_int64(p_peer_id);
}

String LobbyDirectory::local_member_name_default() {
    return String("Player");
}

String LobbyDirectory::member_name(int64_t p_peer_id) {
    String answered;
    if (GDVIRTUAL_CALL(_member_name, p_peer_id, answered)) {
        return answered;
    }
    return member_name_default(p_peer_id);
}

String LobbyDirectory::account_of(int64_t p_peer_id) {
    String answered;
    if (GDVIRTUAL_CALL(_account_of, p_peer_id, answered)) {
        return answered;
    }
    return String();
}

String LobbyDirectory::local_member_name() {
    String answered;
    if (GDVIRTUAL_CALL(_local_member_name, answered)) {
        return answered;
    }
    return local_member_name_default();
}

void LobbyDirectory::list_lobbies() {
    GDVIRTUAL_CALL(_list_lobbies);
}

void LobbyDirectory::stop_listing() {
    GDVIRTUAL_CALL(_stop_listing);
}

void LobbyDirectory::leave_lobby() {
    GDVIRTUAL_CALL(_leave_lobby);
}

void LobbyDirectory::host_lobby(const Dictionary &p_settings) {
    GDVIRTUAL_CALL(_host_lobby, p_settings);
}

void LobbyDirectory::join_lobby(const String &p_address) {
    GDVIRTUAL_CALL(_join_lobby, p_address);
}

Ref<NetwServerInfo> LobbyDirectory::listed_info(NetwMultiplayer *p_session) {
    if (p_session == nullptr || !p_session->is_online()
        || !p_session->has_server_role()) {
        return Ref<NetwServerInfo>();
    }
    bool answered = false;
    const Ref<NetwServerInfo> info = NetwServerInfo::from_payload(
        p_session->probe_reply_payload(answered)
    );
    if (!answered || info.is_null()) {
        return Ref<NetwServerInfo>();
    }
    const int64_t visibility = info->get_visibility();
    if (visibility == NetwServerInfo::VISIBILITY_FRIENDS_ONLY
        && !warned_friends) {
        warned_friends = true;
        NETW_WARN(
            sys::TRANSPORT,
            "%s has no friends list, so this game is private. Use "
            "VISIBILITY_PRIVATE or VISIBILITY_PUBLIC.",
            get_class()
        );
    }
    return visibility == NetwServerInfo::VISIBILITY_PUBLIC
        ? info
        : Ref<NetwServerInfo>();
}

void LobbyDirectory::_bind_methods() {
    ClassDB::bind_method(D_METHOD("deliver", "peer"), &LobbyDirectory::deliver);
    ClassDB::bind_method(
        D_METHOD("fail", "error", "message"),
        &LobbyDirectory::fail
    );
    ClassDB::bind_method(
        D_METHOD("publish_lobbies", "addresses", "names", "infos"),
        &LobbyDirectory::publish_lobbies
    );
    ClassDB::bind_method(
        D_METHOD("invite", "address", "from_name"),
        &LobbyDirectory::invite
    );
    ClassDB::bind_method(
        D_METHOD("supports", "capability"),
        &LobbyDirectory::supports
    );
    ClassDB::bind_static_method(
        "LobbyDirectory",
        D_METHOD("member_name_default", "peer_id"),
        &LobbyDirectory::member_name_default
    );
    ClassDB::bind_static_method(
        "LobbyDirectory",
        D_METHOD("local_member_name_default"),
        &LobbyDirectory::local_member_name_default
    );

    ClassDB::bind_method(
        D_METHOD("set_display_name", "value"),
        &LobbyDirectory::set_display_name
    );
    ClassDB::bind_method(
        D_METHOD("get_display_name"),
        &LobbyDirectory::get_display_name
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "display_name"),
        "set_display_name",
        "get_display_name"
    );
    ClassDB::bind_method(
        D_METHOD("set_address_label", "value"),
        &LobbyDirectory::set_address_label
    );
    ClassDB::bind_method(
        D_METHOD("get_address_label"),
        &LobbyDirectory::get_address_label
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "address_label"),
        "set_address_label",
        "get_address_label"
    );
    ClassDB::bind_method(
        D_METHOD("set_address_placeholder", "value"),
        &LobbyDirectory::set_address_placeholder
    );
    ClassDB::bind_method(
        D_METHOD("get_address_placeholder"),
        &LobbyDirectory::get_address_placeholder
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "address_placeholder"),
        "set_address_placeholder",
        "get_address_placeholder"
    );
    ClassDB::bind_method(
        D_METHOD("set_address_help", "value"),
        &LobbyDirectory::set_address_help
    );
    ClassDB::bind_method(
        D_METHOD("get_address_help"),
        &LobbyDirectory::get_address_help
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "address_help"),
        "set_address_help",
        "get_address_help"
    );
    ClassDB::bind_method(
        D_METHOD("set_accepts_empty_address", "value"),
        &LobbyDirectory::set_accepts_empty_address
    );
    ClassDB::bind_method(
        D_METHOD("get_accepts_empty_address"),
        &LobbyDirectory::get_accepts_empty_address
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "accepts_empty_address"),
        "set_accepts_empty_address",
        "get_accepts_empty_address"
    );
    ClassDB::bind_method(
        D_METHOD("set_can_host_here", "value"),
        &LobbyDirectory::set_can_host_here
    );
    ClassDB::bind_method(
        D_METHOD("get_can_host_here"),
        &LobbyDirectory::get_can_host_here
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "can_host_here"),
        "set_can_host_here",
        "get_can_host_here"
    );
    ClassDB::bind_method(
        D_METHOD("set_can_probe", "value"),
        &LobbyDirectory::set_can_probe
    );
    ClassDB::bind_method(
        D_METHOD("get_can_probe"),
        &LobbyDirectory::get_can_probe
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "can_probe"),
        "set_can_probe",
        "get_can_probe"
    );
    ClassDB::bind_method(
        D_METHOD("set_timeout_hint", "value"),
        &LobbyDirectory::set_timeout_hint
    );
    ClassDB::bind_method(
        D_METHOD("get_timeout_hint"),
        &LobbyDirectory::get_timeout_hint
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "timeout_hint"),
        "set_timeout_hint",
        "get_timeout_hint"
    );
    ClassDB::bind_method(
        D_METHOD("set_list_interval", "value"),
        &LobbyDirectory::set_list_interval
    );
    ClassDB::bind_method(
        D_METHOD("get_list_interval"),
        &LobbyDirectory::get_list_interval
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "list_interval"),
        "set_list_interval",
        "get_list_interval"
    );
    ClassDB::bind_method(
        D_METHOD("set_capabilities", "value"),
        &LobbyDirectory::set_capabilities
    );
    ClassDB::bind_method(
        D_METHOD("get_capabilities"),
        &LobbyDirectory::get_capabilities
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "capabilities",
            PROPERTY_HINT_FLAGS,
            "Browse,Friends Only Support,Invites,Friend Names"
        ),
        "set_capabilities",
        "get_capabilities"
    );

    GDVIRTUAL_BIND(_peer_class);
    GDVIRTUAL_BIND(_is_available);
    GDVIRTUAL_BIND(_host_settings);
    GDVIRTUAL_BIND(_client_settings);
    GDVIRTUAL_BIND(_join_address);
    GDVIRTUAL_BIND(_member_name, "peer_id");
    GDVIRTUAL_BIND(_account_of, "peer_id");
    GDVIRTUAL_BIND(_local_member_name);
    GDVIRTUAL_BIND(_list_lobbies);
    GDVIRTUAL_BIND(_stop_listing);
    GDVIRTUAL_BIND(_leave_lobby);
    GDVIRTUAL_BIND(_host_lobby, "settings");
    GDVIRTUAL_BIND(_join_lobby, "address");

    ADD_SIGNAL(MethodInfo(
        SIG_PEER_READY,
        PropertyInfo(
            Variant::OBJECT,
            "peer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "MultiplayerPeer"
        )
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_FAILED,
        PropertyInfo(
            Variant::INT,
            "error",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_CLASS_IS_ENUM,
            "Error"
        ),
        PropertyInfo(Variant::STRING, "message")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LIST_PUBLISHED,
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "addresses"),
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "names"),
        PropertyInfo(Variant::ARRAY, "infos")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_PROVIDER_UNAVAILABLE,
        PropertyInfo(Variant::STRING, "reason")
    ));

    BIND_ENUM_CONSTANT(CAPABILITY_BROWSE);
    BIND_ENUM_CONSTANT(CAPABILITY_FRIENDS_ONLY_SUPPORT);
    BIND_ENUM_CONSTANT(CAPABILITY_INVITES);
    BIND_ENUM_CONSTANT(CAPABILITY_FRIEND_NAMES);
}

} // namespace netw

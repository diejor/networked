#include "netw/api/nodes/lobby_directory.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_PEER_READY = "lobby_peer_ready";
const char *SIG_FAILED = "lobby_failed";
const char *SIG_LIST_PUBLISHED = "lobby_list_published";
const char *SIG_INVITE_RECEIVED = "invite_received";
const char *SIG_PROVIDER_UNAVAILABLE = "provider_unavailable";

} // namespace

void LobbyDirectory::deliver(const Ref<MultiplayerPeer> &p_peer) {
    emit_signal(StringName(SIG_PEER_READY), p_peer);
}

void LobbyDirectory::fail(int64_t p_error, const String &p_message) {
    emit_signal(StringName(SIG_FAILED), p_error, p_message);
}

void LobbyDirectory::publish_lobbies(
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const TypedArray<NetwServerInfo> &p_infos
) {
    emit_signal(StringName(SIG_LIST_PUBLISHED), p_addresses, p_names, p_infos);
}

bool LobbyDirectory::supports(int64_t p_capability) {
    return (capabilities() & p_capability) != 0;
}

int64_t LobbyDirectory::capabilities() {
    int64_t answered = 0;
    if (GDVIRTUAL_CALL(_capabilities, answered)) {
        return answered;
    }
    return 0;
}

StringName LobbyDirectory::peer_class() {
    StringName answered;
    if (GDVIRTUAL_CALL(_peer_class, answered)) {
        return answered;
    }
    return StringName();
}

String LobbyDirectory::display_name() {
    String answered;
    if (GDVIRTUAL_CALL(_display_name, answered)) {
        return answered;
    }
    return String(peer_class());
}

bool LobbyDirectory::is_available() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_is_available, answered)) {
        return answered;
    }
    return true;
}

bool LobbyDirectory::can_host_here() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_can_host_here, answered)) {
        return answered;
    }
    return true;
}

bool LobbyDirectory::can_probe() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_can_probe, answered)) {
        return answered;
    }
    return false;
}

String LobbyDirectory::address_label() {
    String answered;
    if (GDVIRTUAL_CALL(_address_label, answered)) {
        return answered;
    }
    return String("Lobby");
}

String LobbyDirectory::address_placeholder() {
    String answered;
    if (GDVIRTUAL_CALL(_address_placeholder, answered)) {
        return answered;
    }
    return String();
}

String LobbyDirectory::address_help() {
    String answered;
    if (GDVIRTUAL_CALL(_address_help, answered)) {
        return answered;
    }
    return String();
}

bool LobbyDirectory::accepts_empty_address() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_accepts_empty_address, answered)) {
        return answered;
    }
    return false;
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

double LobbyDirectory::timeout_hint() {
    double answered = 0.0;
    if (GDVIRTUAL_CALL(_timeout_hint, answered)) {
        return answered;
    }
    return 20.0;
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

void LobbyDirectory::leave_lobby() {
    GDVIRTUAL_CALL(_leave_lobby);
}

void LobbyDirectory::host_lobby(const Dictionary &p_settings) {
    GDVIRTUAL_CALL(_host_lobby, p_settings);
}

void LobbyDirectory::join_lobby(const String &p_address) {
    GDVIRTUAL_CALL(_join_lobby, p_address);
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

    GDVIRTUAL_BIND(_capabilities);
    GDVIRTUAL_BIND(_peer_class);
    GDVIRTUAL_BIND(_display_name);
    GDVIRTUAL_BIND(_is_available);
    GDVIRTUAL_BIND(_can_host_here);
    GDVIRTUAL_BIND(_can_probe);
    GDVIRTUAL_BIND(_address_label);
    GDVIRTUAL_BIND(_address_placeholder);
    GDVIRTUAL_BIND(_address_help);
    GDVIRTUAL_BIND(_accepts_empty_address);
    GDVIRTUAL_BIND(_host_settings);
    GDVIRTUAL_BIND(_client_settings);
    GDVIRTUAL_BIND(_timeout_hint);
    GDVIRTUAL_BIND(_join_address);
    GDVIRTUAL_BIND(_member_name, "peer_id");
    GDVIRTUAL_BIND(_local_member_name);
    GDVIRTUAL_BIND(_list_lobbies);
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
        PropertyInfo(Variant::INT, "error"),
        PropertyInfo(Variant::STRING, "message")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_LIST_PUBLISHED,
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "addresses"),
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "names"),
        PropertyInfo(Variant::ARRAY, "infos")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_INVITE_RECEIVED,
        PropertyInfo(Variant::INT, "lobby_id"),
        PropertyInfo(Variant::INT, "sender_id")
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

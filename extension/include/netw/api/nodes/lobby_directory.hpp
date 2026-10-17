#pragma once

#include <cstdint>

#include "godot/gdvirtual.hpp"
#include "godot/multiplayer.hpp"
#include "godot/net_peers.hpp"
#include "godot/variant.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/nodes/service.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class LobbyDirectory : public NetwService, public enums::LobbyDirectory {
    GDCLASS(LobbyDirectory, NetwService)

    bool warned_friends = false;
    godot::ObjectID watcher;

    godot::String display_name_value = godot::String();
    godot::String address_label_value = godot::String("Lobby");
    godot::String address_placeholder_value = godot::String();
    godot::String address_help_value = godot::String();
    bool accepts_empty_address_value = false;
    bool can_host_here_value = true;
    bool can_probe_value = false;
    double timeout_hint_value = 20.0;
    double list_interval_value = 0.0;
    int64_t capabilities_value = 0;

protected:
    static void _bind_methods();

    godot::Ref<NetwServerInfo> listed_info(NetwMultiplayer *p_session);

    GDVIRTUAL0R(godot::StringName, _peer_class)
    GDVIRTUAL0R(bool, _is_available)
    GDVIRTUAL0R(godot::Dictionary, _host_settings)
    GDVIRTUAL0R(godot::Dictionary, _client_settings)
    GDVIRTUAL0R(godot::String, _join_address)
    GDVIRTUAL1R(godot::String, _member_name, int64_t)
    GDVIRTUAL0R(godot::String, _local_member_name)
    GDVIRTUAL0(_list_lobbies)
    GDVIRTUAL0(_stop_listing)
    GDVIRTUAL0(_leave_lobby)
    GDVIRTUAL1(_host_lobby, const godot::Dictionary &)
    GDVIRTUAL1(_join_lobby, const godot::String &)

public:
    void set_display_name(const godot::String &p_value) {
        display_name_value = p_value;
    }
    godot::String get_display_name();
    void set_address_label(const godot::String &p_value) {
        address_label_value = p_value;
    }
    godot::String get_address_label() const {
        return address_label_value;
    }
    void set_address_placeholder(const godot::String &p_value) {
        address_placeholder_value = p_value;
    }
    godot::String get_address_placeholder() const {
        return address_placeholder_value;
    }
    void set_address_help(const godot::String &p_value) {
        address_help_value = p_value;
    }
    godot::String get_address_help() const {
        return address_help_value;
    }
    void set_accepts_empty_address(bool p_value) {
        accepts_empty_address_value = p_value;
    }
    bool get_accepts_empty_address() const {
        return accepts_empty_address_value;
    }
    void set_can_host_here(bool p_value) {
        can_host_here_value = p_value;
    }
    bool get_can_host_here() const {
        return can_host_here_value;
    }
    void set_can_probe(bool p_value) {
        can_probe_value = p_value;
    }
    bool get_can_probe() const {
        return can_probe_value;
    }
    void set_timeout_hint(double p_value) {
        timeout_hint_value = p_value;
    }
    double get_timeout_hint() const {
        return timeout_hint_value;
    }
    void set_list_interval(double p_value) {
        list_interval_value = p_value;
    }
    double get_list_interval() const {
        return list_interval_value;
    }
    void set_capabilities(int64_t p_value) {
        capabilities_value = p_value;
    }
    int64_t get_capabilities() const {
        return capabilities_value;
    }

    void deliver(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    void fail(godot::Error p_error, const godot::String &p_message);
    void publish_lobbies(
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::TypedArray<NetwServerInfo> &p_infos
    );
    void invite(
        const godot::String &p_address,
        const godot::String &p_from_name
    );
    bool supports(Capability p_capability);

    void watched_by(NetwMultiplayer *p_session);
    void unwatched_by(NetwMultiplayer *p_session);

    virtual godot::StringName peer_class();
    virtual bool is_available();
    virtual godot::Dictionary host_settings();
    virtual godot::Dictionary client_settings();
    virtual godot::String join_address();
    virtual godot::String member_name(int64_t p_peer_id);
    virtual godot::String local_member_name();

    static godot::String member_name_default(int64_t p_peer_id);
    static godot::String local_member_name_default();
    virtual void list_lobbies();
    virtual void stop_listing();
    virtual void leave_lobby();
    virtual void host_lobby(const godot::Dictionary &p_settings);
    virtual void join_lobby(const godot::String &p_address);
};

} // namespace netw

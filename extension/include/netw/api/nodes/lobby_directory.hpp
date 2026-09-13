#pragma once

#include <cstdint>

#include "godot/gdvirtual.hpp"
#include "godot/multiplayer.hpp"
#include "godot/net_peers.hpp"
#include "godot/variant.hpp"
#include "netw/api/nodes/service.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class LobbyDirectory : public NetwService {
    GDCLASS(LobbyDirectory, NetwService)

protected:
    static void _bind_methods();

    GDVIRTUAL0R(int64_t, _capabilities)
    GDVIRTUAL0R(godot::StringName, _peer_class)
    GDVIRTUAL0R(godot::String, _display_name)
    GDVIRTUAL0R(bool, _is_available)
    GDVIRTUAL0R(bool, _can_host_here)
    GDVIRTUAL0R(bool, _can_probe)
    GDVIRTUAL0R(godot::String, _address_label)
    GDVIRTUAL0R(godot::String, _address_placeholder)
    GDVIRTUAL0R(godot::String, _address_help)
    GDVIRTUAL0R(bool, _accepts_empty_address)
    GDVIRTUAL0R(godot::Dictionary, _host_settings)
    GDVIRTUAL0R(godot::Dictionary, _client_settings)
    GDVIRTUAL0R(double, _timeout_hint)
    GDVIRTUAL0R(godot::String, _join_address)
    GDVIRTUAL1R(godot::String, _member_name, int64_t)
    GDVIRTUAL0R(godot::String, _local_member_name)
    GDVIRTUAL0(_list_lobbies)
    GDVIRTUAL0(_leave_lobby)
    GDVIRTUAL1(_host_lobby, const godot::Dictionary &)
    GDVIRTUAL1(_join_lobby, const godot::String &)

public:
    enum Capability {
        CAPABILITY_BROWSE = 1,
        CAPABILITY_FRIENDS_ONLY_SUPPORT = 2,
        CAPABILITY_INVITES = 4,
        CAPABILITY_FRIEND_NAMES = 8,
    };

    void deliver(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    void fail(int64_t p_error, const godot::String &p_message);
    void publish_lobbies(
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::TypedArray<NetwServerInfo> &p_infos
    );
    bool supports(int64_t p_capability);

    virtual int64_t capabilities();
    virtual godot::StringName peer_class();
    virtual godot::String display_name();
    virtual bool is_available();
    virtual bool can_host_here();
    virtual bool can_probe();
    virtual godot::String address_label();
    virtual godot::String address_placeholder();
    virtual godot::String address_help();
    virtual bool accepts_empty_address();
    virtual godot::Dictionary host_settings();
    virtual godot::Dictionary client_settings();
    virtual double timeout_hint();
    virtual godot::String join_address();
    virtual godot::String member_name(int64_t p_peer_id);
    virtual godot::String local_member_name();

    static godot::String member_name_default(int64_t p_peer_id);
    static godot::String local_member_name_default();
    virtual void list_lobbies();
    virtual void leave_lobby();
    virtual void host_lobby(const godot::Dictionary &p_settings);
    virtual void join_lobby(const godot::String &p_address);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::LobbyDirectory::Capability);

#pragma once

#include <cstdint>

#include "godot/gdvirtual.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/server_info.hpp"
#include "netw/connect/probe_client.hpp"

namespace netw {

class NetwMultiplayer;

class NetwTransport : public godot::RefCounted {
    GDCLASS(NetwTransport, godot::RefCounted)

    godot::ObjectID session_id;
    connect::ProbeClient *probing = nullptr;
    godot::Ref<NetwPromise> outcome;
    godot::RID armed_ticket;

    bool speaks_for(const godot::RID &p_ticket) const;

    void probe_authenticating(int64_t p_peer_id);
    void probe_auth_received(
        int64_t p_peer_id,
        const godot::PackedByteArray &p_data
    );
    void probe_connection_failed();
    void probe_authentication_failed(int64_t p_peer_id);

protected:
    static void _bind_methods();

public:
    ~NetwTransport();

    static godot::StringName peer_class_of(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    );

    void bind_session(NetwMultiplayer *p_session);
    godot::Ref<NetwMultiplayer> get_session() const;

    void report(
        const godot::RID &p_ticket,
        const godot::StringName &p_step,
        const godot::String &p_message,
        double p_ratio
    );
    void probe_default(
        const godot::RID &p_ticket,
        const godot::String &p_address
    );

    void arm(
        const godot::RID &p_ticket,
        const godot::Ref<NetwPromise> &p_outcome
    );
    void deliver(
        const godot::RID &p_ticket,
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    );
    void publish_targets(
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos
    );
    void deliver_probe(
        const godot::RID &p_ticket,
        const godot::Ref<NetwServerInfo> &p_info
    );
    void fail(
        const godot::RID &p_ticket,
        godot::Error p_error,
        const godot::String &p_message
    );

    virtual godot::StringName peer_class();
    godot::StringName peer_class_default();
    virtual bool recognizes_peer(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    );
    bool recognizes_peer_default(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    );
    virtual godot::String display_name();
    godot::String display_name_default();
    virtual bool is_available();
    bool is_available_default();
    virtual bool can_host_here();
    bool can_host_here_default();
    virtual bool can_probe();
    bool can_probe_default();
    virtual godot::String address_label();
    godot::String address_label_default();
    virtual godot::String address_placeholder();
    godot::String address_placeholder_default();
    virtual godot::String address_help();
    godot::String address_help_default();
    virtual bool accepts_empty_address();
    bool accepts_empty_address_default();
    virtual godot::Dictionary host_settings();
    godot::Dictionary host_settings_default();
    virtual godot::Dictionary client_settings();
    godot::Dictionary client_settings_default();

    virtual void make_peer(
        const godot::RID &p_ticket,
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
    virtual void cancel_peer_creation(const godot::RID &p_ticket);
    virtual void probe(
        const godot::RID &p_ticket,
        const godot::String &p_address
    );
    virtual bool can_browse();
    bool can_browse_default();
    virtual void browse();
    virtual godot::Ref<godot::MultiplayerPeer> make_probe_peer(
        const godot::String &p_address
    );
    godot::Ref<godot::MultiplayerPeer> make_probe_peer_default(
        const godot::String &p_address
    );

    virtual void adopt(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    virtual void poll(double p_delta);
    virtual godot::String join_address();
    godot::String join_address_default();
    virtual godot::Dictionary diagnostics(int64_t p_peer_id);
    godot::Dictionary diagnostics_default(int64_t p_peer_id);
    virtual double timeout_hint();
    double timeout_hint_default();
    virtual void close();

    GDVIRTUAL0R(godot::StringName, _peer_class)
    GDVIRTUAL1R(bool, _recognizes_peer, godot::Ref<godot::MultiplayerPeer>)
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
    GDVIRTUAL4(
        _make_peer,
        godot::RID,
        int64_t,
        godot::String,
        godot::Dictionary
    )
    GDVIRTUAL1(_cancel_peer_creation, godot::RID)
    GDVIRTUAL2(_probe, godot::RID, godot::String)
    GDVIRTUAL0R(bool, _can_browse)
    GDVIRTUAL0(_browse)
    GDVIRTUAL1R(
        godot::Ref<godot::MultiplayerPeer>,
        _make_probe_peer,
        godot::String
    )
    GDVIRTUAL1(_adopt, godot::Ref<godot::MultiplayerPeer>)
    GDVIRTUAL1(_poll, double)
    GDVIRTUAL0R(godot::String, _join_address)
    GDVIRTUAL1R(godot::Dictionary, _diagnostics, int64_t)
    GDVIRTUAL0R(double, _timeout_hint)
    GDVIRTUAL0(_close)
};

} // namespace netw

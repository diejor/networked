#pragma once

#include <cstdint>

#include "godot/multiplayer.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_transport.hpp"
#include "netw/connect/transport.hpp"

namespace netw::connect {

class ScriptTransport : public Transport {
    godot::Ref<NetwTransport> seam;

public:
    explicit ScriptTransport(const godot::Ref<NetwTransport> &p_seam);

    void bind_session(NetwMultiplayer *p_session) override;

    godot::StringName peer_class() const override;
    bool recognizes_peer(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) const override;
    godot::String display_name() const override;
    bool is_available() const override;
    bool can_host_here() const override;
    bool can_probe() const override;
    godot::String address_label() const override;
    godot::String address_placeholder() const override;
    godot::String address_help() const override;
    bool accepts_empty_address() const override;
    godot::Dictionary host_settings() const override;
    godot::Dictionary client_settings() const override;

    void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) override;
    void cancel_peer_creation() override;
    void probe(const godot::String &p_address) override;
    bool can_browse() const override;
    void browse() override;
    godot::Ref<godot::MultiplayerPeer> make_probe_peer(
        const godot::String &p_address
    ) override;

    void adopt(const godot::Ref<godot::MultiplayerPeer> &p_peer) override;
    void poll(double p_delta) override;
    godot::String join_address() const override;
    godot::Dictionary diagnostics(int64_t p_peer_id) const override;
    double timeout_hint() const override;
    void close() override;

    const godot::Ref<NetwTransport> &script_seam() const {
        return seam;
    }
};

} // namespace netw::connect

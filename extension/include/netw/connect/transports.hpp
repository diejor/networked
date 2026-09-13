#pragma once

#include <cstdint>

#include "godot/multiplayer.hpp"
#include "godot/variant.hpp"
#include "netw/api/loopback.hpp"
#include "netw/connect/transport.hpp"

namespace netw::connect {

int64_t setting_int(
    const godot::Dictionary &p_settings,
    const godot::StringName &p_key,
    int64_t p_fallback
);
godot::String setting_string(
    const godot::Dictionary &p_settings,
    const godot::StringName &p_key,
    const godot::String &p_fallback
);
void split_host_port(
    const godot::String &p_address,
    int64_t p_default_port,
    godot::String &r_host,
    int64_t &r_port
);
godot::String lan_address();

class ENetTransport : public Transport {
public:
    static const int64_t DEFAULT_PORT = 21253;
    static const int64_t DEFAULT_MAX_PLAYERS = 32;

    godot::StringName peer_class() const override;
    godot::String display_name() const override;
    bool is_available() const override;
    bool can_probe() const override;
    godot::String address_label() const override;
    godot::String address_placeholder() const override;
    bool accepts_empty_address() const override;
    godot::Dictionary host_settings() const override;
    godot::Dictionary client_settings() const override;

    void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) override;
    godot::Ref<godot::MultiplayerPeer> make_probe_peer(
        const godot::String &p_address
    ) override;

    void adopt(const godot::Ref<godot::MultiplayerPeer> &p_peer) override;
    godot::String join_address() const override;
    void close() override;

private:
    godot::Ref<godot::MultiplayerPeer> live;

    void host_peer(const godot::Dictionary &p_settings);
    void join_peer(
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
};

class WebSocketTransport : public Transport {
public:
    static const int64_t DEFAULT_PORT = 21253;

    godot::StringName peer_class() const override;
    godot::String display_name() const override;
    bool can_host_here() const override;
    bool can_probe() const override;
    godot::String address_label() const override;
    godot::String address_placeholder() const override;
    bool accepts_empty_address() const override;
    godot::Dictionary host_settings() const override;

    void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) override;

    void adopt(const godot::Ref<godot::MultiplayerPeer> &p_peer) override;
    godot::String join_address() const override;
    void close() override;

    static godot::String build_url(const godot::String &p_address);

private:
    godot::Ref<godot::MultiplayerPeer> live;
    int64_t bound_port = 0;

    void host_peer(const godot::Dictionary &p_settings);
    void join_peer(
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
};

class LocalTransport : public Transport {
public:
    godot::StringName peer_class() const override;
    godot::String display_name() const override;
    bool can_probe() const override;
    godot::String address_label() const override;
    bool accepts_empty_address() const override;

    void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) override;
    void probe(const godot::String &p_address) override;

    void adopt(const godot::Ref<godot::MultiplayerPeer> &p_peer) override;
    void poll(double p_delta) override;
    godot::String join_address() const override;
    void close() override;

private:
    godot::Ref<LocalLoopbackSession> loopback;

    void host_peer(const godot::Dictionary &p_settings);
    void join_peer(
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
};

} // namespace netw::connect

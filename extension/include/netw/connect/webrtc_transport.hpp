#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/connect/room_board.hpp"
#include "netw/connect/signaler.hpp"
#include "netw/connect/transport.hpp"
#include "netw/connect/turn_credentials.hpp"
#include "netw/connect/webrtc_session.hpp"

namespace netw::connect {

class WebRTCTransport : public Transport {
    friend struct WebRTCTransportProbe;

public:
    static const char *LOCAL_ROOMS_PATH;

    ~WebRTCTransport() override;

    godot::StringName peer_class() const override;
    godot::String display_name() const override;
    godot::String address_label() const override;
    godot::String address_placeholder() const override;
    godot::String address_help() const override;
    godot::Dictionary host_settings() const override;
    godot::Dictionary client_settings() const override;
    double timeout_hint() const override;

    void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) override;

    bool can_browse() const override;
    void browse() override;
    void poll(double p_delta) override;
    godot::String join_address() const override;
    godot::Dictionary diagnostics(int64_t p_peer_id) const override;
    void close() override;

    static godot::Array filter_ice_servers(const godot::Array &p_servers);
    static bool is_url_supported_native(const godot::String &p_url);

    static void register_local_room(const godot::String &p_room);
    static void unregister_local_room(const godot::String &p_room);
    static bool is_local_room(const godot::String &p_room);

    static godot::Array default_ice_servers();
    static godot::PackedStringArray default_trackers();
    static bool servers_differ_from_default(const godot::Array &p_servers);

    godot::Array effective_ice_servers() const;

    RoomBoard &board() {
        return rooms;
    }

private:
    godot::String signaling_namespace;
    godot::String room_characters = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    godot::Array ice_servers = default_ice_servers();
    godot::PackedStringArray trackers = default_trackers();
    double connect_retry = 8.0;
    int64_t max_connect_attempts = 3;
    double gather_timeout = 6.0;
    double topup_interval = 0.25;
    bool filter_unsupported_turn = true;

    RoomBoard rooms;
    bool board_backed = false;
    WebRTCSession *session_core = nullptr;
    Signaler *signaler = nullptr;
    bool hosting = false;
    bool signaling_ready = false;
    bool offer_reported = false;
    bool ice_servers_authored = false;
    int64_t connect_started_msec = 0;

    TurnCredentials *credentials = nullptr;
    int pending_mode = -1;
    godot::String pending_address;
    godot::Dictionary pending_settings;

    void host_peer(const godot::Dictionary &p_settings);
    void join_peer(
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
    void apply_settings(const godot::Dictionary &p_settings);
    void build(const godot::Dictionary &p_settings);
    void teardown();
    void publish_listing();
    void pump_signals();
    void pump_outcomes();
    void pump_inbound();
    void watch_signaling(int64_t p_now_msec);

    bool arm_credential_fetch(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
    void drive_credential_fetch(int64_t p_now_msec);
    void resume_peer_creation();

    static godot::PackedStringArray read_local_rooms();
    static void write_local_rooms(const godot::PackedStringArray &p_rooms);
};

} // namespace netw::connect

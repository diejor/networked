#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/nodes/lobby_directory.hpp"

namespace netw {

class NetwMultiplayer;

class LANLobbyDirectory : public LobbyDirectory {
    GDCLASS(LANLobbyDirectory, LobbyDirectory)
    friend struct LANLobbyDirectoryProbe;

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    static const double BROWSE_WINDOW;

    void set_port(int64_t p_port);
    int64_t get_port() const;
    void set_port_count(int64_t p_count);
    int64_t get_port_count() const;

    godot::StringName peer_class() override;
    godot::String display_name() override;
    Capability capabilities() override;
    bool is_available() override;
    godot::Error set_browse_settings(
        const godot::Dictionary &p_settings
    ) override;
    void list_lobbies() override;
    void service_entered(NetwMultiplayer *p_api) override;
    void service_exiting(NetwMultiplayer *p_api) override;

private:
    int64_t port = 24750;
    int64_t port_count = 8;
    NetwMultiplayer *session = nullptr;
    uint64_t instance = 0;

    godot::Ref<godot::RefCounted> host_socket;
    godot::Ref<NetwServerInfo> listed;
    double listed_check = 0.0;
    bool warned_ports = false;

    godot::Ref<godot::RefCounted> browse_socket;
    godot::String query_address = "255.255.255.255";
    uint64_t nonce = 0;
    double window_left = 0.0;
    int64_t asked_usec = 0;
    godot::LocalVector<uint64_t> seen;
    godot::PackedStringArray addresses;
    godot::PackedStringArray names;
    godot::TypedArray<NetwServerInfo> infos;

    void tick(double p_delta);
    uint64_t app_tag() const;
    int64_t gameplay_port() const;
    void keep_host_socket();
    void answer_queries();
    void collect_replies();
    void close_host();
    void close_browse();
};

} // namespace netw

#pragma once

#include "godot/variant.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/connect/room_board.hpp"

namespace netw {

class NetwMultiplayer;

class WebTorrentLobbyDirectory : public LobbyDirectory {
    GDCLASS(WebTorrentLobbyDirectory, LobbyDirectory)
    friend struct WebTorrentLobbyDirectoryProbe;

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    WebTorrentLobbyDirectory();

    void set_trackers(const godot::PackedStringArray &p_trackers);
    godot::PackedStringArray get_trackers() const;

    godot::StringName peer_class() override;
    void list_lobbies() override;
    void stop_listing() override;
    void service_entered(NetwMultiplayer *p_api) override;
    void service_exiting(NetwMultiplayer *p_api) override;

private:
    godot::PackedStringArray trackers;
    connect::RoomBoard board;
    NetwMultiplayer *session = nullptr;
    double listed_check = 0.0;

    godot::Dictionary webrtc_defaults() const;
    godot::PackedStringArray effective_trackers() const;
    godot::String effective_namespace() const;
    bool listed_card(connect::RoomCard &r_card);
    void publish_listing();
};

} // namespace netw

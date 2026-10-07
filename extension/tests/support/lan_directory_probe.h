#pragma once

#include "godot/variant.hpp"
#include "netw/api/nodes/lan_lobby_directory.hpp"

namespace netw {

struct LANLobbyDirectoryProbe {
    static void tick(LANLobbyDirectory *p_directory, double p_delta) {
        p_directory->tick(p_delta);
    }
    static void query_loopback(LANLobbyDirectory *p_directory) {
        p_directory->query_address = godot::String("127.0.0.1");
    }
};

} // namespace netw

#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/rate_window.hpp"

namespace netw {

class NetwMultiplayerCore;

class SessionCore {
public:
    enum State {
        STATE_OFFLINE = 0,
        STATE_CONNECTING = 1,
        STATE_ONLINE = 2,
        STATE_DISCONNECTING = 3,
    };

    enum Role {
        ROLE_NONE = 0,
        ROLE_CLIENT = 1,
        ROLE_DEDICATED_SERVER = 2,
        ROLE_LISTEN_SERVER = 3,
    };

private:
    State state = STATE_OFFLINE;
    Role role = ROLE_NONE;
    Role desired_role = ROLE_LISTEN_SERVER;
    int32_t advertised_max_players = 0;
    NetwMultiplayerCore *host = nullptr;

    RateWindow join_window;

    void enter_state(State next);
    void exit_state(State prev);

public:
    static bool edge_is_legal(State from, State to);

    void announce_to(NetwMultiplayerCore *p_host);

    void set_state(State value);
    State get_state() const;
    void set_role(Role value);
    Role get_role() const;
    void set_desired_role(Role value);
    Role get_desired_role() const;
    void set_advertised_max_players(int value);
    int get_advertised_max_players() const;

    void transition(State next);

    void on_peer_assigned(bool has_live_peer, bool connected, int unique_id);

    void resolve_online(int unique_id);

    bool is_server_role() const;

    bool join_flooded(int sender, int64_t now_msec);

    static int64_t compute_app_tag(const godot::StringName &value);

    void clear();
};

} // namespace netw

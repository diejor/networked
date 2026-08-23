#include "netw/session_core.hpp"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw {

bool SessionCore::edge_is_legal(State from, State to) {
    switch (from) {
        case STATE_OFFLINE:
            return to == STATE_CONNECTING;
        case STATE_CONNECTING:
            return to == STATE_ONLINE || to == STATE_OFFLINE;
        case STATE_ONLINE:
            return to == STATE_DISCONNECTING;
        case STATE_DISCONNECTING:
            return to == STATE_OFFLINE;
    }
    return false;
}

void SessionCore::announce_to(NetwMultiplayerCore *p_host) {
    host = p_host;
}

void SessionCore::set_state(State value) {
    if (state == value) {
        return;
    }
    const State previous = state;
    state = value;
    if (host != nullptr) {
        host->session_announce_edge(previous, value);
    }
}

SessionCore::State SessionCore::get_state() const {
    return state;
}

void SessionCore::set_role(Role value) {
    role = value;
}

SessionCore::Role SessionCore::get_role() const {
    return role;
}

void SessionCore::set_desired_role(Role value) {
    desired_role = value;
}

SessionCore::Role SessionCore::get_desired_role() const {
    return desired_role;
}

void SessionCore::set_advertised_max_players(int value) {
    advertised_max_players = value;
}

int SessionCore::get_advertised_max_players() const {
    return advertised_max_players;
}

void SessionCore::transition(State next) {
    NETW_ZONE_NC("SessionCore transition", colors::SESSION);
    if (state == next) {
        return;
    }
    NETW_ERR_COND(
        !edge_is_legal(state, next),
        sys::SESSION,
        "Illegal session transition %d -> %d.",
        int(state),
        int(next)
    );
    const State previous = state;
    NETW_DEBUG(
        sys::SESSION,
        "transition from=%d to=%d",
        int(previous),
        int(next)
    );
    exit_state(previous);
    set_state(next);
    enter_state(next);
}

void SessionCore::exit_state(State prev) {
    if (prev == STATE_ONLINE && host != nullptr) {
        host->session_announce_ended();
    }
}

void SessionCore::enter_state(State next) {
    if (next == STATE_ONLINE && host != nullptr) {
        host->session_announce_entered();
    }
}

void SessionCore::on_peer_assigned(
    bool has_live_peer,
    bool connected,
    int unique_id
) {
    if (!has_live_peer) {
        if (state == STATE_CONNECTING) {
            transition(STATE_OFFLINE);
        }
        return;
    }
    if (state == STATE_OFFLINE) {
        transition(STATE_CONNECTING);
    }
    if (state != STATE_CONNECTING) {
        return;
    }
    if (connected || unique_id == 1) {
        resolve_online(unique_id);
    }
}

void SessionCore::resolve_online(int unique_id) {
    if (unique_id == 1) {
        role = desired_role == ROLE_LISTEN_SERVER ? ROLE_LISTEN_SERVER
                                                  : ROLE_DEDICATED_SERVER;
    } else {
        role = ROLE_CLIENT;
    }
    transition(STATE_ONLINE);
}

bool SessionCore::is_server_role() const {
    return role == ROLE_DEDICATED_SERVER || role == ROLE_LISTEN_SERVER;
}

bool SessionCore::join_flooded(int sender, int64_t now_msec) {
    return join_window.exceeded(sender, now_msec);
}

int64_t SessionCore::compute_app_tag(const StringName &value) {
    const String text = String(value);
    if (text.is_empty()) {
        return 0;
    }
    const uint64_t wire_identity
        = wire::WireRegistry::create_default().identity_hash();
    return int64_t(
        (uint64_t(uint32_t(text.hash())) ^ wire_identity) & 0xFFFFFFFFULL
    );
}

void SessionCore::clear() {
    state = STATE_OFFLINE;
    role = ROLE_NONE;
    advertised_max_players = 0;
    join_window.clear();
}

} // namespace netw

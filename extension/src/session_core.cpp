#include "netw/session_core.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_STATE_CHANGED = "state_changed";
const char *SIG_SESSION_ENTERED = "session_entered";
const char *SIG_SESSION_ENDED = "session_ended";

} // namespace

bool NetwSessionCore::edge_is_legal(State from, State to) {
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

void NetwSessionCore::set_state(State value) {
    if (state == value) {
        return;
    }
    const State previous = state;
    state = value;
    emit_signal(SIG_STATE_CHANGED, previous, value);
}

NetwSessionCore::State NetwSessionCore::get_state() const {
    return state;
}

void NetwSessionCore::set_role(Role value) {
    role = value;
}

NetwSessionCore::Role NetwSessionCore::get_role() const {
    return role;
}

void NetwSessionCore::set_desired_role(Role value) {
    desired_role = value;
}

NetwSessionCore::Role NetwSessionCore::get_desired_role() const {
    return desired_role;
}

void NetwSessionCore::set_advertised_max_players(int value) {
    advertised_max_players = value;
}

int NetwSessionCore::get_advertised_max_players() const {
    return advertised_max_players;
}

void NetwSessionCore::transition(State next) {
    NETW_ZONE_NC("NetwSessionCore transition", colors::SESSION);
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
    NETW_DEBUG(sys::SESSION, "transition from=%d to=%d", int(previous), int(next));
    exit_state(previous);
    set_state(next);
    enter_state(next);
}

void NetwSessionCore::exit_state(State prev) {
    if (prev == STATE_ONLINE) {
        emit_signal(SIG_SESSION_ENDED);
    }
}

void NetwSessionCore::enter_state(State next) {
    if (next == STATE_ONLINE) {
        emit_signal(SIG_SESSION_ENTERED);
    }
}

void NetwSessionCore::on_peer_assigned(
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

void NetwSessionCore::resolve_online(int unique_id) {
    if (unique_id == 1) {
        role = desired_role == ROLE_LISTEN_SERVER ? ROLE_LISTEN_SERVER
                                                  : ROLE_DEDICATED_SERVER;
    } else {
        role = ROLE_CLIENT;
    }
    transition(STATE_ONLINE);
}

bool NetwSessionCore::is_server_role() const {
    return role == ROLE_DEDICATED_SERVER || role == ROLE_LISTEN_SERVER;
}

bool NetwSessionCore::join_flooded(int sender, int64_t now_msec) {
    if (join_window.is_null()) {
        join_window.instantiate();
    }
    return join_window->exceeded(sender, now_msec);
}

int64_t NetwSessionCore::compute_app_tag(const StringName &value) {
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

void NetwSessionCore::clear() {
    state = STATE_OFFLINE;
    role = ROLE_NONE;
    advertised_max_players = 0;
    if (join_window.is_valid()) {
        join_window->clear();
    }
}

void NetwSessionCore::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_state", "value"),
        &NetwSessionCore::set_state
    );
    ClassDB::bind_method(D_METHOD("get_state"), &NetwSessionCore::get_state);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "state",
            PROPERTY_HINT_ENUM,
            "Offline,Connecting,Online,Disconnecting"
        ),
        "set_state",
        "get_state"
    );

    ClassDB::bind_method(
        D_METHOD("set_role", "value"),
        &NetwSessionCore::set_role
    );
    ClassDB::bind_method(D_METHOD("get_role"), &NetwSessionCore::get_role);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "role",
            PROPERTY_HINT_ENUM,
            "None,Client,Dedicated Server,Listen Server"
        ),
        "set_role",
        "get_role"
    );

    ClassDB::bind_method(
        D_METHOD("set_desired_role", "value"),
        &NetwSessionCore::set_desired_role
    );
    ClassDB::bind_method(
        D_METHOD("get_desired_role"),
        &NetwSessionCore::get_desired_role
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "desired_role",
            PROPERTY_HINT_ENUM,
            "None,Client,Dedicated Server,Listen Server"
        ),
        "set_desired_role",
        "get_desired_role"
    );

    ClassDB::bind_method(
        D_METHOD("set_advertised_max_players", "value"),
        &NetwSessionCore::set_advertised_max_players
    );
    ClassDB::bind_method(
        D_METHOD("get_advertised_max_players"),
        &NetwSessionCore::get_advertised_max_players
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "advertised_max_players"),
        "set_advertised_max_players",
        "get_advertised_max_players"
    );

    ClassDB::bind_static_method(
        "NetwSessionCore",
        D_METHOD("edge_is_legal", "from", "to"),
        &NetwSessionCore::edge_is_legal
    );
    ClassDB::bind_method(
        D_METHOD("transition", "next"),
        &NetwSessionCore::transition
    );
    ClassDB::bind_method(
        D_METHOD("on_peer_assigned", "has_live_peer", "connected", "unique_id"),
        &NetwSessionCore::on_peer_assigned
    );
    ClassDB::bind_method(
        D_METHOD("resolve_online", "unique_id"),
        &NetwSessionCore::resolve_online
    );
    ClassDB::bind_method(
        D_METHOD("is_server_role"),
        &NetwSessionCore::is_server_role
    );
    ClassDB::bind_method(
        D_METHOD("join_flooded", "sender", "now_msec"),
        &NetwSessionCore::join_flooded
    );
    ClassDB::bind_static_method(
        "NetwSessionCore",
        D_METHOD("compute_app_tag", "value"),
        &NetwSessionCore::compute_app_tag
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwSessionCore::clear);

    ADD_SIGNAL(MethodInfo(
        SIG_STATE_CHANGED,
        PropertyInfo(Variant::INT, "old_state"),
        PropertyInfo(Variant::INT, "new_state")
    ));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_ENTERED));
    ADD_SIGNAL(MethodInfo(SIG_SESSION_ENDED));

    BIND_ENUM_CONSTANT(STATE_OFFLINE);
    BIND_ENUM_CONSTANT(STATE_CONNECTING);
    BIND_ENUM_CONSTANT(STATE_ONLINE);
    BIND_ENUM_CONSTANT(STATE_DISCONNECTING);

    BIND_ENUM_CONSTANT(ROLE_NONE);
    BIND_ENUM_CONSTANT(ROLE_CLIENT);
    BIND_ENUM_CONSTANT(ROLE_DEDICATED_SERVER);
    BIND_ENUM_CONSTANT(ROLE_LISTEN_SERVER);
}

} // namespace netw

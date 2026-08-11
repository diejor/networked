#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/rate_window.hpp"

namespace netw {

// The session machine: it holds what state a session is in, what role the local
// peer plays in it, and the two admission rules that decide whether an inbound
// join is even looked at.
//
// It holds no Object and sends nothing. Opening a connection is nobody's
// business here and neither is authenticating one, so the peer is never
// reached: the interface above observes it and hands down the two facts the
// machine needs, the way `handle_pong` hands a clock the sample someone else
// received. That is what lets a session with no scene tree, no node and no
// connect verb still answer what state it is in.
//
// [codeblock]
// OFFLINE ─assign peer─▶ CONNECTING ─success─▶ ONLINE
//    ▲                       │                    │
//    └─── null / offline ────┘   leave / crash ───┤
//    │                                            ▼
//    └──────────────────────────────────── DISCONNECTING
// [/codeblock]
class NetwSessionCore : public godot::RefCounted {
    GDCLASS(NetwSessionCore, godot::RefCounted)

public:
    // Values are the contract, not the order: the public twins on the session
    // shell are defined from these constants, so renumbering here renumbers
    // every mirror with it.
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

    // An honest peer submits one join, twice at most through the resend path,
    // so the window's default budget drops a flood without touching a
    // legitimate burst of clients arriving together.
    godot::Ref<NetwRateWindow> join_window;

    void enter_state(State next);
    void exit_state(State prev);

protected:
    static void _bind_methods();

public:
    // Whether an edge is one the machine admits. Public because the refusal
    // path is a contract a caller can check rather than a crash it has to
    // avoid: the server-crash route reuses ONLINE -> DISCONNECTING -> OFFLINE
    // instead of adding a direct edge, and that is only enforceable if the
    // table is readable.
    static bool edge_is_legal(State from, State to);

    void set_state(State value);
    State get_state() const;
    void set_role(Role value);
    Role get_role() const;
    // The role the local peer intends to play, pushed down from the registered
    // session configuration. It is the only thing that splits a server peer
    // into a listen server and a dedicated one.
    void set_desired_role(Role value);
    Role get_desired_role() const;
    // The player cap the live host advertises, or zero while this session is
    // not hosting, so a probe is answered with a session fact rather than with
    // the configuration the host was built from.
    void set_advertised_max_players(int value);
    int get_advertised_max_players() const;

    // Advances along a legal edge, running the exit hook for the old state
    // then the enter hook for the new one. The same state twice is a no-op
    // rather than a re-entry, which is what keeps a transport repeating itself
    // from firing a second session_entered.
    void transition(State next);

    // Reacts to a peer the interface above just handed the session.
    //
    // `has_live_peer` is false for a null or offline assignment, which is how a
    // cancelled connect arrives instead of through a bespoke abort verb. A
    // server peer is born live whether or not its transport says so, so a
    // unique id of 1 resolves without waiting for a connection that is never
    // coming.
    void on_peer_assigned(bool has_live_peer, bool connected, int unique_id);

    // Completes a connect with the role the peer identity and the configured
    // hint imply. Separate from the assignment edge because a client is still
    // mid-handshake when its peer arrives and finishes on the transport's own
    // signal instead.
    void resolve_online(int unique_id);

    bool is_server_role() const;

    // Whether this sender's join frames exceed the flood window, which is a
    // [NetwRateWindow] with the session's own budget. The host self-join is
    // never limited, so a listen server cannot flood itself out of its own
    // session.
    bool join_flooded(int sender, int64_t now_msec);

    // Folds a build tag into the 32 bits the hello carries. Empty means the
    // compatibility gate is disabled, and everything else is the engine's own
    // string hash, over code points rather than over bytes.
    static int64_t compute_app_tag(const godot::StringName &value);

    // Session teardown: the machine, the role, and the flood window.
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwSessionCore::State);
VARIANT_ENUM_CAST(netw::NetwSessionCore::Role);

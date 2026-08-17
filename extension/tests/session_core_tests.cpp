// The session machine's laws, stated where they can be stated.
//
// The trace beside this file pins what the GDScript session did. These are the
// properties that trace could not reach, and there is one reason for each:
// either the GDScript arm had no way to drive it, or driving it there would
// have crashed the arm rather than answering it.
//
// The window's own edge is the clearest case. `_join_flooded` read the wall
// clock, so a GDScript suite could count a flood and could never watch one
// expire, and the suite that named itself "drops past the window" only ever
// checked the count. Handing the clock down is what turned that into a law.
//
// The illegal edges are the other. GDScript asserted on them, which aborts a
// debug build rather than reporting, so no arm could record the refusal it was
// meant to prove.

#include "support/netw_test.h"

#include <cstdio>

#include "netw/session_core.hpp"
#include "support/netw_recorder.h"

namespace TestNetwSessionCore {

using namespace godot;
using netw::NetwSessionCore;
using netw_test::Recorder;

constexpr int64_t NOW = 1'000'000;
constexpr int64_t WINDOW = 1000;
constexpr int LIMIT = 8;
constexpr int TRACKED_PEERS = 64;

Ref<NetwSessionCore> fresh() {
    Ref<NetwSessionCore> core;
    core.instantiate();
    return core;
}

// Spends a peer's whole budget at one instant and answers whether the next
// frame at that same instant is refused.
bool spend_budget(
    const Ref<NetwSessionCore> &p_core,
    int p_peer,
    int64_t p_now
) {
    for (int attempt = 0; attempt < LIMIT; ++attempt) {
        if (p_core->join_flooded(p_peer, p_now)) {
            return true;
        }
    }
    return p_core->join_flooded(p_peer, p_now);
}

TEST_CASE(
    "[Networked][Session][Hosted] S1 the table admits five edges and refuses "
    "the other eleven"
) {
    const NetwSessionCore::State all[] = {
        NetwSessionCore::STATE_OFFLINE,
        NetwSessionCore::STATE_CONNECTING,
        NetwSessionCore::STATE_ONLINE,
        NetwSessionCore::STATE_DISCONNECTING,
    };
    int admitted = 0;
    for (const NetwSessionCore::State from : all) {
        for (const NetwSessionCore::State to : all) {
            admitted += NetwSessionCore::edge_is_legal(from, to) ? 1 : 0;
        }
    }
    NETW_CHECK_EQ(admitted, 5);

    // Naming them individually is what makes a renumbering fail here rather
    // than pass with a different five.
    CHECK(
        NetwSessionCore::edge_is_legal(
            NetwSessionCore::STATE_OFFLINE,
            NetwSessionCore::STATE_CONNECTING
        )
    );
    CHECK(
        NetwSessionCore::edge_is_legal(
            NetwSessionCore::STATE_CONNECTING,
            NetwSessionCore::STATE_ONLINE
        )
    );
    CHECK(
        NetwSessionCore::edge_is_legal(
            NetwSessionCore::STATE_CONNECTING,
            NetwSessionCore::STATE_OFFLINE
        )
    );
    CHECK(
        NetwSessionCore::edge_is_legal(
            NetwSessionCore::STATE_ONLINE,
            NetwSessionCore::STATE_DISCONNECTING
        )
    );
    CHECK(
        NetwSessionCore::edge_is_legal(
            NetwSessionCore::STATE_DISCONNECTING,
            NetwSessionCore::STATE_OFFLINE
        )
    );

    // The crash route is the reason the direct edge is absent: a session that
    // could drop straight to OFFLINE would skip the teardown a leave runs.
    CHECK_FALSE(
        NetwSessionCore::edge_is_legal(
            NetwSessionCore::STATE_ONLINE,
            NetwSessionCore::STATE_OFFLINE
        )
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] S2 a refused edge leaves the machine where "
    "it was and announces nothing"
) {
    Ref<NetwSessionCore> core = fresh();
    Recorder recorder(
        core.ptr(),
        {"state_changed", "session_entered", "session_ended"}
    );
    core->transition(NetwSessionCore::STATE_CONNECTING);
    core->transition(NetwSessionCore::STATE_ONLINE);
    recorder.clear();

    // The refusal writes an error by design, and this is the one place the
    // corpus expects one, so the printer is muted around exactly this call.
    ERR_PRINT_OFF;
    core->transition(NetwSessionCore::STATE_OFFLINE);
    ERR_PRINT_ON;

    NETW_CHECK_EQ(int(core->get_state()), int(NetwSessionCore::STATE_ONLINE));
    NETW_CHECK_EQ(recorder.order().size(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] S3 the flood window expires, which is the "
    "half no wall clock could prove"
) {
    Ref<NetwSessionCore> core = fresh();
    CHECK(spend_budget(core, 7, NOW));

    // Still inside the window, so the refusal stands.
    CHECK(core->join_flooded(7, NOW + WINDOW - 1));

    // Past it, so every stamp the budget was spent on has aged out and the
    // peer is admitted again on its own merits.
    CHECK_FALSE(core->join_flooded(7, NOW + WINDOW + 1));
}

TEST_CASE(
    "[Networked][Session][Hosted] S4 the window slides rather than resetting"
) {
    Ref<NetwSessionCore> core = fresh();
    // One frame every 200 ms is five per second, under a budget of eight, so a
    // peer submitting steadily forever is never refused.
    bool refused = false;
    for (int step = 0; step < 60; ++step) {
        refused = refused || core->join_flooded(7, NOW + int64_t(step) * 200);
    }
    CHECK_FALSE(refused);
}

TEST_CASE(
    "[Networked][Session][Hosted] S5 an idle peer is pruned and a busy one "
    "survives the prune"
) {
    Ref<NetwSessionCore> core = fresh();
    // Every peer here is idle by the time the table outgrows its cap, so the
    // prune has something to drop. Peer ids start at 2, because 1 is the host
    // and is never tracked at all.
    for (int peer = 2; peer < TRACKED_PEERS + 4; ++peer) {
        core->join_flooded(peer, NOW);
    }
    CHECK(spend_budget(core, 3, NOW + 5 * WINDOW));

    // The prune only runs past the cap, and it must never drop the peer whose
    // frames are what triggered it.
    for (int peer = 2; peer < TRACKED_PEERS + 4; ++peer) {
        core->join_flooded(peer, NOW + 5 * WINDOW);
    }
    CHECK(core->join_flooded(3, NOW + 5 * WINDOW));
}

TEST_CASE(
    "[Networked][Session][Hosted] S6 the assignment edge decides from the peer "
    "facts alone"
) {
    // A live server peer reaches ONLINE at the edge, because nothing is coming
    // to finish it.
    Ref<NetwSessionCore> server = fresh();
    server->on_peer_assigned(true, false, 1);
    NETW_CHECK_EQ(int(server->get_state()), int(NetwSessionCore::STATE_ONLINE));

    // A client peer is mid-handshake and holds.
    Ref<NetwSessionCore> client = fresh();
    client->on_peer_assigned(true, false, 7);
    NETW_CHECK_EQ(
        int(client->get_state()),
        int(NetwSessionCore::STATE_CONNECTING)
    );
    client->on_peer_assigned(true, true, 7);
    NETW_CHECK_EQ(int(client->get_state()), int(NetwSessionCore::STATE_ONLINE));
    NETW_CHECK_EQ(int(client->get_role()), int(NetwSessionCore::ROLE_CLIENT));

    // A null assignment cancels a connect and leaves a live session alone.
    Ref<NetwSessionCore> cancelled = fresh();
    cancelled->on_peer_assigned(true, false, 7);
    cancelled->on_peer_assigned(false, false, 0);
    NETW_CHECK_EQ(
        int(cancelled->get_state()),
        int(NetwSessionCore::STATE_OFFLINE)
    );
    client->on_peer_assigned(false, false, 0);
    NETW_CHECK_EQ(int(client->get_state()), int(NetwSessionCore::STATE_ONLINE));
}

TEST_CASE(
    "[Networked][Session][Hosted] S7 the hint splits a server and cannot make "
    "a client one"
) {
    Ref<NetwSessionCore> listen = fresh();
    listen->set_desired_role(NetwSessionCore::ROLE_LISTEN_SERVER);
    listen->resolve_online(1);
    NETW_CHECK_EQ(
        int(listen->get_role()),
        int(NetwSessionCore::ROLE_LISTEN_SERVER)
    );
    CHECK(listen->is_server_role());

    for (const NetwSessionCore::Role hint : {
             NetwSessionCore::ROLE_NONE,
             NetwSessionCore::ROLE_CLIENT,
             NetwSessionCore::ROLE_DEDICATED_SERVER,
         }) {
        Ref<NetwSessionCore> dedicated = fresh();
        dedicated->set_desired_role(hint);
        dedicated->resolve_online(1);
        NETW_CHECK_EQ(
            int(dedicated->get_role()),
            int(NetwSessionCore::ROLE_DEDICATED_SERVER)
        );
        CHECK(dedicated->is_server_role());

        // Peer identity decides authority, so no hint promotes a client.
        Ref<NetwSessionCore> remote = fresh();
        remote->set_desired_role(hint);
        remote->resolve_online(7);
        NETW_CHECK_EQ(
            int(remote->get_role()),
            int(NetwSessionCore::ROLE_CLIENT)
        );
        CHECK_FALSE(remote->is_server_role());
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] S8 clear returns the machine to a fresh one "
    "without announcing an edge"
) {
    Ref<NetwSessionCore> core = fresh();
    core->on_peer_assigned(true, false, 1);
    core->set_advertised_max_players(16);
    CHECK(spend_budget(core, 7, NOW));

    Recorder recorder(
        core.ptr(),
        {"state_changed", "session_entered", "session_ended"}
    );
    core->clear();

    NETW_CHECK_EQ(int(core->get_state()), int(NetwSessionCore::STATE_OFFLINE));
    NETW_CHECK_EQ(int(core->get_role()), int(NetwSessionCore::ROLE_NONE));
    NETW_CHECK_EQ(core->get_advertised_max_players(), 0);
    NETW_CHECK_EQ(recorder.order().size(), 0);
    // The flood window went with it, so a cleared session does not hold a
    // returning peer to the budget its previous session spent.
    CHECK_FALSE(core->join_flooded(7, NOW));
}

TEST_CASE(
    "[Networked][Session][Hosted] S9 the app tag is a fold of the whole string "
    "and zero means no gate"
) {
    NETW_CHECK_EQ(int(NetwSessionCore::compute_app_tag(StringName(""))), 0);
    // Two builds one character apart must not answer the same tag, which is
    // the only property the gate actually needs.
    CHECK(
        bool(
            NetwSessionCore::compute_app_tag(StringName("networked"))
            != NetwSessionCore::compute_app_tag(StringName("networkee"))
        )
    );
    // The mask is 32 bits wide and the fold has to reach all of it, so every
    // tag stays inside the mask and never wraps negative. Whether one chosen
    // string sets the top bit is luck rather than law, so the reach is proven
    // over a spread and each tag is checked for the range it must hold.
    int64_t reached = 0;
    for (int index = 0; index < 64; ++index) {
        char name[32];
        snprintf(name, sizeof(name), "%d-networked", index);
        const int64_t tag = NetwSessionCore::compute_app_tag(StringName(name));
        CHECK(bool(tag >= 0));
        CHECK(bool(tag <= int64_t(0xFFFFFFFF)));
        reached |= tag;
    }
    CHECK(bool(reached > int64_t(1) << 31));
}

} // namespace TestNetwSessionCore

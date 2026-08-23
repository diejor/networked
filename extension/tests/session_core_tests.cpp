#include "support/netw_test.h"

#include <cstdio>

#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_core.hpp"
#include "support/netw_recorder.h"

namespace TestNetwSessionCore {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SessionCore;
using netw_test::Recorder;

constexpr int64_t NOW = 1'000'000;
constexpr int64_t WINDOW = 1000;
constexpr int LIMIT = 8;
constexpr int TRACKED_PEERS = 64;

SessionCore fresh() {
    return SessionCore();
}

bool spend_budget(SessionCore &p_core, int p_peer, int64_t p_now) {
    for (int attempt = 0; attempt < LIMIT; ++attempt) {
        if (p_core.join_flooded(p_peer, p_now)) {
            return true;
        }
    }
    return p_core.join_flooded(p_peer, p_now);
}

TEST_CASE(
    "[Networked][Session][Hosted] S1 the table admits five edges and refuses "
    "the other eleven"
) {
    const SessionCore::State all[] = {
        SessionCore::STATE_OFFLINE,
        SessionCore::STATE_CONNECTING,
        SessionCore::STATE_ONLINE,
        SessionCore::STATE_DISCONNECTING,
    };
    int admitted = 0;
    for (const SessionCore::State from : all) {
        for (const SessionCore::State to : all) {
            admitted += SessionCore::edge_is_legal(from, to) ? 1 : 0;
        }
    }
    NETW_CHECK_EQ(admitted, 5);

    CHECK(
        SessionCore::edge_is_legal(
            SessionCore::STATE_OFFLINE,
            SessionCore::STATE_CONNECTING
        )
    );
    CHECK(
        SessionCore::edge_is_legal(
            SessionCore::STATE_CONNECTING,
            SessionCore::STATE_ONLINE
        )
    );
    CHECK(
        SessionCore::edge_is_legal(
            SessionCore::STATE_CONNECTING,
            SessionCore::STATE_OFFLINE
        )
    );
    CHECK(
        SessionCore::edge_is_legal(
            SessionCore::STATE_ONLINE,
            SessionCore::STATE_DISCONNECTING
        )
    );
    CHECK(
        SessionCore::edge_is_legal(
            SessionCore::STATE_DISCONNECTING,
            SessionCore::STATE_OFFLINE
        )
    );

    CHECK_FALSE(
        SessionCore::edge_is_legal(
            SessionCore::STATE_ONLINE,
            SessionCore::STATE_OFFLINE
        )
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] S2 a refused edge leaves the machine where "
    "it was and announces nothing"
) {
    Ref<NetwMultiplayerCore> host;
    host.instantiate();
    SessionCore &core = host->session_plane();
    Recorder recorder(
        host.ptr(),
        {"state_changed", "session_entered", "session_ended"}
    );
    core.transition(SessionCore::STATE_CONNECTING);
    core.transition(SessionCore::STATE_ONLINE);
    recorder.clear();

    ERR_PRINT_OFF;
    core.transition(SessionCore::STATE_OFFLINE);
    ERR_PRINT_ON;

    NETW_CHECK_EQ(int(core.get_state()), int(SessionCore::STATE_ONLINE));
    NETW_CHECK_EQ(recorder.order().size(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] S3 the flood window expires, which is the "
    "half no wall clock could prove"
) {
    SessionCore core = fresh();
    CHECK(spend_budget(core, 7, NOW));

    CHECK(core.join_flooded(7, NOW + WINDOW - 1));

    CHECK_FALSE(core.join_flooded(7, NOW + WINDOW + 1));
}

TEST_CASE(
    "[Networked][Session][Hosted] S4 the window slides rather than resetting"
) {
    SessionCore core = fresh();
    bool refused = false;
    for (int step = 0; step < 60; ++step) {
        refused = refused || core.join_flooded(7, NOW + int64_t(step) * 200);
    }
    CHECK_FALSE(refused);
}

TEST_CASE(
    "[Networked][Session][Hosted] S5 an idle peer is pruned and a busy one "
    "survives the prune"
) {
    SessionCore core = fresh();
    for (int peer = 2; peer < TRACKED_PEERS + 4; ++peer) {
        core.join_flooded(peer, NOW);
    }
    CHECK(spend_budget(core, 3, NOW + 5 * WINDOW));

    for (int peer = 2; peer < TRACKED_PEERS + 4; ++peer) {
        core.join_flooded(peer, NOW + 5 * WINDOW);
    }
    CHECK(core.join_flooded(3, NOW + 5 * WINDOW));
}

TEST_CASE(
    "[Networked][Session][Hosted] S6 the assignment edge decides from the peer "
    "facts alone"
) {
    SessionCore server = fresh();
    server.on_peer_assigned(true, false, 1);
    NETW_CHECK_EQ(int(server.get_state()), int(SessionCore::STATE_ONLINE));

    SessionCore client = fresh();
    client.on_peer_assigned(true, false, 7);
    NETW_CHECK_EQ(
        int(client.get_state()),
        int(SessionCore::STATE_CONNECTING)
    );
    client.on_peer_assigned(true, true, 7);
    NETW_CHECK_EQ(int(client.get_state()), int(SessionCore::STATE_ONLINE));
    NETW_CHECK_EQ(int(client.get_role()), int(SessionCore::ROLE_CLIENT));

    SessionCore cancelled = fresh();
    cancelled.on_peer_assigned(true, false, 7);
    cancelled.on_peer_assigned(false, false, 0);
    NETW_CHECK_EQ(
        int(cancelled.get_state()),
        int(SessionCore::STATE_OFFLINE)
    );
    client.on_peer_assigned(false, false, 0);
    NETW_CHECK_EQ(int(client.get_state()), int(SessionCore::STATE_ONLINE));
}

TEST_CASE(
    "[Networked][Session][Hosted] S7 the hint splits a server and cannot make "
    "a client one"
) {
    SessionCore listen = fresh();
    listen.set_desired_role(SessionCore::ROLE_LISTEN_SERVER);
    listen.resolve_online(1);
    NETW_CHECK_EQ(
        int(listen.get_role()),
        int(SessionCore::ROLE_LISTEN_SERVER)
    );
    CHECK(listen.is_server_role());

    for (const SessionCore::Role hint : {
             SessionCore::ROLE_NONE,
             SessionCore::ROLE_CLIENT,
             SessionCore::ROLE_DEDICATED_SERVER,
         }) {
        SessionCore dedicated = fresh();
        dedicated.set_desired_role(hint);
        dedicated.resolve_online(1);
        NETW_CHECK_EQ(
            int(dedicated.get_role()),
            int(SessionCore::ROLE_DEDICATED_SERVER)
        );
        CHECK(dedicated.is_server_role());

        SessionCore remote = fresh();
        remote.set_desired_role(hint);
        remote.resolve_online(7);
        NETW_CHECK_EQ(
            int(remote.get_role()),
            int(SessionCore::ROLE_CLIENT)
        );
        CHECK_FALSE(remote.is_server_role());
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] S8 clear returns the machine to a fresh one "
    "without announcing an edge"
) {
    Ref<NetwMultiplayerCore> host;
    host.instantiate();
    SessionCore &core = host->session_plane();
    core.on_peer_assigned(true, false, 1);
    core.set_advertised_max_players(16);
    CHECK(spend_budget(core, 7, NOW));

    Recorder recorder(
        host.ptr(),
        {"state_changed", "session_entered", "session_ended"}
    );
    core.clear();

    NETW_CHECK_EQ(int(core.get_state()), int(SessionCore::STATE_OFFLINE));
    NETW_CHECK_EQ(int(core.get_role()), int(SessionCore::ROLE_NONE));
    NETW_CHECK_EQ(core.get_advertised_max_players(), 0);
    NETW_CHECK_EQ(recorder.order().size(), 0);
    CHECK_FALSE(core.join_flooded(7, NOW));
}

TEST_CASE(
    "[Networked][Session][Hosted] S9 the app tag is a fold of the whole string "
    "and zero means no gate"
) {
    NETW_CHECK_EQ(int(SessionCore::compute_app_tag(StringName(""))), 0);
    CHECK(
        bool(
            SessionCore::compute_app_tag(StringName("networked"))
            != SessionCore::compute_app_tag(StringName("networkee"))
        )
    );
    int64_t reached = 0;
    for (int index = 0; index < 64; ++index) {
        char name[32];
        snprintf(name, sizeof(name), "%d-networked", index);
        const int64_t tag = SessionCore::compute_app_tag(StringName(name));
        CHECK(bool(tag >= 0));
        CHECK(bool(tag <= int64_t(0xFFFFFFFF)));
        reached |= tag;
    }
    CHECK(bool(reached > int64_t(1) << 31));
}

} // namespace TestNetwSessionCore

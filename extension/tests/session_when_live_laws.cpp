#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"

namespace TestNetwSessionWhenLive {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

bool differ(int64_t p_a, int64_t p_b) {
    return p_a != p_b;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 every route watch is parked as one "
    "pending row, whether or not the caller named a timeout"
) {
    Ref<NetwMultiplayer> session = hosting();

    NETW_CHECK_EQ(session->liveness_pending_live_count(), 0);
    session->liveness_when_live(4, Callable(), 0, Callable());
    NETW_CHECK_EQ(session->liveness_pending_live_count(), 1);

    SUBCASE("a second route parks a row of its own") {
        session->liveness_when_live(5, Callable(), 3, Callable());
        NETW_CHECK_EQ(session->liveness_pending_live_count(), 2);
    }

    SUBCASE("two watches on one route are two rows, not one") {
        session->liveness_when_live(4, Callable(), 3, Callable());
        NETW_CHECK_EQ(session->liveness_pending_live_count(), 2);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 a wait asked for no timeout waits a "
    "whole tickrate, so a caller that named no deadline is not expired by "
    "the very next poll"
) {
    Ref<NetwMultiplayer> session = hosting();
    REQUIRE_FALSE(session->clock_is_configured());

    session->liveness_when_live(7, Callable(), 0, Callable());
    NETW_CHECK_EQ(session->liveness_pending_live_count(), 1);
    for (int step = 0; step < 8; step++) {
        session->liveness_poll(0);
    }
    NETW_CHECK_EQ(session->liveness_pending_live_count(), 1);

    SUBCASE("while a deadline the caller DID name expires on time") {
        Ref<NetwMultiplayer> named = hosting();
        named->liveness_when_live(7, Callable(), 2, Callable());
        NETW_CHECK_EQ(named->liveness_pending_live_count(), 1);
        for (int step = 0; step < 3; step++) {
            named->liveness_poll(0);
        }
        NETW_CHECK_EQ(named->liveness_pending_live_count(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 flushing standalone acks with no peer "
    "installed sends nothing rather than reaching through a null inner"
) {
    Ref<NetwMultiplayer> session = hosting();

    const int64_t before = session->get_standalone_acks_out();
    session->flush_standalone_acks();
    NETW_CHECK_EQ(session->get_standalone_acks_out(), before);
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 moving an entity between scenes is "
    "server authority's, and a client is answered a rejected promise"
) {
    Ref<NetwMultiplayer> client;
    client.instantiate();
    client->session_set_role(NetwMultiplayer::ROLE_CLIENT);

    const Ref<NetwPromise> answered
        = client->scene_move(RID(), RID(), Ref<netw::NetwReparentOpts>());
    CHECK(answered.is_valid());
    CHECK(answered->get_is_failed());
    NETW_CHECK_EQ(answered->get_code(), ERR_UNAUTHORIZED);
}

} // namespace TestNetwSessionWhenLive

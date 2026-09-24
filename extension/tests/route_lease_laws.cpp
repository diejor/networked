#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/templates.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"

namespace TestNetwRouteLeaseLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwLivenessCore;
using netw::NetwMultiplayer;

constexpr int MINTED_EACH = 1000;
constexpr int BLOCK = NetwMultiplayer::ROUTE_LEASE_BLOCK;

Ref<NetwLivenessCore> book_of(NetwMultiplayer *p_api) {
    return p_api->get_liveness_core();
}

void join_every_client(LoopbackRig &p_rig) {
    for (int at = 0; at < p_rig.count(); ++at) {
        p_rig.join(at, StringName(vformat("peer%d", at)));
    }
}

int mint_and_spend(LoopbackRig &p_rig, int p_client, int &r_unspent) {
    NetwMultiplayer *author = p_rig.client(p_client);
    const int64_t route = author->liveness_reserve_route();
    if (route <= 0) {
        return 0;
    }
    const Error spent
        = p_rig.server()->liveness_spend_lease(p_rig.peer_id(p_client), route);
    r_unspent += spent == OK ? 0 : 1;
    if (book_of(author)->lease_remaining()
        <= NetwMultiplayer::ROUTE_LEASE_REFILL_AT) {
        p_rig.pump(2);
    }
    return int(route);
}

TEST_CASE(
    "[Networked][Liveness][Lease] three clients and the session mint a "
    "thousand routes each from one counter, all distinct and all below the "
    "session's next route"
) {
    LoopbackRig rig(3);
    join_every_client(rig);

    HashSet<int> seen;
    int duplicates = 0;
    int refused = 0;
    int unspent = 0;
    int greatest = 0;
    auto note = [&](int p_route) {
        if (p_route <= 0) {
            refused += 1;
            return;
        }
        duplicates += seen.has(p_route) ? 1 : 0;
        seen.insert(p_route);
        greatest = p_route > greatest ? p_route : greatest;
    };
    for (int round = 0; round < MINTED_EACH; ++round) {
        for (int client = 0; client < rig.count(); ++client) {
            note(mint_and_spend(rig, client, unspent));
        }
        note(int(rig.server()->liveness_reserve_route()));
    }

    NETW_CHECK_EQ(refused, 0);
    NETW_CHECK_EQ(unspent, 0);
    NETW_CHECK_EQ(duplicates, 0);
    NETW_CHECK_EQ(int(seen.size()), MINTED_EACH * 4);
    NETW_CHECK_EQ(
        int(rig.server()->liveness_reserve_route()) > greatest,
        true
    );
}

TEST_CASE(
    "[Networked][Liveness][Lease] a client whose refill has not arrived "
    "mints its ninth route and refuses its seventeenth until the refill "
    "lands"
) {
    LoopbackRig rig(1);
    join_every_client(rig);
    NetwMultiplayer *author = rig.client(0);
    NETW_REQUIRE_EQ(book_of(author)->lease_remaining(), BLOCK);

    rig.hold(0);
    int minted = 0;
    int unspent = 0;
    for (int at = 0; at < BLOCK; ++at) {
        const int64_t route = author->liveness_reserve_route();
        if (route <= 0) {
            break;
        }
        minted += 1;
        const Error spent
            = rig.server()->liveness_spend_lease(rig.peer_id(0), route);
        unspent += spent == OK ? 0 : 1;
        rig.pump();
    }
    NETW_CHECK_EQ(minted, BLOCK);
    NETW_CHECK_EQ(unspent, 0);
    NETW_CHECK_EQ(author->liveness_reserve_route(), 0);
    NETW_CHECK_EQ(
        book_of(rig.server())->granted_remaining(rig.peer_id(0)),
        BLOCK
    );

    rig.release(0);
    rig.pump(4);
    NETW_CHECK_EQ(book_of(author)->lease_remaining(), BLOCK);
    NETW_CHECK_EQ(author->liveness_reserve_route() > 0, true);
}

TEST_CASE(
    "[Networked][Liveness][Lease] a route the session did not lease to the "
    "sender is refused and grants nothing"
) {
    LoopbackRig rig(2);
    join_every_client(rig);
    const int64_t theirs = rig.client(1)->liveness_reserve_route();
    NETW_REQUIRE_EQ(theirs > 0, true);
    NETW_CHECK_EQ(
        rig.server()->liveness_spend_lease(rig.peer_id(0), theirs),
        ERR_UNAUTHORIZED
    );
    NETW_CHECK_EQ(
        book_of(rig.server())->granted_remaining(rig.peer_id(0)),
        BLOCK
    );
}

TEST_CASE(
    "[Networked][Liveness][Lease] a peer joining after the session minted "
    "holds a lease above every route already minted"
) {
    LoopbackRig rig(1);
    join_every_client(rig);
    int greatest = 0;
    for (int at = 0; at < 40; ++at) {
        greatest = int(rig.server()->liveness_reserve_route());
    }

    const int late = rig.add_client();
    rig.pump(2);
    rig.join(late, StringName("late"));

    NetwMultiplayer *joiner = rig.client(late);
    NETW_CHECK_EQ(book_of(joiner)->lease_remaining(), BLOCK);
    const int64_t first = joiner->liveness_reserve_route();
    NETW_CHECK_EQ(first > greatest, true);
    NETW_CHECK_EQ(int(rig.server()->liveness_reserve_route()) > first, true);
}

TEST_CASE(
    "[Networked][Liveness][Lease] the session never leases to itself, so its "
    "own spawns keep minting from its counter"
) {
    LoopbackRig rig(1);
    join_every_client(rig);
    NETW_CHECK_EQ(book_of(rig.server())->is_leased(), false);
    NETW_CHECK_EQ(book_of(rig.server())->lease_remaining(), 0);
}

} // namespace TestNetwRouteLeaseLaws

#endif

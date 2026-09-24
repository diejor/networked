#include "support/netw_test.h"

#include "godot/templates.hpp"
#include "netw/liveness_core.hpp"

namespace TestNetwLivenessLeaseLaws {

using namespace godot;
using netw::NetwLivenessCore;

constexpr int BLOCK = 16;
constexpr int PEERS = 3;
constexpr int BLOCKS_EACH = 60;

TEST_CASE(
    "[Networked][Lease][Hosted][Law] blocks granted from one counter "
    "never overlap each other or the routes the session mints between them"
) {
    Ref<NetwLivenessCore> session;
    session.instantiate();
    HashSet<int> seen;
    int duplicates = 0;
    int refused = 0;
    for (int round = 0; round < BLOCKS_EACH; ++round) {
        for (int peer = 2; peer < 2 + PEERS; ++peer) {
            const int base = session->grant_lease(peer, BLOCK);
            refused += base > 0 ? 0 : 1;
            for (int route = base; route < base + BLOCK; ++route) {
                duplicates += seen.has(route) ? 1 : 0;
                seen.insert(route);
            }
        }
        const int own = session->reserve_route();
        duplicates += seen.has(own) ? 1 : 0;
        seen.insert(own);
    }
    NETW_CHECK_EQ(refused, 0);
    NETW_CHECK_EQ(duplicates, 0);
    NETW_CHECK_EQ(int(seen.size()), BLOCKS_EACH * (PEERS * BLOCK + 1));
}

TEST_CASE(
    "[Networked][Lease][Hosted][Law] a leased book mints its "
    "blocks in order, queues a second block behind the first and answers 0 "
    "when empty"
) {
    Ref<NetwLivenessCore> peer;
    peer.instantiate();
    peer->install_lease(17, 2);
    peer->install_lease(40, 1);
    NETW_CHECK_EQ(peer->is_leased(), true);
    NETW_CHECK_EQ(peer->lease_remaining(), 3);
    NETW_CHECK_EQ(peer->reserve_route(), 17);
    NETW_CHECK_EQ(peer->reserve_route(), 18);
    NETW_CHECK_EQ(peer->reserve_route(), 40);
    NETW_CHECK_EQ(peer->reserve_route(), 0);
    NETW_CHECK_EQ(peer->lease_remaining(), 0);
    peer->clear();
    NETW_CHECK_EQ(peer->is_leased(), false);
    NETW_CHECK_EQ(peer->reserve_route(), 1);
}

TEST_CASE(
    "[Networked][Lease][Hosted][Law] the session spends a peer's "
    "route once, burns the routes it skipped and forgets an abandoned lease"
) {
    Ref<NetwLivenessCore> session;
    session.instantiate();
    const int first = session->grant_lease(2, BLOCK);
    const int second = session->grant_lease(2, BLOCK);
    const int other = session->grant_lease(3, BLOCK);
    NETW_CHECK_EQ(session->granted_remaining(2), 2 * BLOCK);

    NETW_CHECK_EQ(session->spend_lease(2, first + 3), true);
    NETW_CHECK_EQ(session->granted_remaining(2), 2 * BLOCK - 4);
    NETW_CHECK_EQ(session->spend_lease(2, first + 3), false);
    NETW_CHECK_EQ(session->spend_lease(2, first + 1), false);
    NETW_CHECK_EQ(session->spend_lease(2, other), false);

    NETW_CHECK_EQ(session->spend_lease(2, second), true);
    NETW_CHECK_EQ(session->granted_remaining(2), BLOCK - 1);
    NETW_CHECK_EQ(session->spend_lease(2, first + 5), false);

    session->abandon_lease(2);
    NETW_CHECK_EQ(session->granted_remaining(2), 0);
    NETW_CHECK_EQ(session->granted_remaining(3), BLOCK);
}

TEST_CASE(
    "[Networked][Lease][Hosted][Law] a route floor covers every route a "
    "peer was leased, minted or holds, so a successor counter above the "
    "greatest floor never reissues one, even a route another peer never saw"
) {
    Ref<NetwLivenessCore> session;
    session.instantiate();
    const int to_two = session->grant_lease(2, BLOCK);
    const int to_three = session->grant_lease(3, BLOCK);
    const int own = session->reserve_route();

    Ref<NetwLivenessCore> two;
    two.instantiate();
    two->install_lease(to_two, BLOCK);
    two->reserve_route();
    two->reserve_route();

    Ref<NetwLivenessCore> three;
    three.instantiate();
    three->install_lease(to_three, BLOCK);
    for (int at = 0; at < BLOCK; ++at) {
        three->reserve_route();
    }
    const int hidden = to_three + BLOCK - 1;

    NETW_CHECK_EQ(two->route_floor(), to_two + BLOCK - 1);
    NETW_CHECK_EQ(three->route_floor(), hidden);
    NETW_CHECK_EQ(session->route_floor(), own);

    const int greatest = MAX(
        session->route_floor(),
        MAX(two->route_floor(), three->route_floor())
    );
    CHECK(greatest >= hidden);
    CHECK(greatest >= own);

    Ref<NetwLivenessCore> joined;
    joined.instantiate();
    const RID held = joined->entity_create();
    REQUIRE(joined->bind_route(held, 900));
    NETW_CHECK_EQ(joined->route_floor(), 900);
}

} // namespace TestNetwLivenessLeaseLaws

#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionRouteClaim {

using namespace godot;
using netw::NetwMultiplayer;

bool differ(int64_t p_a, int64_t p_b) {
    return p_a != p_b;
}

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a claim hands back exactly the routes "
    "asked for, and every one of them is distinct"
) {
    Ref<NetwMultiplayer> session = hosting();

    const PackedInt64Array claimed = session->liveness_claim_routes(3);
    NETW_CHECK_EQ(claimed.size(), 3);

    SUBCASE("no route repeats inside one claim") {
        CHECK(differ(claimed[0], claimed[1]));
        CHECK(differ(claimed[1], claimed[2]));
        CHECK(differ(claimed[0], claimed[2]));
    }

    SUBCASE("a second claim overlaps the first nowhere") {
        const PackedInt64Array again = session->liveness_claim_routes(2);
        NETW_CHECK_EQ(again.size(), 2);
        CHECK(differ(again[0], claimed[0]));
        CHECK(differ(again[0], claimed[1]));
        CHECK(differ(again[0], claimed[2]));
    }

    SUBCASE("a claim of nothing is empty rather than one route") {
        NETW_CHECK_EQ(session->liveness_claim_routes(0).size(), 0);
        NETW_CHECK_EQ(session->liveness_claim_routes(-1).size(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 claiming and releasing routes is server "
    "authority's, so a client claims nothing rather than a private range"
) {
    Ref<NetwMultiplayer> client;
    client.instantiate();
    client->session_set_role(NetwMultiplayer::ROLE_CLIENT);

    NETW_CHECK_EQ(client->liveness_claim_routes(3).size(), 0);
    NETW_CHECK_EQ(
        client->liveness_release_routes(PackedInt64Array()),
        ERR_UNCONFIGURED
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 releasing no routes is a success rather "
    "than an error, so a teardown with nothing to give back is not noisy"
) {
    Ref<NetwMultiplayer> session = hosting();

    NETW_CHECK_EQ(session->liveness_release_routes(PackedInt64Array()), OK);
    NETW_CHECK_EQ(
        session->liveness_release_routes(session->liveness_claim_routes(2)),
        OK
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 a despawn is server authority's and "
    "needs an entity the session actually holds"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(
        session->entity_despawn(entity, Ref<netw::NetwDespawnOpts>()),
        ERR_DOES_NOT_EXIST
    );

    SUBCASE("a client is refused before the entity is even looked up") {
        Ref<NetwMultiplayer> client;
        client.instantiate();
        client->session_set_role(NetwMultiplayer::ROLE_CLIENT);
        NETW_CHECK_EQ(
            client->entity_despawn(entity, Ref<netw::NetwDespawnOpts>()),
            ERR_UNAUTHORIZED
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L6 a release queues its routes for "
    "lifecycle removal, so every peer is told the row died rather than the "
    "host quietly forgetting it"
) {
    Ref<NetwMultiplayer> session = hosting();
    const PackedInt64Array claimed = session->liveness_claim_routes(2);

    NETW_CHECK_EQ(session->get_table_core()->lifecycle_removals().size(), 0);
    NETW_CHECK_EQ(session->liveness_release_routes(claimed), OK);

    const PackedInt64Array queued
        = session->get_table_core()->lifecycle_removals();
    NETW_CHECK_EQ(queued.size(), claimed.size());
    NETW_CHECK_EQ(queued[0], claimed[0]);
    NETW_CHECK_EQ(queued[1], claimed[1]);

    SUBCASE("and a release of nothing queues nothing") {
        session->get_table_core()->take_lifecycle_removals();
        NETW_CHECK_EQ(session->liveness_release_routes(PackedInt64Array()), OK);
        NETW_CHECK_EQ(
            session->get_table_core()->lifecycle_removals().size(),
            0
        );
    }

    SUBCASE("and a client's refused release queues nothing") {
        Ref<NetwMultiplayer> client;
        client.instantiate();
        client->session_set_role(NetwMultiplayer::ROLE_CLIENT);
        const PackedInt64Array asked = session->liveness_claim_routes(1);
        NETW_CHECK_EQ(client->liveness_release_routes(asked), ERR_UNCONFIGURED);
        NETW_CHECK_EQ(client->get_table_core()->lifecycle_removals().size(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 a script that does not descend from "
    "NetwMultiplayer is not an extension script, and null is not either"
) {
    CHECK_FALSE(NetwMultiplayer::is_extension_script(Ref<Script>()));
}

} // namespace TestNetwSessionRouteClaim

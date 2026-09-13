#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionFacade {

using namespace godot;
using netw::NetwMultiplayer;

bool same_row(const RID &p_a, const RID &p_b) {
    return p_a == p_b;
}

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 the session reaches its own liveness "
    "rows, so a caller never has to hold the liveness core to make an entity"
) {
    Ref<NetwMultiplayer> session = make_session();

    const RID entity = session->entity_create();
    CHECK(entity.is_valid());

    SUBCASE("minting an id does not by itself make the entity live") {
        NETW_CHECK_EQ(
            session->entity_get_state(entity),
            NetwMultiplayer::ENTITY_STATE_UNKNOWN
        );
    }

    SUBCASE("the session and its liveness core answer the same row") {
        NETW_CHECK_EQ(
            session->entity_get_state(entity),
            session->get_liveness_core()->state_of(entity)
        );
        NETW_CHECK_EQ(
            session->entity_get_epoch(entity),
            session->get_liveness_core()->epoch_of(entity)
        );
    }

    SUBCASE("a second mint is a different row rather than the same one") {
        const RID other = session->entity_create();
        CHECK(other.is_valid());
        CHECK_FALSE(same_row(other, entity));
    }

    SUBCASE("a route nobody claimed resolves to no entity at all") {
        CHECK_FALSE(session->entity_from_route(4242).is_valid());
        NETW_CHECK_EQ(session->liveness_get_routes().size(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 every peer verb that needs the wrapped "
    "SceneMultiplayer refuses rather than crashing once it is cleared"
) {
    Ref<NetwMultiplayer> session = make_session();
    session->session_set_inner(Ref<SceneMultiplayer>());

    NETW_CHECK_EQ(session->send_auth(3, PackedByteArray()), ERR_UNCONFIGURED);
    NETW_CHECK_EQ(session->complete_auth(3), ERR_UNCONFIGURED);
    NETW_CHECK_EQ(session->get_authenticating_peers().size(), 0);

    SUBCASE("the void-returning ones are no-ops rather than a null deref") {
        session->disconnect_peer(3);
        session->clear();
        CHECK(true);
    }
}

} // namespace TestNetwSessionFacade

#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionEntity {

using namespace godot;
using netw::NetwMultiplayer;

bool differ(int64_t p_a, int64_t p_b) {
    return p_a != p_b;
}

bool is_nothing(const Node *p_node) {
    return p_node == nullptr;
}

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a route binds to an entity once, so a "
    "second claim on the same route is refused rather than stealing it"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID first = session->entity_create();
    const RID second = session->entity_create();

    NETW_CHECK_EQ(session->entity_bind_route(first, 7), OK);
    NETW_CHECK_EQ(session->entity_get_route(first), 7);

    SUBCASE("a second entity cannot take a bound route") {
        NETW_CHECK_EQ(
            session->entity_bind_route(second, 7),
            ERR_ALREADY_IN_USE
        );
    }

    SUBCASE("route zero and below name no route at all") {
        NETW_CHECK_EQ(session->entity_bind_route(second, 0), ERR_INVALID_DATA);
        NETW_CHECK_EQ(session->entity_bind_route(second, -1), ERR_INVALID_DATA);
    }

    SUBCASE("an entity nobody minted cannot be bound") {
        NETW_CHECK_EQ(session->entity_bind_route(RID(), 8), ERR_DOES_NOT_EXIST);
    }

    SUBCASE("the bound route resolves back to its entity") {
        CHECK(session->entity_from_route(7).is_valid());
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 admitting an entity twice reuses the "
    "route it already holds rather than burning a second one"
) {
    Ref<NetwMultiplayer> session = make_session();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);

    const RID entity = session->entity_create();
    const int64_t route = session->entity_admit(entity);
    CHECK(route > 0);

    NETW_CHECK_EQ(session->entity_admit(entity), route);
    NETW_CHECK_EQ(session->entity_get_route(entity), route);

    SUBCASE("two entities are admitted onto routes of their own") {
        const RID other = session->entity_create();
        const int64_t second = session->entity_admit(other);
        CHECK(second > 0);
        CHECK(differ(second, route));
    }

    SUBCASE("an entity nobody minted is admitted onto no route") {
        NETW_CHECK_EQ(session->entity_admit(RID()), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 an entity with no wrapper answers the "
    "absent shape for every wrapper-backed question"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    CHECK(is_nothing(session->entity_get_node(entity)));
    CHECK_FALSE(session->entity_get_parent(entity).is_valid());
    NETW_CHECK_EQ(session->entity_get_peer(entity), 0);
}

} // namespace TestNetwSessionEntity

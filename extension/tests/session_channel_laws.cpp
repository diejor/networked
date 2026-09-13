#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionChannel {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a user channel send is refused outside "
    "the user channel range, so it can never collide with a kit channel"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();
    session->entity_bind_route(entity, 5);
    const PackedByteArray payload;

    NETW_CHECK_EQ(
        session->channel_send(1, entity, 99, payload, true),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(
        session->channel_send(1, entity, 255, payload, true),
        ERR_INVALID_DATA
    );

    SUBCASE("an entity with no route names nowhere to send") {
        const RID unbound = session->entity_create();
        NETW_CHECK_EQ(
            session->channel_send(1, unbound, 100, payload, true),
            ERR_DOES_NOT_EXIST
        );
    }

    SUBCASE("the range check runs before the route check") {
        const RID unbound = session->entity_create();
        NETW_CHECK_EQ(
            session->channel_send(1, unbound, 99, payload, true),
            ERR_INVALID_DATA
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a write policy admits nobody for an "
    "entity the session cannot resolve to a component node"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    CHECK_FALSE(session->sync_policy_admits(
        NetwMultiplayer::WRITE_POLICY_ANY_PEER,
        3,
        entity,
        0
    ));
    CHECK_FALSE(session->sync_policy_admits(
        NetwMultiplayer::WRITE_POLICY_ANY_PEER,
        3,
        RID(),
        0
    ));
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a write policy reads the node's own "
    "authority and the wrapper's controller, so the two halves of the author "
    "gate answer apart for one entity"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();
    NETW_CHECK_EQ(session->entity_bind_route(entity, 71), OK);

    Node *body = memnew(Node);
    body->set_name("AuthorEntity");
    NETW_CHECK_EQ(session->entity_bind_node(entity, body), OK);
    body->set_multiplayer_authority(4);

    const Ref<NetwEntity> wrapper = session->entity_get_view(entity);
    REQUIRE(wrapper.is_valid());
    wrapper->set_controller(5);

    CHECK(session->sync_policy_admits(
        NetwMultiplayer::WRITE_POLICY_AUTHORITY,
        4,
        entity,
        0
    ));
    CHECK_FALSE(session->sync_policy_admits(
        NetwMultiplayer::WRITE_POLICY_AUTHORITY,
        5,
        entity,
        0
    ));
    CHECK(session->sync_policy_admits(
        NetwMultiplayer::WRITE_POLICY_CONTROLLER,
        5,
        entity,
        0
    ));
    CHECK_FALSE(session->sync_policy_admits(
        NetwMultiplayer::WRITE_POLICY_CONTROLLER,
        4,
        entity,
        0
    ));

    memdelete(body);
}

} // namespace TestNetwSessionChannel

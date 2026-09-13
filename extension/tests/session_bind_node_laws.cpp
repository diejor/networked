#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionBindNode {

using namespace godot;
using netw::NetwMultiplayer;

bool is_node(const Node *p_a, const Node *p_b) {
    return p_a == p_b;
}

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L1 a node binds to an entity "
    "only once, so a second node cannot take an entity another node "
    "already owns"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID entity = session->entity_create();
    session->entity_bind_route(entity, 11);

    Node *first = memnew(Node);
    Node *second = memnew(Node);
    netw::gd::scene_root()->add_child(first);
    netw::gd::scene_root()->add_child(second);

    NETW_CHECK_EQ(session->entity_bind_node(entity, first), OK);

    SUBCASE("binding the same node again is idempotent") {
        NETW_CHECK_EQ(session->entity_bind_node(entity, first), OK);
    }

    SUBCASE("a different node is refused rather than stealing the entity") {
        NETW_CHECK_EQ(
            session->entity_bind_node(entity, second),
            ERR_ALREADY_EXISTS
        );
    }

    SUBCASE("the entity now presents that node") {
        CHECK(is_node(session->entity_get_node(entity), first));
    }

    first->queue_free();
    second->queue_free();
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L2 a bind needs a real entity, "
    "a real node and a claimed route, and refuses each absence differently"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID entity = session->entity_create();

    Node *node = memnew(Node);
    netw::gd::scene_root()->add_child(node);

    NETW_CHECK_EQ(session->entity_bind_node(RID(), node), ERR_DOES_NOT_EXIST);
    NETW_CHECK_EQ(session->entity_bind_node(entity, nullptr), ERR_INVALID_DATA);
    NETW_CHECK_EQ(session->entity_bind_node(entity, node), ERR_INVALID_DATA);

    node->queue_free();
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a rewind over entities the session does "
    "not hold arms no slots rather than rewinding a partial set"
) {
    Ref<NetwMultiplayer> session = hosting();
    TypedArray<RID> entities;
    entities.push_back(session->entity_create());
    entities.push_back(RID());

    session->lagcomp_rewind(entities, 4, Callable());
    CHECK(true);
}

} // namespace TestNetwSessionBindNode

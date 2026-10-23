#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestEntityTreeBookLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

constexpr int SERVER = -1;
constexpr int CLIENT = 0;
const char *ENTITY_META = "netw_entity";

Node *configured(const char *p_name, Ref<NetwEntity> &r_entity) {
    Node *made = memnew(Node);
    made->set_name(p_name);
    r_entity = NetwEntity::ensure(made);
    return made;
}

bool carries_meta(Node *p_node) {
    return p_node->has_meta(StringName(ENTITY_META));
}

bool found_as(Node *p_node, const Ref<NetwEntity> &p_entity) {
    return NetwEntity::of(p_node).ptr() == p_entity.ptr();
}

bool booked(NetwMultiplayer *p_session, Node *p_node) {
    return p_session->entity_book_holds_under_test(p_node);
}

bool alive(ObjectID p_id) {
    return netw::gd::object_of(p_id) != nullptr;
}

TEST_CASE(
    "[Networked][Entity][SceneTree] an entity in the tree carries no attach "
    "meta and is found through its session's book, and leaving the tree puts "
    "it back on the meta"
) {
    LoopbackRig rig(1);
    rig.mount();
    Ref<NetwEntity> entity;
    Node *node = configured("Booked", entity);
    CHECK(carries_meta(node));

    rig.branch(SERVER)->add_child(node);
    CHECK(!carries_meta(node));
    CHECK(booked(rig.server(), node));
    CHECK(found_as(node, entity));

    rig.branch(SERVER)->remove_child(node);
    CHECK(carries_meta(node));
    CHECK(!booked(rig.server(), node));
    CHECK(found_as(node, entity));
    memdelete(node);
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a node freed off the tree takes its "
    "entity with it once the departure settles and interest next runs"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *node = nullptr;
    ObjectID entity_id;
    {
        Ref<NetwEntity> entity;
        node = configured("Released", entity);
        entity_id = entity->get_instance_id();
    }
    rig.branch(SERVER)->add_child(node);
    node->set_owner(rig.branch(SERVER));
    rig.step_ticks(2);
    rig.branch(SERVER)->remove_child(node);
    rig.step_ticks(2);
    CHECK(alive(entity_id));

    memdelete(node);
    rig.step_ticks(1);
    rig.server()->interest_flush_now();
    CHECK(!alive(entity_id));
}

TEST_CASE(
    "[Networked][Entity][SceneTree] an entity entering the tree is booked by "
    "the session on its own branch, never by another session in the tree"
) {
    LoopbackRig rig(1);
    rig.mount();
    Ref<NetwEntity> hosted;
    Ref<NetwEntity> joined;
    Node *on_server = configured("OnServer", hosted);
    Node *on_client = configured("OnClient", joined);
    rig.branch(SERVER)->add_child(on_server);
    rig.branch(CLIENT)->add_child(on_client);

    CHECK(booked(rig.server(), on_server));
    CHECK(!booked(rig.client(CLIENT), on_server));
    CHECK(booked(rig.client(CLIENT), on_client));
    CHECK(!booked(rig.server(), on_client));

    rig.branch(SERVER)->remove_child(on_server);
    rig.branch(CLIENT)->remove_child(on_client);
    memdelete(on_server);
    memdelete(on_client);
}

TEST_CASE(
    "[Networked][Entity][SceneTree] a synchronizer added under a live entity "
    "whose walk found none is seen at once, with no re-entry"
) {
    LoopbackRig rig(1);
    rig.mount();
    Ref<NetwEntity> entity;
    Node *node = configured("Watched", entity);
    rig.branch(SERVER)->add_child(node);
    CHECK(entity->synchronizers().is_empty());

    MultiplayerSynchronizer *late = memnew(MultiplayerSynchronizer);
    late->set_root_path(NodePath(".."));
    node->add_child(late);
    late->set_owner(node);
    NETW_CHECK_EQ(int64_t(entity->synchronizers().size()), int64_t(1));

    rig.branch(SERVER)->remove_child(node);
    memdelete(node);
}

} // namespace TestEntityTreeBookLaws

#endif

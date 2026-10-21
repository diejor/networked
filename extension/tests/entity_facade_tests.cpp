#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwEntityFacade {

using namespace godot;
using netw::NetwEntity;

TEST_CASE(
    "[Networked][Entity][Hosted] F1 an identity is stamped onto the entity "
    "and the node keeps the name it was given"
) {
    Node *root = memnew(Node);
    root->set_name("Player");

    const Ref<NetwEntity> entity = NetwEntity::ensure(root);
    REQUIRE(entity.is_valid());
    entity->set_name("valeria");
    entity->set_peer_id(42);

    CHECK(root->get_name() == StringName("Player"));
    CHECK(entity->get_name() == StringName("valeria"));
    NETW_CHECK_EQ(entity->get_peer_id(), 42);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Entity][Hosted] F2 asking about a node with no record answers "
    "null, and minting one makes the same ask answer it"
) {
    Node *clean = memnew(Node);
    CHECK(NetwEntity::of(clean).is_null());
    CHECK(NetwEntity::of(nullptr).is_null());

    const Ref<NetwEntity> minted = NetwEntity::ensure(clean);
    REQUIRE(minted.is_valid());
    CHECK(NetwEntity::of(clean) == minted);
    CHECK(NetwEntity::ensure(clean) == minted);

    memdelete(clean);
}

TEST_CASE(
    "[Networked][Entity][Hosted] F3 a child inside an entity is part of it "
    "until it is given a record of its own"
) {
    Node *root = memnew(Node);
    Node *child = memnew(Node);
    root->add_child(child);
    const Ref<NetwEntity> owned = NetwEntity::ensure(root);
    REQUIRE(owned.is_valid());

    CHECK(NetwEntity::of(child) == owned);

    const Ref<NetwEntity> own = NetwEntity::ensure(child);
    REQUIRE(own.is_valid());
    CHECK(own != owned);
    CHECK(NetwEntity::of(child) == own);
    CHECK(NetwEntity::of(root) == owned);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Entity][Hosted] F4 resolving a detached branch provisions its "
    "outermost node, because that is the one an enclosing entity will find"
) {
    Node *parent = memnew(Node);
    Node *child = memnew(Node);
    Node *grandchild = memnew(Node);
    parent->add_child(child);
    child->add_child(grandchild);

    const Ref<NetwEntity> provisioned = NetwEntity::resolve(grandchild);
    REQUIRE(provisioned.is_valid());
    CHECK(NetwEntity::of(parent) == provisioned);
    CHECK(NetwEntity::of(grandchild) == provisioned);
    CHECK(NetwEntity::resolve(child) == provisioned);

    CHECK(NetwEntity::resolve(nullptr).is_null());

    memdelete(parent);
}

TEST_CASE(
    "[Networked][Entity][Hosted][SceneTree] IT3 a mounted child cannot "
    "declare its ancestor as its own entity root"
) {
    Node *root = memnew(Node);
    root->set_name("Root");
    Node *child = memnew(Node);
    child->set_name("Child");
    const Ref<NetwEntity> entity = netw::Netw::configure_entity(root);
    root->add_child(child);
    netw::gd::scene_root()->add_child(root);
    netw_test::FaultWatch faults;
    faults.expect_error(
        "[error][entity] Netw.configure_entity ran after 'Child' entered "
        "the tree, which is too late. Call it before the node enters "
        "the tree, or use NetwEntity.of to find an entity that already "
        "exists.",
        1
    );
    CHECK(netw::Netw::configure_entity(child).is_null());
    CHECK(netw::Netw::configure_entity(root) == entity);
    CHECK(NetwEntity::of(child) == entity);
    root->get_parent()->remove_child(root);
    memdelete(root);
}

} // namespace TestNetwEntityFacade

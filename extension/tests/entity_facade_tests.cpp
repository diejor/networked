// Laws for NetwEntity's static facade: of, ensure, resolve and bind.
//
// The wrapper verbs underneath these are covered beside the session that owns
// them. What is stated HERE is the facade's own contract, which is that every
// one of them answers a NetwEntity or nothing: the index is typed on
// RefCounted so it can hold whatever the mint produced, and a caller of this
// class is entitled to the typed answer or to null, never to a wrapper it has
// to test the class of.

#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwEntityFacade {

using namespace godot;
using netw::NetwEntity;

TEST_CASE(
    "[Networked][Entity][Hosted] F1 binding names the node and the record it "
    "mints reads back what the name spells"
) {
    Node *root = memnew(Node);
    root->set_name("Player");

    NETW_CHECK_EQ(NetwEntity::bind(root, "valeria", 42) == root, true);

    CHECK(root->get_name() == StringName("valeria|42"));
    const Ref<NetwEntity> entity = NetwEntity::of(root);
    REQUIRE(entity.is_valid());
    CHECK(entity->get_entity_id() == StringName("valeria"));
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
    // Minting twice is the same record, because a node stands for one entity.
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

    // The ask walks up, which is what makes a child part of the entity that
    // encloses it rather than an entity of its own.
    CHECK(NetwEntity::of(child) == owned);

    const Ref<NetwEntity> own = NetwEntity::ensure(child);
    REQUIRE(own.is_valid());
    CHECK(own != owned);
    // Minting does NOT walk up, so the child now answers for itself.
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
    // Asking again answers the same record rather than provisioning a second.
    CHECK(NetwEntity::resolve(child) == provisioned);

    CHECK(NetwEntity::resolve(nullptr).is_null());

    memdelete(parent);
}

} // namespace TestNetwEntityFacade

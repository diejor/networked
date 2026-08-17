// Laws for NetwSynchronizers, the walk that answers which synchronizers govern
// a node.
//
// The walk is a cache over a tree query, so the laws are about the two ways a
// cache is wrong: answering a synchronizer that no longer governs the node, and
// re-walking a tree that has not changed.

#include "support/netw_test.h"

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "netw/synchronizers.hpp"

namespace TestNetwSynchronizers {

using namespace godot;
using netw::NetwSynchronizers;

// The owner is set because `find_children` answers only OWNED children by
// default, which is what a scene-instantiated synchronizer always is and what a
// hand-built one never is.
MultiplayerSynchronizer *sync_under(Node *p_parent, const NodePath &p_root) {
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    p_parent->add_child(sync);
    sync->set_owner(p_parent);
    sync->set_root_path(p_root);
    return sync;
}

TEST_CASE(
    "[Networked][Sync][Hosted] the walk answers the synchronizers aimed at the "
    "node and no others"
) {
    Node *root = memnew(Node);
    Node *holder = memnew(Node);
    root->add_child(holder);
    holder->set_owner(root);
    MultiplayerSynchronizer *mine = sync_under(root, NodePath(".."));
    // Aimed at the holder rather than at the root, so it governs a different
    // node while living in the same subtree and being found by the same walk.
    sync_under(holder, NodePath(".."));

    const TypedArray<MultiplayerSynchronizer> found
        = NetwSynchronizers::of_node(root);

    REQUIRE(found.size() == 1);
    NETW_CHECK_EQ(Object::cast_to<Object>(found[0]), mine);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a node off the tree is walked and not cached"
) {
    Node *root = memnew(Node);
    // An orphan resolves no path, so the parent fallback is the only thing
    // that can tell a synchronizer aimed at this root from one that is not.
    MultiplayerSynchronizer *mine = sync_under(root, NodePath(".."));

    const TypedArray<MultiplayerSynchronizer> found
        = NetwSynchronizers::of_node(root);

    REQUIRE(found.size() == 1);
    NETW_CHECK_EQ(Object::cast_to<Object>(found[0]), mine);
    // Caching an off-tree answer would freeze a list taken before the node
    // learned where it lives.
    CHECK_FALSE(root->has_meta(NetwSynchronizers::meta_key()));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a governed target is the object and the "
    "property "
    "under it"
) {
    Node *root = memnew(Node);
    MultiplayerSynchronizer *sync = sync_under(root, NodePath(".."));
    Ref<SceneReplicationConfig> config;
    config.instantiate();
    config->add_property(NodePath(".:name"));
    sync->set_replication_config(config);

    const Array governed = NetwSynchronizers::governed_targets(sync, root);

    // One entry per resolvable path, as [object, sub_path], which is the key
    // two synchronizers overlap on.
    REQUIRE(governed.size() == 1);
    const Array pair = governed[0];
    REQUIRE(pair.size() == 2);
    NETW_CHECK_EQ(Object::cast_to<Object>(pair[0]), root);
    CHECK(NodePath(pair[1]) == NodePath(":name"));

    SUBCASE("a synchronizer with no config governs nothing") {
        MultiplayerSynchronizer *bare = sync_under(root, NodePath(".."));
        NETW_CHECK_EQ(
            NetwSynchronizers::governed_targets(bare, root).size(),
            0
        );
    }

    SUBCASE("a display binding names the key, the node and the property") {
        const Array bindings = NetwSynchronizers::display_bindings(sync, root);
        REQUIRE(bindings.size() == 1);
        const Array triple = bindings[0];
        REQUIRE(triple.size() == 3);
        CHECK(StringName(triple[0]) == StringName(".:name"));
        NETW_CHECK_EQ(Object::cast_to<Object>(triple[1]), root);
        CHECK(StringName(triple[2]) == StringName("name"));
    }

    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a deactivated node speaks to the server alone"
) {
    Node *root = memnew(Node);
    MultiplayerSynchronizer *sync = sync_under(root, NodePath(".."));

    NetwSynchronizers::sync_only_server(root);

    // Peer 0 is every peer, so clearing it and then naming the server is what
    // makes the exception rather than a second grant.
    CHECK_FALSE(sync->get_visibility_for(0));
    CHECK(sync->get_visibility_for(1));

    memdelete(root);
}

} // namespace TestNetwSynchronizers

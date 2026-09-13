#include "support/netw_test.h"

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/synchronizers.hpp"

namespace TestNetwSynchronizers {

using namespace godot;
namespace synchronizers = netw::synchronizers;

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
    sync_under(holder, NodePath(".."));

    const TypedArray<MultiplayerSynchronizer> found
        = synchronizers::of_node(root);

    REQUIRE(found.size() == 1);
    NETW_CHECK_EQ(Object::cast_to<Object>(found[0]), mine);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a node off the tree is walked and not cached"
) {
    Node *root = memnew(Node);
    MultiplayerSynchronizer *mine = sync_under(root, NodePath(".."));

    const TypedArray<MultiplayerSynchronizer> found
        = synchronizers::of_node(root);

    REQUIRE(found.size() == 1);
    NETW_CHECK_EQ(Object::cast_to<Object>(found[0]), mine);
    CHECK_FALSE(root->has_meta(synchronizers::meta_key()));

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

    const Array governed = synchronizers::governed_targets(sync, root);

    REQUIRE(governed.size() == 1);
    const Array pair = governed[0];
    REQUIRE(pair.size() == 2);
    NETW_CHECK_EQ(Object::cast_to<Object>(pair[0]), root);
    CHECK(NodePath(pair[1]) == NodePath(":name"));

    SUBCASE("a synchronizer with no config governs nothing") {
        MultiplayerSynchronizer *bare = sync_under(root, NodePath(".."));
        NETW_CHECK_EQ(synchronizers::governed_targets(bare, root).size(), 0);
    }

    SUBCASE("a display binding names the key, the node and the property") {
        const Array bindings = synchronizers::display_bindings(sync, root);
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

    synchronizers::sync_only_server(root);

    CHECK_FALSE(sync->get_visibility_for(0));
    CHECK(sync->get_visibility_for(1));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a subtree governed by no synchronizer is "
    "visible, because an absent filter is not a refusal"
) {
    Node *root = memnew(Node);
    CHECK(synchronizers::visibility_verdict(root, 2, 1));

    Node *child = memnew(Node);
    root->add_child(child);
    child->set_owner(root);
    CHECK(synchronizers::visibility_verdict(root, 2, 1));
    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] one governing synchronizer saying yes is "
    "enough, and every one saying no refuses"
) {
    Node *root = memnew(Node);
    MultiplayerSynchronizer *first = sync_under(root, NodePath(".."));
    MultiplayerSynchronizer *second = sync_under(root, NodePath(".."));
    first->set_multiplayer_authority(1, false);
    second->set_multiplayer_authority(1, false);
    first->set_visibility_public(false);
    second->set_visibility_public(false);

    CHECK_FALSE(synchronizers::visibility_verdict(root, 2, 1));

    second->set_visibility_for(2, true);
    CHECK(synchronizers::visibility_verdict(root, 2, 1));
    CHECK_FALSE(synchronizers::visibility_verdict(root, 3, 1));
    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a synchronizer this peer does not hold does "
    "not vote, so a subtree holding only those reads as ungoverned"
) {
    Node *root = memnew(Node);
    MultiplayerSynchronizer *sync = sync_under(root, NodePath(".."));
    sync->set_multiplayer_authority(7, false);
    sync->set_visibility_public(false);

    CHECK(synchronizers::visibility_verdict(root, 2, 1));
    CHECK_FALSE(synchronizers::visibility_verdict(root, 2, 7));
    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a synchronizer pointing somewhere else in the "
    "subtree does not govern the root"
) {
    Node *root = memnew(Node);
    Node *inner = memnew(Node);
    root->add_child(inner);
    inner->set_owner(root);
    MultiplayerSynchronizer *sync = sync_under(inner, NodePath(".."));
    sync->set_multiplayer_authority(1, false);
    sync->set_visibility_public(false);

    CHECK(synchronizers::visibility_verdict(root, 2, 1));
    CHECK_FALSE(synchronizers::visibility_verdict(inner, 2, 1));
    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] spawn state is the union of the authority's "
    "own governing synchronizers, and only their spawn properties"
) {
    Node2D *root = memnew(Node2D);
    MultiplayerSynchronizer *sync = sync_under(root, NodePath(".."));
    sync->set_multiplayer_authority(1, false);
    Ref<SceneReplicationConfig> config;
    config.instantiate();
    config->add_property(NodePath(".:position"));
    config->property_set_spawn(NodePath(".:position"), true);
    config->add_property(NodePath(".:rotation"));
    config->property_set_spawn(NodePath(".:rotation"), false);
    sync->set_replication_config(config);
    root->set_position(Vector2(3, 4));

    const Array out = synchronizers::spawn_state(root, 1);

    REQUIRE(out.size() == 1);
    const Dictionary entry = out[0];
    CHECK(String(NodePath(entry[StringName("path")])) == String(".:position"));
    CHECK(Vector2(entry[StringName("value")]) == Vector2(3, 4));

    CHECK(synchronizers::spawn_state(root, 7).is_empty());
    memdelete(root);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a spawn property with no subname carries no "
    "value, so it is not collected"
) {
    Node2D *root = memnew(Node2D);
    MultiplayerSynchronizer *sync = sync_under(root, NodePath(".."));
    sync->set_multiplayer_authority(1, false);
    Ref<SceneReplicationConfig> config;
    config.instantiate();
    config->add_property(NodePath("."));
    config->property_set_spawn(NodePath("."), true);
    sync->set_replication_config(config);

    CHECK(synchronizers::spawn_state(root, 1).is_empty());
    memdelete(root);
}

} // namespace TestNetwSynchronizers

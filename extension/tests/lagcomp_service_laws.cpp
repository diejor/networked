#include "support/netw_test.h"

#include "godot/scene_tree.hpp"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/nodes/multiplayer_tree.hpp"
#include "support/loopback_rig.h"

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/callable.hpp>

namespace TestNetwLagCompServiceLaws {

using godot::Callable;
using godot::Node;
using godot::Ref;
using godot::Script;
using godot::StringName;
using netw::NetwEntity;
using netw_test::LoopbackRig;

Node *node_from(const char *p_path) {
    const Ref<Script> script
        = godot::ResourceLoader::get_singleton()->load(p_path);
    REQUIRE_MESSAGE(script.is_valid(), "the node script did not load");
    if (script.is_null()) {
        return nullptr;
    }
    return godot::Object::cast_to<Node>(script->call("new"));
}

Node *rewindable_tree(Node *p_root, const char *p_name) {
    Node *tree = memnew(netw::MultiplayerTree);
    tree->set_name(p_name);
    p_root->add_child(tree);

    netw::NetwMultiplayer *api
        = LoopbackRig::core_of(tree->get(StringName("api")));
    REQUIRE(api != nullptr);
    REQUIRE(api->lagcomp_initialize(8, 12) == godot::OK);
    return tree;
}

bool observe_queued(netw::NetwMultiplayer *p_api, Node *p_node) {
    REQUIRE_MESSAGE(p_api != nullptr, "the session has no native core");
    if (p_api == nullptr) {
        return false;
    }
    return p_api->settle_has_key(StringName(
        godot::vformat("lagcomp-observe-node?%d", p_node->get_instance_id())
    ));
}

Node *stamped_late(Node *p_tree, const char *p_name, const StringName &p_id) {
    netw::NetwMultiplayer *api
        = LoopbackRig::core_of(p_tree->get(StringName("api")));
    Node *body = memnew(Node);
    body->set_name(p_name);
    p_tree->add_child(body);
    api->lagcomp_effect_arm(p_id, Callable(body, StringName("get_name")), 0);
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    entity->set_entity_id(p_id);
    return body;
}

bool drives_tick(netw::NetwMultiplayer *p_api) {
    REQUIRE_MESSAGE(p_api != nullptr, "the session has no native core");
    return p_api->is_connected(
        StringName("clock_on_tick"),
        callable_mp(p_api, &netw::NetwMultiplayer::tick_step)
    );
}

TEST_CASE(
    "[Networked][LagComp][Service][SceneTree] LS1 rewind is a configuration "
    "the session holds rather than a node it watches, so installing one "
    "leaves the session configured and driving the tick with nothing mounted "
    "to keep it that way"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Node *tree = rewindable_tree(root, "LagCompServiceTree");
    REQUIRE(tree != nullptr);

    netw::NetwMultiplayer *api
        = LoopbackRig::core_of(tree->get(StringName("api")));
    REQUIRE(api != nullptr);
    CHECK(api->lagcomp_is_configured());
    CHECK(drives_tick(api));

    Node *spare = memnew(Node);
    tree->add_child(spare);
    tree->remove_child(spare);
    memdelete(spare);

    CHECK(api->lagcomp_is_configured());
    CHECK(drives_tick(api));

    root->remove_child(tree);
    memdelete(tree);
}

TEST_CASE(
    "[Networked][LagComp][Settle][SceneTree] SO1 a node enters the "
    "tree before whatever stamps an entity onto it has run, so the session "
    "reads it a second time at its OWN settle, with no frame driven, and "
    "adopts the effect that entity's key had armed"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Node *tree = rewindable_tree(root, "LagCompSettleTree");
    REQUIRE(tree != nullptr);
    netw::NetwMultiplayer *api
        = LoopbackRig::core_of(tree->get(StringName("api")));
    REQUIRE(api != nullptr);

    Node *body = stamped_late(tree, "Late", StringName("late-subject"));

    REQUIRE_MESSAGE(
        api->lagcomp_effect_pending(StringName("late-subject")),
        "the arm must outlive the add, or the adopt proves nothing"
    );
    CHECK(observe_queued(api, body));

    api->session_flush_deferred();

    CHECK_FALSE(observe_queued(api, body));
    CHECK_FALSE(api->lagcomp_effect_pending(StringName("late-subject")));

    root->remove_child(tree);
    memdelete(tree);
}

TEST_CASE(
    "[Networked][LagComp][Settle][SceneTree] SO2 the second look is "
    "keyed by the node, so two nodes added in one cascade are both read "
    "rather than the first losing its turn to the last"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Node *tree = rewindable_tree(root, "LagCompCascadeTree");
    REQUIRE(tree != nullptr);
    netw::NetwMultiplayer *api
        = LoopbackRig::core_of(tree->get(StringName("api")));
    REQUIRE(api != nullptr);

    stamped_late(tree, "FirstLate", StringName("first-subject"));
    stamped_late(tree, "SecondLate", StringName("second-subject"));

    api->session_flush_deferred();

    CHECK_FALSE(api->lagcomp_effect_pending(StringName("first-subject")));
    CHECK_FALSE(api->lagcomp_effect_pending(StringName("second-subject")));

    root->remove_child(tree);
    memdelete(tree);
}

TEST_CASE(
    "[Networked][LagComp][Settle][SceneTree] SO3 an entity observed "
    "while it still carried no id is adopted when it spawns, because the id "
    "is stamped on the way into the tree and the observation is what waits"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    Node *tree = rewindable_tree(root, "LagCompSpawnedTree");
    REQUIRE(tree != nullptr);
    netw::NetwMultiplayer *api
        = LoopbackRig::core_of(tree->get(StringName("api")));
    REQUIRE(api != nullptr);
    Node *body = memnew(Node);
    body->set_name("Nameless");
    tree->add_child(body);
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    REQUIRE(entity.is_valid());
    api->observe_node_entity(body);

    api->lagcomp_effect_arm(
        StringName("spawned-subject"),
        Callable(body, StringName("get_name")),
        0
    );
    entity->set_entity_id(StringName("spawned-subject"));
    REQUIRE_MESSAGE(
        api->lagcomp_effect_pending(StringName("spawned-subject")),
        "the arm must outlive the stamp, or the adopt proves nothing"
    );

    entity->emit_signal(StringName("spawned"));

    CHECK_FALSE(api->lagcomp_effect_pending(StringName("spawned-subject")));

    root->remove_child(tree);
    memdelete(tree);
}

} // namespace TestNetwLagCompServiceLaws

#endif

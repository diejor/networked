#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSceneAnchor {

using namespace godot;
using netw::NetwSceneCore;

Ref<NetwSceneCore> fresh_core() {
    Ref<NetwSceneCore> core;
    core.instantiate();
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA1 a session that declared no anchor parents "
    "at the fallback the caller passed"
) {
    const Ref<NetwSceneCore> core = fresh_core();
    Node *root = memnew(Node);

    CHECK(core->get_scene_anchor() == nullptr);
    CHECK(core->spawn_anchor(root) == root);
    CHECK(core->spawn_anchor(nullptr) == nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA2 a declared anchor answers instead of the "
    "fallback, so a declaration node co-locates its own scenes"
) {
    const Ref<NetwSceneCore> core = fresh_core();
    Node *root = memnew(Node);
    Node *manager = memnew(Node);

    core->set_scene_anchor(manager);

    CHECK(core->get_scene_anchor() == manager);
    CHECK(core->spawn_anchor(root) == manager);
    CHECK(core->spawn_anchor(root) != root);
    CHECK(core->spawn_anchor(nullptr) == manager);

    memdelete(manager);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA3 giving the anchor back returns the answer "
    "to the fallback rather than leaving the spawn unanchored"
) {
    const Ref<NetwSceneCore> core = fresh_core();
    Node *root = memnew(Node);
    Node *manager = memnew(Node);

    core->set_scene_anchor(manager);
    CHECK(core->spawn_anchor(root) == manager);

    core->set_scene_anchor(nullptr);

    CHECK(core->get_scene_anchor() == nullptr);
    CHECK(core->spawn_anchor(root) == root);

    memdelete(manager);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA4 the last declaration wins the anchor, "
    "matching the one-instance-per-key rule the service registry follows"
) {
    const Ref<NetwSceneCore> core = fresh_core();
    Node *root = memnew(Node);
    Node *first = memnew(Node);
    Node *second = memnew(Node);

    core->set_scene_anchor(first);
    core->set_scene_anchor(second);

    CHECK(core->spawn_anchor(root) == second);
    CHECK(core->spawn_anchor(root) != first);

    memdelete(second);
    memdelete(first);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA5 a freed anchor is no anchor, so losing a "
    "declaration node degrades to the fallback instead of a dangling parent"
) {
    const Ref<NetwSceneCore> core = fresh_core();
    Node *root = memnew(Node);
    Node *manager = memnew(Node);

    core->set_scene_anchor(manager);
    CHECK(core->spawn_anchor(root) == manager);

    memdelete(manager);

    CHECK(core->get_scene_anchor() == nullptr);
    CHECK(core->spawn_anchor(root) == root);
    CHECK(core->spawn_anchor(nullptr) == nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SA6 clearing the book forgets the anchor, so "
    "the next session does not inherit the last one's parent"
) {
    const Ref<NetwSceneCore> core = fresh_core();
    Node *root = memnew(Node);
    Node *manager = memnew(Node);

    core->set_scene_anchor(manager);
    CHECK(core->spawn_anchor(root) == manager);

    core->clear();

    CHECK(core->get_scene_anchor() == nullptr);
    CHECK(core->spawn_anchor(root) == root);

    memdelete(manager);
    memdelete(root);
}

} // namespace TestNetwSceneAnchor

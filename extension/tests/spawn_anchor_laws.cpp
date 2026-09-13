#include "support/netw_test.h"

#include <cstdint>

#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "netw/spawn/spawner_roster.hpp"

namespace TestNetwSpawnAnchor {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;
using netw::spawn::SpawnerRoster;

struct Branch {
    Node *root = nullptr;
    Node *origin = nullptr;
    Node *destination = nullptr;
    MultiplayerSpawner *watching_origin = nullptr;
    MultiplayerSpawner *watching_destination = nullptr;
    Ref<NetwMultiplayer> core;
    SpawnerRoster roster;
};

MultiplayerSpawner *watcher(Node *p_root, Node *p_watched) {
    MultiplayerSpawner *spawner = memnew(MultiplayerSpawner);
    p_root->add_child(spawner);
    spawner->set_spawn_path(spawner->get_path_to(p_watched));
    spawner->set_spawn_function(
        Callable(p_root, "get_node").bind(NodePath("."))
    );
    return spawner;
}

Branch a_branch() {
    Branch out;
    out.root = memnew(Node);
    out.root->set_name("anchor-root");
    netw::gd::scene_root()->add_child(out.root);
    out.origin = memnew(Node);
    out.origin->set_name("origin");
    out.root->add_child(out.origin);
    out.destination = memnew(Node);
    out.destination->set_name("destination");
    out.root->add_child(out.destination);
    out.watching_origin = watcher(out.root, out.origin);
    out.watching_destination = watcher(out.root, out.destination);
    out.core.instantiate();
    out.core->session_set_root(
        Callable(out.root, "get_node").bind(NodePath("."))
    );
    out.roster.enrol(out.watching_origin);
    out.roster.enrol(out.watching_destination);
    return out;
}

void retire(Branch &r_branch) {
    netw::gd::scene_root()->remove_child(r_branch.root);
    memdelete(r_branch.root);
}

Record consumed(Branch &r_branch, Node *p_node) {
    Record record;
    record.set_route(1);
    record.set_recipe(Book::RECIPE_SPAWNER);
    record.set_scene_index(-1);
    record.bind_node(p_node);
    record.bind_spawner(r_branch.watching_origin);
    return record;
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SA1 a consumed record follows its "
    "node to the spawner watching the new parent, and keeps its origin anchor "
    "when none watches"
) {
    Branch branch = a_branch();
    Node *node = memnew(Node);
    node->set_name("mover");
    branch.origin->add_child(node);
    Record record = consumed(branch, node);

    branch.core->spawn_reanchor(&record, node, &branch.roster);
    NETW_CHECK_EQ(record.spawner(), branch.watching_origin);

    branch.origin->remove_child(node);
    branch.destination->add_child(node);
    branch.core->spawn_reanchor(&record, node, &branch.roster);
    NETW_CHECK_EQ(record.spawner(), branch.watching_destination);

    branch.destination->remove_child(node);
    branch.root->add_child(node);
    branch.core->spawn_reanchor(&record, node, &branch.roster);
    NETW_CHECK_EQ(record.spawner(), branch.watching_destination);

    retire(branch);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SA2 a recipe that names no spawner "
    "is never re-anchored, and neither is an orphan"
) {
    Branch branch = a_branch();
    Node *node = memnew(Node);
    node->set_name("scene-recipe");
    branch.destination->add_child(node);
    Record record = consumed(branch, node);
    record.set_recipe(Book::RECIPE_SCENE);

    branch.core->spawn_reanchor(&record, node, &branch.roster);
    NETW_CHECK_EQ(record.spawner(), branch.watching_origin);

    record.set_recipe(Book::RECIPE_SPAWNER);
    Node *orphan = memnew(Node);
    record.bind_node(orphan);
    branch.core->spawn_reanchor(&record, orphan, &branch.roster);
    NETW_CHECK_EQ(record.spawner(), branch.watching_origin);
    memdelete(orphan);

    branch.core->spawn_reanchor(&record, node, nullptr);
    NETW_CHECK_EQ(record.spawner(), branch.watching_origin);

    retire(branch);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SA3 a refresh reads the parent "
    "route off the parent's entity, and zero when the parent carries none"
) {
    Branch branch = a_branch();
    Node *node = memnew(Node);
    node->set_name("child");
    branch.destination->add_child(node);
    Record record = consumed(branch, node);
    record.set_parent_route(41);

    branch.core->spawn_refresh_anchor(&record, node, &branch.roster);
    NETW_CHECK_EQ(record.get_parent_route(), int64_t(0));
    NETW_CHECK_EQ(record.spawner(), branch.watching_destination);

    retire(branch);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SA4 the sweep refresh touches every "
    "in-tree record in the book and leaves an out-of-tree one alone"
) {
    Branch branch = a_branch();
    Book book;
    Node *seated = memnew(Node);
    seated->set_name("seated");
    branch.destination->add_child(seated);
    Node *orphan = memnew(Node);
    orphan->set_name("orphan");

    Record *moved = book.issue(consumed(branch, seated));
    Record orphaned = consumed(branch, orphan);
    orphaned.set_route(2);
    orphaned.set_parent_route(77);
    Record *still = book.issue(orphaned);

    branch.core->spawn_refresh_anchors(&book, &branch.roster);

    NETW_CHECK_EQ(moved->spawner(), branch.watching_destination);
    NETW_CHECK_EQ(still->spawner(), branch.watching_origin);
    NETW_CHECK_EQ(still->get_parent_route(), int64_t(77));

    branch.core->spawn_refresh_anchors(nullptr, &branch.roster);
    NETW_CHECK_EQ(moved->spawner(), branch.watching_destination);

    memdelete(orphan);
    retire(branch);
}

} // namespace TestNetwSpawnAnchor

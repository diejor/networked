#include "support/netw_test.h"

#include <cstdint>

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"

namespace TestNetwSpawnVisibility {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;

const int64_t VIEWER = 7;
const int64_t OTHER = 9;
const int64_t STRANGER = 11;

Node *seated(const String &p_name) {
    Node *node = memnew(Node);
    node->set_name(p_name);
    netw::gd::scene_root()->add_child(node);
    return node;
}

void retire(Node *p_node) {
    netw::gd::scene_root()->remove_child(p_node);
    memdelete(p_node);
}

Record *issued(
    Book *p_book,
    int64_t p_route,
    int64_t p_parent_route,
    Node *p_node
) {
    Record record;
    record.set_route(p_route);
    record.set_parent_route(p_parent_route);
    record.bind_node(p_node);
    return p_book->issue(record);
}

MultiplayerSynchronizer *governing(Node *p_root, bool p_public) {
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    p_root->add_child(sync);
    sync->set_root_path(NodePath(".."));
    sync->set_multiplayer_authority(1);
    sync->set_visibility_public(p_public);
    return sync;
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SV1 a node no authority-held "
    "synchronizer governs is desired by every peer"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *node = seated("ungoverned");

    CHECK(core->spawn_locally_desired(VIEWER, node));
    CHECK(core->spawn_locally_desired(OTHER, node));
    CHECK_FALSE(core->spawn_locally_desired(VIEWER, nullptr));

    retire(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SV2 a governed node is desired only "
    "by the peers its synchronizers admit, and one admitting synchronizer is "
    "enough"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *node = seated("governed");
    MultiplayerSynchronizer *closed = governing(node, false);

    CHECK_FALSE(core->spawn_locally_desired(VIEWER, node));
    CHECK_FALSE(core->spawn_locally_desired(OTHER, node));

    closed->set_visibility_for(int(VIEWER), true);
    CHECK(core->spawn_locally_desired(VIEWER, node));
    CHECK_FALSE(core->spawn_locally_desired(OTHER, node));

    governing(node, true);
    CHECK(core->spawn_locally_desired(OTHER, node));

    retire(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SV3 the ancestor clamp withholds a "
    "child from a peer its parent's record does not hold, however the child "
    "judges itself"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *parent = seated("parent");
    Node *child = seated("child");
    Record *above = issued(&book, 1, 0, parent);
    issued(&book, 2, 1, child);

    CHECK(core->spawn_locally_desired(VIEWER, child));
    CHECK_FALSE(core->spawn_visible_to(&book, 2, VIEWER, child));
    CHECK(core->spawn_visible_to(&book, 1, VIEWER, parent));

    above->add_recipient(int(VIEWER));
    CHECK(core->spawn_visible_to(&book, 2, VIEWER, child));

    CHECK_FALSE(core->spawn_visible_to(nullptr, 2, VIEWER, child));

    retire(child);
    retire(parent);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SV4 the reconcile rows carry one "
    "entry per in-tree record, in ancestry order, judged for every peer named"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *parent = seated("parent");
    Node *child = seated("child");
    Node *orphan = memnew(Node);
    issued(&book, 2, 1, child);
    issued(&book, 1, 0, parent);
    issued(&book, 3, 0, orphan);

    PackedInt32Array peers;
    peers.push_back(int32_t(VIEWER));
    peers.push_back(int32_t(OTHER));

    const TypedArray<Dictionary> rows
        = core->spawn_reconcile_rows(&book, peers);

    NETW_CHECK_EQ(int64_t(rows.size()), int64_t(2));
    const Dictionary first = rows[0];
    const Dictionary second = rows[1];
    NETW_CHECK_EQ(int64_t(first[StringName("route")]), int64_t(1));
    NETW_CHECK_EQ(int64_t(second[StringName("route")]), int64_t(2));
    NETW_CHECK_EQ(int64_t(second[StringName("parent_route")]), int64_t(1));

    const Dictionary desired = first[StringName("local_desired")];
    NETW_CHECK_EQ(int64_t(desired.size()), int64_t(2));
    CHECK(bool(desired[VIEWER]));
    CHECK(bool(desired[OTHER]));
    CHECK(Dictionary(first[StringName("leave")]).is_empty());

    CHECK(core->spawn_reconcile_rows(nullptr, peers).is_empty());

    memdelete(orphan);
    retire(child);
    retire(parent);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SV5 a leave entry is raised exactly "
    "for a peer that holds the record and no longer wants it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *node = seated("mover");
    NetwEntity::ensure(node);
    Record *record = issued(&book, 1, 0, node);
    record->add_recipient(int(VIEWER));
    MultiplayerSynchronizer *closed = governing(node, false);
    closed->set_visibility_for(int(OTHER), true);

    PackedInt32Array peers;
    peers.push_back(int32_t(VIEWER));
    peers.push_back(int32_t(OTHER));
    peers.push_back(int32_t(STRANGER));

    const TypedArray<Dictionary> rows
        = core->spawn_reconcile_rows(&book, peers);
    NETW_CHECK_EQ(int64_t(rows.size()), int64_t(1));
    const Dictionary row = rows[0];
    const Dictionary desired = row[StringName("local_desired")];
    CHECK_FALSE(bool(desired[VIEWER]));
    CHECK(bool(desired[OTHER]));
    CHECK_FALSE(bool(desired[STRANGER]));

    const Dictionary leave = row[StringName("leave")];
    NETW_CHECK_EQ(int64_t(leave.size()), int64_t(1));
    CHECK(leave.has(VIEWER));
    CHECK_FALSE(leave.has(OTHER));
    CHECK_FALSE(leave.has(STRANGER));

    NETW_CHECK_EQ(
        int64_t(PackedInt32Array(row[StringName("recipients")]).size()),
        int64_t(1)
    );

    retire(node);
}

} // namespace TestNetwSpawnVisibility

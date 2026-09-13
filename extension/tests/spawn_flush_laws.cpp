#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"

namespace TestNetwSpawnFlush {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;

const int64_t SPAWN_CHANNEL = 14;

void armed(
    Book *p_book,
    int64_t p_route,
    Node *p_node,
    const String &p_node_name
) {
    Record record;
    record.set_route(p_route);
    record.bind_node(p_node);
    record.set_node_name(p_node_name);
    p_book->arm(record);
}

PackedByteArray a_frame() {
    PackedByteArray bytes;
    bytes.push_back(7);
    return bytes;
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SF1 an armed route issues once it "
    "is in the tree, taking its name and parent route from where it actually "
    "landed"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *parent = memnew(Node);
    parent->set_name("Arena");
    netw::gd::scene_root()->add_child(parent);
    Node *node = memnew(Node);
    node->set_name("Renamed");
    parent->add_child(node);
    armed(&book, 1, node, "StaleName");

    Record *issued = core->spawn_issue_armed(&book, 1);

    REQUIRE(issued != nullptr);
    NETW_CHECK_EQ(issued->get_route(), int64_t(1));
    CHECK(bool(issued->get_node_name() == String("Renamed")));
    NETW_CHECK_EQ(issued->get_parent_route(), int64_t(0));
    CHECK(book.has_spawned(1));
    CHECK_FALSE(book.has_armed(1));

    CHECK(core->spawn_issue_armed(&book, 1) == nullptr);
    CHECK(core->spawn_issue_armed(nullptr, 1) == nullptr);

    netw::gd::scene_root()->remove_child(parent);
    memdelete(parent);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SF2 an armed route that left the tree before "
    "the flush is dropped rather than issued"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *orphan = memnew(Node);
    orphan->set_name("Orphan");
    armed(&book, 1, orphan, String());

    CHECK(core->spawn_issue_armed(&book, 1) == nullptr);
    CHECK_FALSE(book.has_spawned(1));
    CHECK_FALSE(book.has_armed(1));

    memdelete(orphan);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SF3 a fan-out records exactly the "
    "peers it chose, so the record's recipient book is what the sweep later "
    "reconciles"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *parent = memnew(Node);
    parent->set_name("Arena");
    netw::gd::scene_root()->add_child(parent);
    Node *node = memnew(Node);
    node->set_name("Crate");
    parent->add_child(node);
    Record built;
    built.set_route(1);
    built.bind_node(node);
    Record *record = book.issue(built);
    record->add_recipient(99);

    PackedInt32Array connected;
    connected.push_back(2);
    connected.push_back(3);

    const PackedInt32Array chosen = core->spawn_fan_out(
        &book,
        record,
        node,
        a_frame(),
        connected,
        SPAWN_CHANNEL
    );

    NETW_CHECK_EQ(int64_t(chosen.size()), int64_t(2));
    CHECK(record->has_recipient(2));
    CHECK(record->has_recipient(3));
    CHECK_FALSE(record->has_recipient(99));

    netw::gd::scene_root()->remove_child(parent);
    memdelete(parent);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SF4 a fan-out with no frame to "
    "carry chooses nobody, and leaves the recipient book untouched"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *parent = memnew(Node);
    netw::gd::scene_root()->add_child(parent);
    Node *node = memnew(Node);
    parent->add_child(node);
    Record built;
    built.set_route(1);
    built.bind_node(node);
    Record *record = book.issue(built);
    record->add_recipient(5);

    PackedInt32Array connected;
    connected.push_back(2);

    CHECK(core->spawn_fan_out(
                  &book,
                  record,
                  node,
                  PackedByteArray(),
                  connected,
                  SPAWN_CHANNEL
    )
              .is_empty());
    CHECK(record->has_recipient(5));
    CHECK_FALSE(record->has_recipient(2));

    CHECK(core->spawn_fan_out(
                  &book,
                  record,
                  nullptr,
                  a_frame(),
                  connected,
                  SPAWN_CHANNEL
    )
              .is_empty());
    CHECK(record->has_recipient(5));

    netw::gd::scene_root()->remove_child(parent);
    memdelete(parent);
}

} // namespace TestNetwSpawnFlush

#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSpawnReparent {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;
using netw_test::CallLog;

const int64_t REPARENT_CHANNEL = 16;

Node *named(const String &p_name) {
    Node *node = memnew(Node);
    node->set_name(p_name);
    return node;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SP1 a placement raises the applying flag only "
    "for the duration of the add, and restores what it found"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *parent = named("parent");
    Node *node = named("node");

    CHECK_FALSE(core->spawn_applying_remote_frame());
    core->spawn_place_node(parent, node);
    CHECK_FALSE(core->spawn_applying_remote_frame());
    NETW_CHECK_EQ(node->get_parent(), parent);

    Node *stray = named("stray");
    core->spawn_place_node(nullptr, stray);
    NETW_CHECK_EQ(stray->get_parent(), nullptr);
    CHECK_FALSE(core->spawn_applying_remote_frame());

    memdelete(stray);
    memdelete(parent);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SP2 a reparent moves the node and tells the "
    "session to adopt it, and a node already there is not moved at all"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *origin = named("origin");
    Node *destination = named("destination");
    Node *node = named("mover");
    origin->add_child(node);

    CHECK(core->spawn_reparent_node(node, destination, log.callable("adopt")));
    NETW_CHECK_EQ(node->get_parent(), destination);
    NETW_CHECK_EQ(log.count("adopt"), 1);

    CHECK_FALSE(
        core->spawn_reparent_node(node, destination, log.callable("adopt"))
    );
    NETW_CHECK_EQ(log.count("adopt"), 1);

    CHECK(core->spawn_reparent_node(node, origin, Callable()));
    NETW_CHECK_EQ(node->get_parent(), origin);
    NETW_CHECK_EQ(log.count("adopt"), 1);

    CHECK_FALSE(core->spawn_reparent_node(nullptr, origin, Callable()));
    CHECK_FALSE(core->spawn_reparent_node(node, nullptr, Callable()));

    memdelete(destination);
    memdelete(origin);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SP3 a reparent frame is refused "
    "when the destination is outside the session tree, so peers keep the old "
    "parent"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *root = named("session-root");
    netw::gd::scene_root()->add_child(root);
    core->session_set_root(Callable(root, "get_node").bind(NodePath(".")));

    Node *inside = named("inside");
    root->add_child(inside);
    Node *node = named("mover");
    inside->add_child(node);

    Record built;
    built.set_route(1);
    built.bind_node(node);
    Record *record = book.issue(built);

    PackedInt32Array connected;
    connected.push_back(2);

    CHECK(core->spawn_send_reparent(
        &book,
        record,
        node,
        connected,
        REPARENT_CHANNEL
    ));

    Node *outside = named("outside");
    inside->remove_child(node);
    outside->add_child(node);
    ERR_PRINT_OFF;
    CHECK_FALSE(core->spawn_send_reparent(
        &book,
        record,
        node,
        connected,
        REPARENT_CHANNEL
    ));
    CHECK_FALSE(core->spawn_send_reparent(
        &book,
        record,
        nullptr,
        connected,
        REPARENT_CHANNEL
    ));
    CHECK_FALSE(core->spawn_send_reparent(
        nullptr,
        record,
        node,
        connected,
        REPARENT_CHANNEL
    ));
    ERR_PRINT_ON;

    memdelete(outside);
    netw::gd::scene_root()->remove_child(root);
    memdelete(root);
}

} // namespace TestNetwSpawnReparent

#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSpawnPlanExecution {

using namespace godot;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;
using netw_test::CallLog;

const int64_t SPAWN_CHANNEL = 14;
const int64_t HIDE_CHANNEL = 18;

struct Stand {
    Ref<NetwMultiplayer> core;
    Book book;
    Node *root = nullptr;
};

Stand a_stand() {
    Stand out;
    out.core.instantiate();
    out.root = memnew(Node);
    out.root->set_name("Arena");
    netw::gd::scene_root()->add_child(out.root);
    return out;
}

void retire(Stand &r_stand) {
    netw::gd::scene_root()->remove_child(r_stand.root);
    memdelete(r_stand.root);
}

Record *issued(Stand &r_stand, int64_t p_route, Node *p_node) {
    r_stand.root->add_child(p_node);
    Record record;
    record.set_route(p_route);
    record.bind_node(p_node);
    return r_stand.book.issue(record);
}

Dictionary op(int64_t p_route, int64_t p_peer, const char *p_action) {
    Dictionary entry;
    entry[StringName("route")] = p_route;
    entry[StringName("peer")] = p_peer;
    entry[StringName("action")] = StringName(p_action);
    return entry;
}

PackedByteArray a_frame() {
    PackedByteArray bytes;
    bytes.push_back(7);
    return bytes;
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SX1 one spawn frame is encoded per "
    "route however many peers gained it, and each gainer joins the recipient "
    "book"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record *record = issued(stand, 1, node);

    Array plan;
    plan.push_back(op(1, 2, "spawn"));
    plan.push_back(op(1, 3, "spawn"));

    stand.core->spawn_execute_plan(
        &stand.book,
        plan,
        SPAWN_CHANNEL,
        HIDE_CHANNEL,
        log.answering("encode", a_frame())
    );

    NETW_CHECK_EQ(log.count("encode"), 1);
    CHECK(record->has_recipient(2));
    CHECK(record->has_recipient(3));

    retire(stand);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SX2 a route whose frame will not "
    "encode joins nobody's recipient book, and is not re-encoded for the next "
    "peer"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record *record = issued(stand, 1, node);

    Array plan;
    plan.push_back(op(1, 2, "spawn"));
    plan.push_back(op(1, 3, "spawn"));

    stand.core->spawn_execute_plan(
        &stand.book,
        plan,
        SPAWN_CHANNEL,
        HIDE_CHANNEL,
        log.answering("encode", PackedByteArray())
    );

    NETW_CHECK_EQ(log.count("encode"), 1);
    CHECK_FALSE(record->has_recipient(2));
    CHECK_FALSE(record->has_recipient(3));

    retire(stand);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SX3 a hide drops the peer from "
    "the recipient book, and a retain leaves it holding the entity"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record *record = issued(stand, 1, node);
    record->add_recipient(2);
    record->add_recipient(3);

    Array plan;
    plan.push_back(op(1, 2, "hide"));
    plan.push_back(op(1, 3, "retain"));

    stand.core->spawn_execute_plan(
        &stand.book,
        plan,
        SPAWN_CHANNEL,
        HIDE_CHANNEL,
        log.answering("encode", a_frame())
    );

    CHECK_FALSE(record->has_recipient(2));
    CHECK(record->has_recipient(3));
    NETW_CHECK_EQ(log.count("encode"), 0);

    retire(stand);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SX4 an operation naming a route the "
    "book has forgotten, a node out of the tree, or no action at all is "
    "skipped whole"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record *record = issued(stand, 1, node);
    record->add_recipient(5);
    Node *orphan = memnew(Node);
    Record gone;
    gone.set_route(2);
    gone.bind_node(orphan);
    stand.book.issue(gone);

    Array plan;
    plan.push_back(op(9, 2, "spawn"));
    plan.push_back(op(2, 2, "spawn"));
    plan.push_back(op(1, 5, "shrug"));

    stand.core->spawn_execute_plan(
        &stand.book,
        plan,
        SPAWN_CHANNEL,
        HIDE_CHANNEL,
        log.answering("encode", a_frame())
    );

    NETW_CHECK_EQ(log.count("encode"), 0);
    CHECK_FALSE(record->has_recipient(2));
    CHECK_FALSE(gone.has_recipient(2));
    CHECK(record->has_recipient(5));

    stand.core->spawn_execute_plan(
        nullptr,
        plan,
        SPAWN_CHANNEL,
        HIDE_CHANNEL,
        log.answering("encode", a_frame())
    );
    NETW_CHECK_EQ(log.count("encode"), 0);

    memdelete(orphan);
    retire(stand);
}

} // namespace TestNetwSpawnPlanExecution

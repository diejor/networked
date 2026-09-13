#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSpawnReplay {

using namespace godot;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;
using netw_test::CallLog;

const int64_t SPAWN_CHANNEL = 14;
const int64_t LATE = 4;

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

Record *issued(
    Stand &r_stand,
    int64_t p_route,
    int64_t p_parent_route,
    Node *p_node
) {
    Record record;
    record.set_route(p_route);
    record.set_parent_route(p_parent_route);
    record.bind_node(p_node);
    return r_stand.book.issue(record);
}

Node *seated(Stand &r_stand, const String &p_name) {
    Node *node = memnew(Node);
    node->set_name(p_name);
    r_stand.root->add_child(node);
    return node;
}

PackedByteArray a_frame() {
    PackedByteArray bytes;
    bytes.push_back(7);
    return bytes;
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SR1 a late joiner is replayed "
    "parents before children, and joins each record's recipient book as it "
    "goes"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *parent = seated(stand, "Parent");
    Node *child = seated(stand, "Child");
    Record *low = issued(stand, 2, 1, child);
    Record *high = issued(stand, 1, 0, parent);

    const PackedInt64Array unencodable = stand.core->spawn_replay_to(
        &stand.book,
        LATE,
        SPAWN_CHANNEL,
        log.answering("encode", a_frame())
    );

    CHECK(unencodable.is_empty());
    NETW_CHECK_EQ(log.count("encode"), 2);
    CHECK(high->has_recipient(int(LATE)));
    CHECK(low->has_recipient(int(LATE)));

    const Array first = log.args("encode", 0);
    NETW_CHECK_EQ(int64_t(first.size()), int64_t(2));
    if (first.size() == 2) {
        NETW_CHECK_EQ(int64_t(first[0]), int64_t(1));
    }

    retire(stand);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SR2 a record the peer already holds "
    "is not replayed, and neither is one whose node left the tree"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *held = seated(stand, "Held");
    Node *orphan = memnew(Node);
    orphan->set_name("Orphan");
    Record *already = issued(stand, 1, 0, held);
    already->add_recipient(int(LATE));
    issued(stand, 2, 0, orphan);

    CHECK(stand.core
              ->spawn_replay_to(
                  &stand.book,
                  LATE,
                  SPAWN_CHANNEL,
                  log.answering("encode", a_frame())
              )
              .is_empty());
    NETW_CHECK_EQ(log.count("encode"), 0);

    memdelete(orphan);
    retire(stand);
}

TEST_CASE(
    "[Networked][Spawn][Hosted][SceneTree] SR3 a record whose frame will not "
    "encode is reported by route and enrolls nobody, and the replay carries on "
    "past it"
) {
    Stand stand = a_stand();
    CallLog log;
    Node *node = seated(stand, "Crate");
    Record *record = issued(stand, 1, 0, node);

    const PackedInt64Array unencodable = stand.core->spawn_replay_to(
        &stand.book,
        LATE,
        SPAWN_CHANNEL,
        log.answering("encode", PackedByteArray())
    );

    NETW_CHECK_EQ(int64_t(unencodable.size()), int64_t(1));
    if (unencodable.size() == 1) {
        NETW_CHECK_EQ(unencodable[0], int64_t(1));
    }
    CHECK_FALSE(record->has_recipient(int(LATE)));

    CHECK(stand.core
              ->spawn_replay_to(
                  nullptr,
                  LATE,
                  SPAWN_CHANNEL,
                  log.answering("encode", a_frame())
              )
              .is_empty());

    retire(stand);
}

} // namespace TestNetwSpawnReplay

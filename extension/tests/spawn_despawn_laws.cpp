#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSpawnDespawn {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::spawn::Book;
using netw::spawn::Record;
using netw_test::CallLog;

const int64_t DESPAWN_CHANNEL = 15;

Record *tracked(
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

TEST_CASE(
    "[Networked][Spawn][Hosted] SD1 an untracked route despawns nothing and "
    "says so, so the caller flushes no carrier for it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    CallLog log;
    Node *node = memnew(Node);
    tracked(&book, 1, 0, node);

    CHECK_FALSE(core->spawn_despawn_route(
        &book,
        9,
        PackedInt32Array(),
        DESPAWN_CHANNEL,
        log.callable("undeclare")
    ));
    NETW_CHECK_EQ(log.count("undeclare"), 0);
    CHECK(book.has_spawned(1));

    CHECK_FALSE(core->spawn_despawn_route(
        nullptr,
        1,
        PackedInt32Array(),
        DESPAWN_CHANNEL,
        log.callable("undeclare")
    ));
    CHECK(book.has_spawned(1));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SD2 a despawn cascades children before the "
    "ancestor that contains them, and empties the book of the whole subtree"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    CallLog log;
    Node *parent = memnew(Node);
    Node *child = memnew(Node);
    Node *grandchild = memnew(Node);
    NetwEntity::ensure(parent);
    NetwEntity::ensure(child);
    NetwEntity::ensure(grandchild);
    tracked(&book, 1, 0, parent);
    tracked(&book, 2, 1, child);
    tracked(&book, 3, 2, grandchild);

    CHECK(core->spawn_despawn_route(
        &book,
        1,
        PackedInt32Array(),
        DESPAWN_CHANNEL,
        log.callable("undeclare")
    ));

    NETW_CHECK_EQ(log.count("undeclare"), 3);
    CHECK_FALSE(book.has_spawned(1));
    CHECK_FALSE(book.has_spawned(2));
    CHECK_FALSE(book.has_spawned(3));
    NETW_CHECK_EQ(book.spawned_count(), 0);

    const Array first = log.args("undeclare", 0);
    const Array last = log.args("undeclare", 2);
    NETW_CHECK_EQ(int64_t(first.size()), int64_t(1));
    NETW_CHECK_EQ(int64_t(last.size()), int64_t(1));
    if (first.size() == 1 && last.size() == 1) {
        NETW_CHECK_EQ(
            uint64_t(RID(first[0]).get_id()),
            uint64_t(NetwEntity::of(grandchild)->get_rid_handle().get_id())
        );
        NETW_CHECK_EQ(
            uint64_t(RID(last[0]).get_id()),
            uint64_t(NetwEntity::of(parent)->get_rid_handle().get_id())
        );
    }

    memdelete(grandchild);
    memdelete(child);
    memdelete(parent);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SD3 a record whose node carries no entity is "
    "still dropped, because the book's plan is what the despawn honours"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    CallLog log;
    Node *bare = memnew(Node);
    Node *carrying = memnew(Node);
    NetwEntity::ensure(carrying);
    tracked(&book, 1, 0, bare);
    tracked(&book, 2, 1, carrying);

    CHECK(core->spawn_despawn_route(
        &book,
        1,
        PackedInt32Array(),
        DESPAWN_CHANNEL,
        log.callable("undeclare")
    ));

    NETW_CHECK_EQ(log.count("undeclare"), 1);
    NETW_CHECK_EQ(book.spawned_count(), 0);

    memdelete(carrying);
    memdelete(bare);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SD4 a caller that declares nothing needs no "
    "undeclare, so an absent one is not a refusal"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Book book;
    Node *node = memnew(Node);
    NetwEntity::ensure(node);
    tracked(&book, 1, 0, node);

    CHECK(core->spawn_despawn_route(
        &book,
        1,
        PackedInt32Array(),
        DESPAWN_CHANNEL,
        Callable()
    ));
    NETW_CHECK_EQ(book.spawned_count(), 0);

    memdelete(node);
}

} // namespace TestNetwSpawnDespawn

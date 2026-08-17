#include "support/netw_test.h"

#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "netw/spawn_record.hpp"

namespace TestNetwSpawnRecord {

using namespace godot;
using netw::NetwSpawnRecord;

Ref<NetwSpawnRecord> fresh() {
    Ref<NetwSpawnRecord> record;
    record.instantiate();
    return record;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a record names the node it was stamped for and "
    "answers nothing once that node is freed"
) {
    const Ref<NetwSpawnRecord> record = fresh();
    Node *node = memnew(Node);

    CHECK(record->node() == nullptr);
    record->bind_node(node);
    CHECK(record->node() == node);

    memdelete(node);
    CHECK(record->node() == nullptr);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] binding nothing forgets the node the record "
    "named"
) {
    const Ref<NetwSpawnRecord> record = fresh();
    Node *node = memnew(Node);
    record->bind_node(node);

    record->bind_node(nullptr);

    CHECK(record->node() == nullptr);
    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the function host and the consumed spawner "
    "outlive nothing either"
) {
    const Ref<NetwSpawnRecord> record = fresh();
    Node *host = memnew(Node);
    MultiplayerSpawner *spawner = memnew(MultiplayerSpawner);

    record->bind_fn_host(host);
    record->bind_spawner(spawner);
    CHECK(record->fn_host() == host);
    CHECK(record->spawner() == spawner);

    memdelete(host);
    memdelete(spawner);
    CHECK(record->fn_host() == nullptr);
    CHECK(record->spawner() == nullptr);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a recipient is enrolled once and each mutation "
    "answers whether it moved the book"
) {
    const Ref<NetwSpawnRecord> record = fresh();

    CHECK(record->add_recipient(7));
    CHECK_FALSE(record->add_recipient(7));
    CHECK(record->add_recipient(9));

    CHECK(record->has_recipient(7));
    CHECK(record->has_recipient(9));
    CHECK_FALSE(record->has_recipient(11));

    CHECK(record->remove_recipient(7));
    CHECK_FALSE(record->remove_recipient(7));
    CHECK_FALSE(record->has_recipient(7));
    CHECK(record->has_recipient(9));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the recipients read back in the order they "
    "were enrolled"
) {
    const Ref<NetwSpawnRecord> record = fresh();
    record->add_recipient(9);
    record->add_recipient(3);
    record->add_recipient(5);
    record->remove_recipient(3);

    PackedInt32Array expected;
    expected.push_back(9);
    expected.push_back(5);
    CHECK(record->recipients() == expected);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] replacing the recipients drops the whole "
    "previous set and keeps no duplicate"
) {
    const Ref<NetwSpawnRecord> record = fresh();
    record->add_recipient(7);

    PackedInt32Array peers;
    peers.push_back(2);
    peers.push_back(4);
    peers.push_back(2);
    record->set_recipients(peers);

    CHECK_FALSE(record->has_recipient(7));
    NETW_CHECK_EQ(record->recipients().size(), 2);
    CHECK(record->has_recipient(2));
    CHECK(record->has_recipient(4));
}

} // namespace TestNetwSpawnRecord

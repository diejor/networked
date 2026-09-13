#include "support/netw_test.h"

#include "godot/multiplayer_spawner.hpp"
#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/spawn/record.hpp"
#include "netw/wire/stream.hpp"

namespace TestNetwRecord {

using namespace godot;
using netw::spawn::Record;

Dictionary round_trip_header(const Record &p_record, Object *p_entity) {
    netw::wire::WriteStream writer;
    Dictionary header;
    if (!p_record.encode_header(writer, p_entity)) {
        return header;
    }
    netw::wire::ReadStream reader(writer.to_bytes());
    Record::decode_header(reader, header);
    return header;
}

int64_t header_size(const Record &p_record, Object *p_entity) {
    netw::wire::WriteStream writer;
    if (!p_record.encode_header(writer, p_entity)) {
        return -1;
    }
    return int64_t(writer.to_bytes().size());
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a record names the node it was stamped for and "
    "answers nothing once that node is freed"
) {
    Record record;
    Node *node = memnew(Node);

    CHECK(record.node() == nullptr);
    record.bind_node(node);
    CHECK(record.node() == node);

    memdelete(node);
    CHECK(record.node() == nullptr);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] binding nothing forgets the node the record "
    "named"
) {
    Record record;
    Node *node = memnew(Node);
    record.bind_node(node);

    record.bind_node(nullptr);

    CHECK(record.node() == nullptr);
    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the function host and the consumed spawner "
    "outlive nothing either"
) {
    Record record;
    Node *host = memnew(Node);
    MultiplayerSpawner *spawner = memnew(MultiplayerSpawner);

    record.bind_fn_host(host);
    record.bind_spawner(spawner);
    CHECK(record.fn_host() == host);
    CHECK(record.spawner() == spawner);

    memdelete(host);
    memdelete(spawner);
    CHECK(record.fn_host() == nullptr);
    CHECK(record.spawner() == nullptr);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a recipient is enrolled once and each mutation "
    "answers whether it moved the book"
) {
    Record record;

    CHECK(record.add_recipient(7));
    CHECK_FALSE(record.add_recipient(7));
    CHECK(record.add_recipient(9));

    CHECK(record.has_recipient(7));
    CHECK(record.has_recipient(9));
    CHECK_FALSE(record.has_recipient(11));

    CHECK(record.remove_recipient(7));
    CHECK_FALSE(record.remove_recipient(7));
    CHECK_FALSE(record.has_recipient(7));
    CHECK(record.has_recipient(9));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the recipients read back in the order they "
    "were enrolled"
) {
    Record record;
    record.add_recipient(9);
    record.add_recipient(3);
    record.add_recipient(5);
    record.remove_recipient(3);

    PackedInt32Array expected;
    expected.push_back(9);
    expected.push_back(5);
    CHECK(record.recipients() == expected);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] replacing the recipients drops the whole "
    "previous set and keeps no duplicate"
) {
    Record record;
    record.add_recipient(7);

    PackedInt32Array peers;
    peers.push_back(2);
    peers.push_back(4);
    peers.push_back(2);
    record.set_recipients(peers);

    CHECK_FALSE(record.has_recipient(7));
    NETW_CHECK_EQ(record.recipients().size(), 2);
    CHECK(record.has_recipient(2));
    CHECK(record.has_recipient(4));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the frame header round trips, and a record "
    "with no entity carries its own captured controller"
) {
    Record record;
    record.set_route(7);
    record.set_entity_id(StringName("probe@7"));
    record.set_peer_id(3);
    record.set_controller(5);
    record.set_node_name(String("Ghost"));

    const Dictionary header = round_trip_header(record, nullptr);

    CHECK(StringName(header[StringName("entity_id")]) == StringName("probe@7"));
    NETW_CHECK_EQ(int64_t(header[StringName("peer_id")]), 3);
    NETW_CHECK_EQ(int64_t(header[StringName("controller")]), 5);
    NETW_CHECK_EQ(int64_t(header[StringName("action_requester")]), 0);
    NETW_CHECK_EQ(int64_t(header[StringName("wire_hash")]), 0);
    CHECK_FALSE(bool(header[StringName("declares_scene")]));
    CHECK(String(header[StringName("node_name")]) == String("Ghost"));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the -1 no-action sentinel survives the header, "
    "because the field is biased by one and never hands a negative to the "
    "unsigned varint"
) {
    Record record;
    record.set_route(1);
    record.set_node_name(String("Ghost"));

    const Dictionary header = round_trip_header(record, nullptr);

    NETW_CHECK_EQ(int64_t(header[StringName("action_spawn_tick")]), -1);
    CHECK(int64_t(header[StringName("action_spawn_tick")]) < 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a real action tick survives the bias, "
    "including tick zero, which the sentinel must not collide with"
) {
    Node *owner = memnew(Node);
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(owner);
    REQUIRE(entity.is_valid());

    Record record;
    record.set_route(1);
    record.set_node_name(String("Bolt"));

    const int64_t ticks[] = {0, 1, 126, 127, 128, 65535};
    for (int at = 0; at < 6; at++) {
        entity->set_action_spawn_tick(ticks[at]);
        const Dictionary header = round_trip_header(record, entity.ptr());
        NETW_CHECK_EQ(
            int64_t(header[StringName("action_spawn_tick")]),
            ticks[at]
        );
    }
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the live entity's controller beats the "
    "record's, because a late joiner must see a mid-session grant"
) {
    Node *owner = memnew(Node);
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(owner);
    REQUIRE(entity.is_valid());
    entity->set_action_spawn_tick(42);
    entity->set_action_requester(9);

    Record record;
    record.set_route(1);
    record.set_controller(5);
    record.set_node_name(String("Player"));

    const Dictionary header = round_trip_header(record, entity.ptr());

    NETW_CHECK_EQ(
        int64_t(header[StringName("controller")]),
        entity->get_controller()
    );
    NETW_CHECK_EQ(int64_t(header[StringName("action_spawn_tick")]), 42);
    NETW_CHECK_EQ(int64_t(header[StringName("action_requester")]), 9);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a declared scene carries its label and an "
    "undeclared one carries no bytes for it"
) {
    Node *owner = memnew(Node);
    const Ref<netw::NetwEntity> entity = netw::NetwEntity::ensure(owner);
    REQUIRE(entity.is_valid());

    Record record;
    record.set_route(1);
    record.set_node_name(String("Arena"));

    const int64_t plain = header_size(record, entity.ptr());

    entity->set_declares_scene(true);
    entity->set_scene_label(StringName("arena"));
    const int64_t declared = header_size(record, entity.ptr());

    NETW_CHECK_GT(declared, plain);

    const Dictionary header = round_trip_header(record, entity.ptr());
    CHECK(bool(header[StringName("declares_scene")]));
    CHECK(StringName(header[StringName("scene_label")]) == StringName("arena"));
    CHECK(String(header[StringName("node_name")]) == String("Arena"));
    memdelete(owner);
}

} // namespace TestNetwRecord

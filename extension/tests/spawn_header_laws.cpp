#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/spawn/record.hpp"

namespace TestNetwSpawnHeader {

using namespace godot;
using netw::NetwEntity;
using netw::spawn::DescriptorRow;
using netw::spawn::Record;

Dictionary a_header() {
    Dictionary header;
    header[StringName("route")] = int64_t(12);
    header[StringName("entity_id")] = StringName("hero");
    header[StringName("peer_id")] = int64_t(3);
    header[StringName("controller")] = int64_t(5);
    header[StringName("action_spawn_tick")] = int64_t(41);
    header[StringName("action_requester")] = int64_t(9);
    header[StringName("wire_hash")] = int64_t(77);
    header[StringName("declares_scene")] = true;
    header[StringName("scene_label")] = StringName("Arena");
    header[StringName("node_name")] = String("hero");
    return header;
}

LocalVector<DescriptorRow> rows_of(
    const std::initializer_list<std::pair<uint64_t, uint64_t>> &p_rows
) {
    LocalVector<DescriptorRow> out;
    for (const std::pair<uint64_t, uint64_t> &each : p_rows) {
        DescriptorRow row;
        row.ordinal = each.first;
        row.schema_hash = each.second;
        out.push_back(row);
    }
    return out;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SH1 a decoded header stamps every identity "
    "field the encoder carried onto the orphan it names"
) {
    Node *node = memnew(Node);

    const Ref<NetwEntity> entity
        = Record::stamp_header(node, a_header(), nullptr);

    REQUIRE(entity.is_valid());
    NETW_CHECK_EQ(entity->get_route(), int64_t(12));
    CHECK(bool(String(entity->get_entity_id()) == String("hero")));
    NETW_CHECK_EQ(entity->get_peer_id(), int64_t(3));
    NETW_CHECK_EQ(entity->get_controller(), int64_t(5));
    NETW_CHECK_EQ(entity->get_action_spawn_tick(), int64_t(41));
    NETW_CHECK_EQ(entity->get_action_requester(), int64_t(9));
    CHECK(entity->get_declares_scene());
    CHECK(bool(String(entity->get_scene_label()) == String("Arena")));
    NETW_CHECK_EQ(entity->comp_table().get_wire_hash(), int64_t(77));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SH2 a header the encoder left a field out of "
    "stamps that field's absent value rather than keeping the old one"
) {
    Node *node = memnew(Node);
    Record::stamp_header(node, a_header(), nullptr);

    Dictionary sparse;
    sparse[StringName("route")] = int64_t(12);
    const Ref<NetwEntity> entity = Record::stamp_header(node, sparse, nullptr);

    REQUIRE(entity.is_valid());
    NETW_CHECK_EQ(entity->get_action_spawn_tick(), int64_t(-1));
    NETW_CHECK_EQ(entity->get_action_requester(), int64_t(0));
    CHECK_FALSE(entity->get_declares_scene());
    CHECK(bool(String(entity->get_scene_label()) == String()));

    CHECK(Record::stamp_header(nullptr, a_header(), nullptr).is_null());
    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SH3 the native state section writes each "
    "sub-property onto the node its path names, and skips one that is absent"
) {
    Node2D *root = memnew(Node2D);
    root->set_name("root");
    Node2D *child = memnew(Node2D);
    child->set_name("child");
    root->add_child(child);

    const PackedByteArray here = netw::gd::var_to_bytes(Vector2(4, 5));
    const PackedByteArray there = netw::gd::var_to_bytes(Vector2(6, 7));
    const PackedByteArray lost = netw::gd::var_to_bytes(Vector2(8, 9));

    Record::apply_native_entry(root, ":position", here);
    Record::apply_native_entry(root, "child:position", there);
    Record::apply_native_entry(root, "ghost:position", lost);

    NETW_CHECK_CLOSE(root->get_position().x, 4.0, 0.001);
    NETW_CHECK_CLOSE(root->get_position().y, 5.0, 0.001);
    NETW_CHECK_CLOSE(child->get_position().x, 6.0, 0.001);
    NETW_CHECK_CLOSE(child->get_position().y, 7.0, 0.001);

    Record::apply_native_entry(nullptr, ":position", here);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SH4 a descriptor section reads exactly its own "
    "count, so the section after it starts where this one stopped"
) {
    PackedByteArray bytes
        = Record::write_descriptors(rows_of({{0, 111}, {1, 222}}));
    bytes.append_array(Record::write_descriptors(rows_of({{9, 333}})));

    netw::wire::ReadStream stream(bytes);
    Dictionary consumed;
    Dictionary derived;
    const bool both = Record::read_descriptors(stream, consumed)
        && Record::read_descriptors(stream, derived);

    REQUIRE(both);
    NETW_CHECK_EQ(int64_t(consumed.size()), int64_t(2));
    NETW_CHECK_EQ(int64_t(consumed[int64_t(0)]), int64_t(111));
    NETW_CHECK_EQ(int64_t(consumed[int64_t(1)]), int64_t(222));
    NETW_CHECK_EQ(int64_t(derived.size()), int64_t(1));
    NETW_CHECK_EQ(int64_t(derived[int64_t(9)]), int64_t(333));

    CHECK(Record::descriptors_of_bytes(PackedByteArray()).is_empty());
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SH6 a descriptor run with a byte left over is "
    "refused whole, because a run that does not end where its blob does was "
    "framed by a peer reading a different declaration"
) {
    PackedByteArray bytes = Record::write_descriptors(rows_of({{0, 111}}));
    const bool whole = !Record::descriptors_of_bytes(bytes).is_empty();
    bytes.push_back(0);
    const bool residue_refused = Record::descriptors_of_bytes(bytes).is_empty();

    CHECK(whole);
    CHECK(residue_refused);

    PackedByteArray cut = Record::write_descriptors(rows_of({{0, 111}}));
    cut.resize(cut.size() - 1);
    const bool short_refused = Record::descriptors_of_bytes(cut).is_empty();
    CHECK(short_refused);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SH5 a descriptor hash carries all thirty-two "
    "bits, so two schemas agreeing in their low sixteen stay distinguishable"
) {
    const Dictionary read = Record::descriptors_of_bytes(
        Record::write_descriptors(rows_of({{0, 0x1234ABCD}, {1, 0x5678ABCD}}))
    );

    NETW_CHECK_EQ(int64_t(read.size()), int64_t(2));
    NETW_CHECK_EQ(int64_t(read[int64_t(0)]), int64_t(0x1234ABCD));
    NETW_CHECK_EQ(int64_t(read[int64_t(1)]), int64_t(0x5678ABCD));
}

} // namespace TestNetwSpawnHeader

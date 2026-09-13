#include "support/netw_test.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/comp_table.hpp"

using namespace godot;

namespace TestNetwCompTable {

using godot::PackedStringArray;
using godot::Ref;
using godot::String;
using netw::NetwCompTable;

PackedStringArray registered(std::initializer_list<const char *> p_paths) {
    PackedStringArray out;
    for (const char *path : p_paths) {
        out.push_back(String(path));
    }
    return out;
}

NetwCompTable table(std::initializer_list<const char *> p_paths) {
    NetwCompTable out;
    out.assign(registered(p_paths));
    return out;
}

String numbered(int64_t p_index) {
    char text[16];
    std::snprintf(text, sizeof(text), "Comp%03d", int(p_index));
    return String(text);
}

TEST_CASE(
    "[Networked][Comp][Hosted] two registration orders assign the same ids"
) {
    const NetwCompTable first = table({"Gun", "Body", "Wheel"});
    const NetwCompTable second = table({"Wheel", "Gun", "Body"});

    NETW_CHECK_EQ(first.id_for_path(String("Body")), 1);
    NETW_CHECK_EQ(first.id_for_path(String("Gun")), 2);
    NETW_CHECK_EQ(first.id_for_path(String("Wheel")), 3);
    for (int64_t id = 1; id <= 3; ++id) {
        CHECK(first.path_for_id(id) == second.path_for_id(id));
    }
}

TEST_CASE("[Networked][Comp][Hosted] an id round trips to its own path") {
    NetwCompTable held = table({"Body", "Gun"});
    NetwCompTable *const ids = &held;

    NETW_CHECK_EQ(ids->id_for_path(ids->path_for_id(1)), 1);
    CHECK(ids->has_path(String("Gun")));
    CHECK_FALSE(ids->has_path(String("Turret")));
    NETW_CHECK_EQ(ids->id_for_path(String("Turret")), 0);
    CHECK(ids->path_for_id(0).is_empty());
    CHECK(ids->path_for_id(9).is_empty());
}

TEST_CASE(
    "[Networked][Comp][Hosted] a path past the addressable range keeps its "
    "place and gains no id"
) {
    PackedStringArray many;
    for (int64_t i = 0; i < 300; ++i) {
        many.push_back(numbered(i));
    }
    NetwCompTable held;
    NetwCompTable *const ids = &held;
    ids->assign(many);

    NETW_CHECK_EQ(ids->sorted_paths().size(), 300);
    NETW_CHECK_EQ(ids->id_for_path(numbered(0)), 1);
    NETW_CHECK_EQ(ids->id_for_path(numbered(253)), 254);
    NETW_CHECK_EQ(ids->id_for_path(numbered(254)), 0);
    CHECK(ids->path_for_id(255).is_empty());
}

TEST_CASE("[Networked][Comp][Hosted] comp 0 is the entity root") {
    NetwCompTable held = table({"Gun"});
    NetwCompTable *const ids = &held;

    NETW_CHECK_EQ(ids->classify(0, String()), NetwCompTable::ADDRESS_ROOT);
}

TEST_CASE(
    "[Networked][Comp][Hosted] a fallback path that can leave the subtree is "
    "refused on its shape"
) {
    NetwCompTable held = table({"Gun"});
    NetwCompTable *const ids = &held;
    const int64_t fallback = 255;

    NETW_CHECK_EQ(
        ids->classify(fallback, String("Turret/Barrel")),
        NetwCompTable::ADDRESS_RELATIVE
    );
    NETW_CHECK_EQ(
        ids->classify(fallback, String("/root/Main")),
        NetwCompTable::ADDRESS_HOSTILE
    );
    NETW_CHECK_EQ(
        ids->classify(fallback, String("../Sibling")),
        NetwCompTable::ADDRESS_HOSTILE
    );
    NETW_CHECK_EQ(
        ids->classify(fallback, String("Turret/../../Sibling")),
        NetwCompTable::ADDRESS_HOSTILE
    );
    NETW_CHECK_EQ(
        ids->classify(fallback, String("res://scenes/main.tscn")),
        NetwCompTable::ADDRESS_HOSTILE
    );
    NETW_CHECK_EQ(
        ids->classify(fallback, String("user://save.dat")),
        NetwCompTable::ADDRESS_HOSTILE
    );
}

TEST_CASE("[Networked][Comp][Hosted] an id no path carries names nothing") {
    NetwCompTable held = table({"Gun"});
    NetwCompTable *const ids = &held;

    NETW_CHECK_EQ(ids->classify(1, String()), NetwCompTable::ADDRESS_MAPPED);
    NETW_CHECK_EQ(ids->classify(2, String()), NetwCompTable::ADDRESS_UNMAPPED);
}

TEST_CASE(
    "[Networked][Comp][Hosted] a poisoned table answers no id, including ones "
    "it holds"
) {
    NetwCompTable held = table({"Gun", "Body"});
    NetwCompTable *const ids = &held;
    ids->poisoned = true;

    NETW_CHECK_EQ(ids->classify(1, String()), NetwCompTable::ADDRESS_UNMAPPED);
    NETW_CHECK_EQ(ids->classify(2, String()), NetwCompTable::ADDRESS_UNMAPPED);
    NETW_CHECK_EQ(ids->classify(0, String()), NetwCompTable::ADDRESS_ROOT);
    NETW_CHECK_EQ(
        ids->classify(255, String("Gun")),
        NetwCompTable::ADDRESS_RELATIVE
    );
}

TEST_CASE(
    "[Networked][Comp][Hosted] authority authors the digest and a peer that "
    "disagrees with it poisons"
) {
    NetwCompTable authority = table({"Gun"});
    authority.table_hash = 4242;
    CHECK_FALSE(authority.reconcile(true));
    NETW_CHECK_EQ(authority.wire_hash, 4242);
    CHECK_FALSE(authority.poisoned);

    NetwCompTable agreeing = table({"Gun"});
    agreeing.table_hash = 4242;
    agreeing.wire_hash = 4242;
    CHECK_FALSE(agreeing.reconcile(false));
    CHECK_FALSE(agreeing.poisoned);

    NetwCompTable diverged = table({"Gun"});
    diverged.table_hash = 4242;
    diverged.wire_hash = 9999;
    CHECK(diverged.reconcile(false));
    CHECK(diverged.poisoned);
}

TEST_CASE(
    "[Networked][Comp][Hosted] an id the table cannot map resolves to nothing "
    "rather than to the entity root, because a row addressed by an id carries "
    "no path to fall back to"
) {
    Node *owner = memnew(Node);
    owner->set_name("Owner");
    Node *gun = memnew(Node);
    gun->set_name("Gun");
    owner->add_child(gun);
    NetwCompTable held = table({"Gun"});
    NetwCompTable *const ids = &held;

    NETW_CHECK_EQ(ids->resolve_node(owner, 1, String()), gun);
    NETW_CHECK_EQ(ids->resolve_node(owner, 0, String()), owner);
    NETW_CHECK_EQ(ids->resolve_node(owner, 2, String()), nullptr);

    ids->poisoned = true;
    NETW_CHECK_EQ(ids->resolve_node(owner, 1, String()), nullptr);
    NETW_CHECK_EQ(ids->resolve_node(owner, 0, String()), owner);
    NETW_CHECK_EQ(ids->resolve_node(owner, 255, String("Gun")), gun);

    memdelete(owner);
}

} // namespace TestNetwCompTable

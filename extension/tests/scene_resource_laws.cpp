#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/script.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSceneResourceLaws {

using namespace godot;
using netw::NetwSceneCore;

Ref<PackedScene> a_packed(const char *p_stem, const char *p_path) {
    Node *root = memnew(Node);
    root->set_name(p_stem);
    Ref<PackedScene> packed;
    packed.instantiate();
    packed->pack(root);
    if (p_path != nullptr) {
        packed->set_path(p_path);
    }
    memdelete(root);
    return packed;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR1 a PackedScene built in memory is its own "
    "destination kind, because it names nothing a second peer could load and "
    "the caller has to refuse rather than activate it"
) {
    const Ref<PackedScene> loose = a_packed("Arena", nullptr);
    const Ref<PackedScene> filed = a_packed("Arena", "res://levels/arena.tscn");

    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(loose)),
        int(NetwSceneCore::DESTINATION_PACKED_UNPATHED)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(filed)),
        int(NetwSceneCore::DESTINATION_PACKED)
    );
    CHECK(
        NetwSceneCore::destination_kind(loose)
        != NetwSceneCore::destination_kind(filed)
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] SR2 a name, a node and a nothing are three "
    "destination kinds, so one table answers every front door that takes a "
    "destination loosely"
) {
    Node *node = memnew(Node);
    Ref<RefCounted> plain;
    plain.instantiate();

    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(String("Arena"))),
        int(NetwSceneCore::DESTINATION_NAME)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(StringName("Arena"))),
        int(NetwSceneCore::DESTINATION_NAME)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(node)),
        int(NetwSceneCore::DESTINATION_NODE)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(Variant())),
        int(NetwSceneCore::DESTINATION_NONE)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(Variant(7))),
        int(NetwSceneCore::DESTINATION_NONE)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::destination_kind(plain)),
        int(NetwSceneCore::DESTINATION_NONE)
    );

    memdelete(node);
}

} // namespace TestNetwSceneResourceLaws

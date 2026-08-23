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
    const Ref<PackedScene> filed
        = a_packed("Arena", "res://levels/arena.tscn");

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

#if defined(NETW_TIER_HOSTED)

constexpr const char *MARKED_SCENE
    = "res://tests/support/scene/marked_test_scene.tscn";
constexpr const char *MARKED_SCENE_UID = "uid://b7n8w2marked01";
constexpr const char *MARKED_SCRIPT
    = "res://tests/support/scene/marked_test_scene.gd";

TEST_CASE(
    "[Networked][Scene] SR3 a reference that resolves to something "
    "other than a scene answers no scene, rather than a resource the caller "
    "would go on to instantiate"
) {
    CHECK(NetwSceneCore::packed_at(String()).is_null());
    CHECK(NetwSceneCore::packed_at(String(MARKED_SCRIPT)).is_null());

    const Ref<PackedScene> by_path = NetwSceneCore::packed_at(
        String(MARKED_SCENE)
    );
    REQUIRE(by_path.is_valid());
    CHECK(by_path->get_path() == String(MARKED_SCENE));
}

TEST_CASE(
    "[Networked][Scene] SR4 a uid reference names the same scene its "
    "path does, so a peer may request either spelling and the server loads "
    "one scene"
) {
    const Ref<PackedScene> by_uid = NetwSceneCore::packed_at(
        String(MARKED_SCENE_UID)
    );
    REQUIRE(by_uid.is_valid());
    CHECK(by_uid->get_path() == String(MARKED_SCENE));

    CHECK(
        NetwSceneCore::resolve_requested_path(String(MARKED_SCENE_UID))
        == String(MARKED_SCENE)
    );
    CHECK(
        NetwSceneCore::resolve_requested_path(String(MARKED_SCRIPT)).is_empty()
    );
    CHECK(NetwSceneCore::resolve_requested_path(String()).is_empty());
}

TEST_CASE(
    "[Networked][Scene] SR5 a scene's root script is read out of the "
    "packed state, so the mark authorizing a request costs no instantiation, "
    "and a root with properties but no script has no script"
) {
    CHECK(NetwSceneCore::packed_root_script(Ref<PackedScene>()).is_null());

    Node *bare = memnew(Node);
    bare->set_name("Arena");
    bare->set_process_priority(7);
    Ref<PackedScene> unscripted;
    unscripted.instantiate();
    unscripted->pack(bare);
    memdelete(bare);
    CHECK(NetwSceneCore::packed_root_script(unscripted).is_null());

    Ref<PackedScene> empty;
    empty.instantiate();
    CHECK(NetwSceneCore::packed_root_script(empty).is_null());

    const Ref<PackedScene> marked = NetwSceneCore::packed_at(
        String(MARKED_SCENE)
    );
    REQUIRE(marked.is_valid());
    const Ref<Script> root_script = NetwSceneCore::packed_root_script(marked);
    REQUIRE(root_script.is_valid());
    CHECK(root_script->get_path() == String(MARKED_SCRIPT));
}

TEST_CASE(
    "[Networked][Scene] SR6 reading a scene's root script by "
    "reference is the load and the read composed once, and a reference "
    "naming no scene reads no script rather than failing the caller"
) {
    CHECK(NetwSceneCore::scene_root_script_at(String()).is_null());
    CHECK(NetwSceneCore::scene_root_script_at(String(MARKED_SCRIPT)).is_null());

    const Ref<Script> by_path = NetwSceneCore::scene_root_script_at(
        String(MARKED_SCENE)
    );
    const Ref<Script> by_uid = NetwSceneCore::scene_root_script_at(
        String(MARKED_SCENE_UID)
    );
    REQUIRE(by_path.is_valid());
    CHECK(by_path == by_uid);
    CHECK(
        by_path
        == NetwSceneCore::packed_root_script(
            NetwSceneCore::packed_at(String(MARKED_SCENE))
        )
    );
}

#endif

} // namespace TestNetwSceneResourceLaws

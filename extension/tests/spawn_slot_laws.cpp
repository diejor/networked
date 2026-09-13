#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/scene_handle.hpp"
#include "netw/api/spawn_slot.hpp"

#if defined(NETW_TIER_HOSTED)
#include "support/loopback_rig.h"
#endif

namespace TestSpawnSlotLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwSceneHandle;
using netw::SpawnSlot;

struct DeclaredScene {
    Node *container = memnew(Node);
    Ref<netw::NetwEntity> wrapper;
    Ref<NetwSceneHandle> handle;

    DeclaredScene() {
        container->set_meta(NetwMultiplayer::scene_container_meta(), true);
        wrapper.instantiate();
        wrapper->set_owner(container);
        handle.instantiate();
        handle->bind(wrapper.ptr());
    }

    ~DeclaredScene() {
        memdelete(container);
    }
};

TEST_CASE(
    "[Networked][Session][Hosted] SL1 a slot built from nothing points "
    "nowhere, and "
    "handing it a player leaves that player where it was"
) {
    Ref<SpawnSlot> slot;
    slot.instantiate();

    CHECK_FALSE(slot->is_valid());
    CHECK_FALSE(slot->has_scene());
    CHECK(slot->get_scene().is_null());

    Node *player = memnew(Node);
    slot->place_player(player);
    CHECK(player->get_parent() == nullptr);
    memdelete(player);
}

TEST_CASE(
    "[Networked][Session][Hosted] SL2 a slot naming a parent adds the player "
    "under "
    "that parent, which is the route a tree with no declared scene takes"
) {
    Node *host = memnew(Node);
    const Ref<SpawnSlot> slot = SpawnSlot::under(host);

    CHECK(slot->is_valid());
    CHECK_FALSE(slot->has_scene());
    CHECK(slot->get_scene().is_null());

    Node *player = memnew(Node);
    slot->place_player(player);
    CHECK(player->get_parent() == host);
    NETW_CHECK_EQ(host->get_child_count(), 1);
    if (player->get_parent() == nullptr) {
        memdelete(player);
    }
    memdelete(host);
}

TEST_CASE(
    "[Networked][Session][Hosted] SL3 a slot outliving its parent reports "
    "itself "
    "invalid rather than placing a player into a freed node"
) {
    Node *host = memnew(Node);
    const Ref<SpawnSlot> slot = SpawnSlot::under(host);
    REQUIRE(slot->is_valid());
    memdelete(host);

    CHECK_FALSE(slot->is_valid());

    Node *player = memnew(Node);
    slot->place_player(player);
    CHECK(player->get_parent() == nullptr);
    memdelete(player);
}

TEST_CASE(
    "[Networked][Session][Hosted] SL4 a slot carries a scene only while that "
    "scene is "
    "declared, so an undeclared handle is the same as no scene at all"
) {
    Ref<NetwSceneHandle> bare;
    bare.instantiate();
    const Ref<SpawnSlot> undeclared = SpawnSlot::in_scene(bare);
    CHECK_FALSE(undeclared->has_scene());
    CHECK(undeclared->get_scene().is_null());
    CHECK_FALSE(undeclared->is_valid());

    const Ref<SpawnSlot> nothing = SpawnSlot::in_scene(Ref<NetwSceneHandle>());
    CHECK_FALSE(nothing->has_scene());
    CHECK_FALSE(nothing->is_valid());

    DeclaredScene scene;
    REQUIRE(scene.handle->get_is_declared());
    const Ref<SpawnSlot> declared = SpawnSlot::in_scene(scene.handle);

    CHECK(declared->has_scene());
    CHECK(declared->is_valid());
    CHECK(declared->get_scene() == scene.handle);
}

TEST_CASE(
    "[Networked][Session][Hosted] SL5 asking no session at all answers a slot "
    "that rejects itself, so a spawner tests the answer rather than handling "
    "an error"
) {
    const Ref<SpawnSlot> without_session
        = SpawnSlot::for_scene(nullptr, StringName("Arena"));
    REQUIRE(without_session.is_valid());
    CHECK_FALSE(without_session->has_scene());
    CHECK_FALSE(without_session->is_valid());
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Session] SL6 a live session that declares no scene under the "
    "requested stem answers the same rejecting slot, which is what makes one "
    "spawner right for a declared scene and a plain branch alike"
) {
    netw_test::LoopbackRig rig(0);
    rig.mount();
    REQUIRE(rig.server()->is_online());

    const Ref<SpawnSlot> undeclared
        = SpawnSlot::for_scene(rig.server(), StringName("NoSuchScene"));
    REQUIRE(undeclared.is_valid());
    CHECK_FALSE(undeclared->has_scene());
    CHECK_FALSE(undeclared->is_valid());
}

#endif

} // namespace TestSpawnSlotLaws

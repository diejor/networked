#include "support/netw_test.h"

#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwSessionJoinLaws {

using namespace godot;
using netw_test::LoopbackRig;

constexpr const char *LABELS = "_scene_nodes_by_label";

Object *participant_of(Object *p_api, int p_peer) {
    return Object::cast_to<Object>(
        p_api->call("peer_get_participant", p_peer)
    );
}

TEST_CASE("[Networked][Session] a connected peer holds no participant until "
          "it joins") {
    LoopbackRig rig(1);
    rig.pump(4);

    CHECK(Object::cast_to<Object>(rig.client(0)->get("local_participant"))
          == nullptr);
    CHECK(participant_of(rig.server(), rig.peer_id(0)) == nullptr);

    Object *seated = rig.join(0, StringName("alice"));

    CHECK(seated != nullptr);
    CHECK(participant_of(rig.server(), rig.peer_id(0)) != nullptr);
}

TEST_CASE("[Networked][Session] a host joins itself and is seated the same "
          "way a client is") {
    LoopbackRig rig(1);
    rig.pump(4);

    Object *seated = rig.join(-1, StringName("host"));

    CHECK(seated != nullptr);
    CHECK(participant_of(rig.server(), 1) != nullptr);
    CHECK(Object::cast_to<Object>(rig.client(0)->get("local_participant"))
          == nullptr);
}

TEST_CASE("[Networked][Session] every joined peer reaches every peer's "
          "roster") {
    LoopbackRig rig(2);
    rig.pump(4);

    rig.join(-1, StringName("host"));
    rig.join(0, StringName("alice"));
    rig.join(1, StringName("bob"));
    rig.pump(4);

    for (int at = -1; at < 2; ++at) {
        Object *api = at < 0 ? rig.server() : rig.client(at);
        CHECK(participant_of(api, 1) != nullptr);
        CHECK(participant_of(api, rig.peer_id(0)) != nullptr);
        CHECK(participant_of(api, rig.peer_id(1)) != nullptr);
    }
}

TEST_CASE("[Networked][Session] a joined peer's participant carries the name "
          "its payload named") {
    LoopbackRig rig(1);
    rig.pump(4);

    Object *seated = rig.join(0, StringName("alice"));

    REQUIRE(seated != nullptr);
    CHECK(StringName(seated->get("username")) == StringName("alice"));
}

TEST_CASE("[Networked][Session] a declared scene is not a live one until it "
          "enters") {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);

    Object *scenes = rig.server();
    REQUIRE(scenes != nullptr);
    NETW_CHECK_EQ(int(Dictionary(scenes->call(LABELS)).size()), 0);

    rig.enter_scene(StringName("Arena"));

    NETW_CHECK_EQ(int(Dictionary(scenes->call(LABELS)).size()), 1);
    CHECK(Dictionary(scenes->call(LABELS)).has(StringName("Arena")));
}

TEST_CASE("[Networked][Session] two instances of one stem are two live "
          "scenes and the stem answers with the later one") {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("first"), StringName("Arena"));
    rig.declare_scene(StringName("second"), StringName("Arena"));
    rig.pump(4);

    rig.enter_scene(StringName("first"));
    rig.enter_scene(StringName("second"));

    Object *scenes = rig.server();
    Object *core = Object::cast_to<Object>(scenes->get("_scene_core"));
    REQUIRE(core != nullptr);
    NETW_CHECK_EQ(int(Array(core->call("live_scenes")).size()), 2);
    NETW_CHECK_EQ(int(Dictionary(scenes->call(LABELS)).size()), 1);
    CHECK(
        RID(core->call("scene_named", StringName("Arena")))
        == rig.entity_of(StringName("second"))
    );
}

TEST_CASE("[Networked][Session] a client mirrors a scene without being "
          "admitted to it, and holds no seat there") {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));
    rig.join(-1, StringName("host"));
    rig.join(0, StringName("alice"));
    rig.mirror_scene(0, StringName("Arena"));
    rig.pump(8);

    Object *client_scenes = rig.client(0);
    REQUIRE(client_scenes != nullptr);
    Object *seated
        = Object::cast_to<Object>(rig.client(0)->get("local_participant"));
    REQUIRE(seated != nullptr);

    NETW_CHECK_EQ(int(Dictionary(client_scenes->call(LABELS)).size()), 1);
    CHECK(Dictionary(client_scenes->call(LABELS)).has(StringName("Arena")));
    CHECK(Object::cast_to<Object>(seated->get("current_scene")) == nullptr);
}

TEST_CASE("[Networked][Session] a mirrored scene is the server's scene, not a "
          "second one") {
    LoopbackRig rig(1);
    rig.declare_scene(StringName("Arena"));
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));

    const RID origin = rig.entity_of(StringName("Arena"));
    const RID mirror = rig.mirror_scene(0, StringName("Arena"));

    NETW_CHECK_EQ(
        int(rig.client(0)->call("entity_get_route", mirror)),
        int(rig.server()->call("entity_get_route", origin))
    );
}

} // namespace TestNetwSessionJoinLaws

#endif

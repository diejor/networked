#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestSceneSessionVerbLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Placed {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Placed place(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    bool p_declares_scene
) {
    Placed made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record = made.wrapper->get_record();
    made.record->adopt_handle(made.handle);
    made.record->set_declares_scene(p_declares_scene);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.owner
    ));
    made.owner->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    if (p_declares_scene) {
        Node *level = memnew(Node);
        level->set_name("Level");
        made.owner->add_child(level);
        p_core->get_scene_core()
            ->scene_enter(made.handle, StringName("Arena"), false);
    }
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV1 admitting names the scene's node, so a "
    "scene whose node is gone refuses rather than admitting a peer to a "
    "boundary nothing stands behind"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);

    REQUIRE(core->is_server());
    NETW_CHECK_EQ(
        int(core->scene_admit(arena.handle, 0)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(int(core->scene_admit(RID(), 7)), int(ERR_DOES_NOT_EXIST));
    CHECK_FALSE(core->scene_admits(arena.handle, 7));

    NETW_CHECK_EQ(int(core->scene_admit(arena.handle, 7)), int(OK));
    CHECK(core->scene_admits(arena.handle, 7));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV2 releasing tells the peer before it takes "
    "the seat away, because a client learns membership from the seat that "
    "names what it is being released from"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    REQUIRE(int(core->scene_admit(arena.handle, 7)) == int(OK));
    REQUIRE(core->scene_admits(arena.handle, 7));

    CHECK(core->scene_release(arena.handle, 7));

    CHECK_FALSE(core->scene_admits(arena.handle, 7));

    CHECK_FALSE(core->scene_release(RID(), 7));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV3 the entity walk stops at a nested scene, "
    "so a scene reports its own members and never the members of a scene it "
    "merely contains"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);
    const Placed annex = place(core, arena.owner, true);
    const Placed inner = place(core, annex.owner, false);

    const TypedArray<RID> members = core->scene_entities_under(arena.handle);

    NETW_CHECK_EQ(int(members.size()), 2);
    CHECK(members.has(pawn.handle));
    CHECK(members.has(annex.handle));
    CHECK_FALSE(members.has(inner.handle));
    CHECK_FALSE(members.has(arena.handle));

    const TypedArray<RID> nested = core->scene_entities_under(annex.handle);

    NETW_CHECK_EQ(int(nested.size()), 1);
    CHECK(nested.has(inner.handle));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV4 an entity that owns no record of its own "
    "is walked through rather than reported, so a plain node between a scene "
    "and its members hides nobody"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    Node *plain = memnew(Node);
    arena.owner->add_child(plain);
    const Placed deep = place(core, plain, false);

    const TypedArray<RID> members = core->scene_entities_under(arena.handle);

    NETW_CHECK_EQ(int(members.size()), 1);
    CHECK(members.has(deep.handle));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV5 declaring is a fact on the record, so an "
    "entity the session never bound answers false rather than erroring"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);

    CHECK(core->scene_is_declared(arena.handle));
    CHECK_FALSE(core->scene_is_declared(pawn.handle));
    CHECK_FALSE(core->scene_is_declared(RID()));

    CHECK(core->scene_entity_node(arena.handle) == arena.owner);
    CHECK(core->scene_entity_node(RID()) == nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV6 an admission commits the interest matrix "
    "in the same act, so the peer it just admitted can see the scene without "
    "waiting for a settle nobody is obliged to drive"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);

    REQUIRE(int(core->scene_admit(arena.handle, 7)) == int(OK));
    CHECK_FALSE(core->interest_flush_pending());

    SUBCASE("a refused admission leaves nothing pending either") {
        NETW_CHECK_EQ(
            int(core->scene_admit(RID(), 9)),
            int(ERR_DOES_NOT_EXIST)
        );
        CHECK_FALSE(core->interest_flush_pending());
    }

    memdelete(root);
}

} // namespace TestSceneSessionVerbLaws

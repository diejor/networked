#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneMembershipLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwSceneCore;
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
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM1 an entity crossing a scene boundary "
    "dispatches once against the scene it resolves to, and one carrying a "
    "peer dispatches a second time as a player, so an observer watching only "
    "players hears the crossing without filtering the entity feed"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);
    const Placed prop = place(core, arena.owner, false);
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog heard;
    scenes->observe(
        arena.handle,
        NetwSceneCore::EVENT_ENTITY,
        heard.callable("entity")
    );
    scenes->observe(
        arena.handle,
        NetwSceneCore::EVENT_PLAYER,
        heard.callable("player")
    );

    REQUIRE(core->scene_of(prop.handle) == arena.handle);

    CHECK(
        core->scene_report_entity_edge(prop.handle, true, false) == arena.handle
    );

    NETW_CHECK_EQ(heard.count("entity"), 1);
    NETW_CHECK_EQ(heard.count("player"), 0);
    CHECK(bool(heard.args("entity")[0]));
    CHECK(RID(heard.args("entity")[1]) == prop.handle);

    CHECK(
        core->scene_report_entity_edge(pawn.handle, false, true) == arena.handle
    );

    NETW_CHECK_EQ(heard.count("entity"), 2);
    NETW_CHECK_EQ(heard.count("player"), 1);
    CHECK_FALSE(bool(heard.args("player")[0]));
    CHECK(RID(heard.args("player")[1]) == pawn.handle);
    CHECK(RID(heard.args("entity", 1)[1]) == pawn.handle);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM2 a subject that resolves to no scene, and "
    "a scene container that resolves to itself, dispatch nothing and answer "
    "an invalid RID, so a scene standing in the tree is not an arrival in "
    "itself"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed loose = place(core, root, false);
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog heard;
    scenes->observe(
        arena.handle,
        NetwSceneCore::EVENT_ENTITY,
        heard.callable("entity")
    );
    scenes->observe(
        arena.handle,
        NetwSceneCore::EVENT_PLAYER,
        heard.callable("player")
    );

    REQUIRE(core->scene_of(arena.handle) == arena.handle);
    REQUIRE_FALSE(core->scene_of(loose.handle).is_valid());

    CHECK_FALSE(
        core->scene_report_entity_edge(arena.handle, true, false).is_valid()
    );
    CHECK_FALSE(
        core->scene_report_entity_edge(arena.handle, true, true).is_valid()
    );
    CHECK_FALSE(
        core->scene_report_entity_edge(loose.handle, true, false).is_valid()
    );
    CHECK_FALSE(core->scene_report_entity_edge(RID(), true, true).is_valid());

    NETW_CHECK_EQ(heard.count("entity"), 0);
    NETW_CHECK_EQ(heard.count("player"), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SM3 the scene is resolved at the moment of "
    "the edge rather than remembered, so an entity reparented between two "
    "scenes reports its leave against the scene it is still under and its "
    "arrival against the one it reached"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed annex = place(core, root, true);
    const Placed pawn = place(core, arena.owner, false);
    const Ref<NetwSceneCore> scenes = core->get_scene_core();
    const CallLog heard;
    scenes->observe(
        arena.handle,
        NetwSceneCore::EVENT_PLAYER,
        heard.callable("arena")
    );
    scenes->observe(
        annex.handle,
        NetwSceneCore::EVENT_PLAYER,
        heard.callable("annex")
    );

    CHECK(
        core->scene_report_entity_edge(pawn.handle, false, true) == arena.handle
    );

    NETW_CHECK_EQ(heard.count("arena"), 1);
    NETW_CHECK_EQ(heard.count("annex"), 0);
    CHECK_FALSE(bool(heard.args("arena")[0]));

    arena.owner->remove_child(pawn.owner);
    annex.owner->add_child(pawn.owner);

    CHECK(
        core->scene_report_entity_edge(pawn.handle, true, true) == annex.handle
    );

    NETW_CHECK_EQ(heard.count("arena"), 1);
    NETW_CHECK_EQ(heard.count("annex"), 1);
    CHECK(bool(heard.args("annex")[0]));

    memdelete(root);
}

} // namespace TestNetwSceneMembershipLaws

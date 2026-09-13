#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/scene_handle.hpp"

namespace TestParticipantTravelLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayer;
using netw::NetwParticipant;
using netw::NetwPromise;
using netw::NetwSceneHandle;

struct Destination {
    Node *root = nullptr;
    RID handle;
    Ref<NetwSceneHandle> view;
};

Destination a_scene(LoopbackRig &p_rig, const StringName &p_stem, bool p_live) {
    NetwMultiplayer *api = p_rig.server();
    Destination made;
    made.root = memnew(Node);
    made.root->set_name(p_stem);
    p_rig.branch()->add_child(made.root);

    made.handle = api->entity_create();
    REQUIRE(made.handle.is_valid());
    NETW_CHECK_EQ(int(api->scene_declare(made.handle)), int(godot::OK));
    NETW_CHECK_GT(int(api->entity_admit(made.handle)), 0);
    NETW_CHECK_EQ(
        int(api->entity_bind_node(made.handle, made.root)),
        int(godot::OK)
    );
    api->scene_set_param(
        made.handle,
        NetwMultiplayer::SCENE_PARAM_LABEL,
        p_stem
    );
    if (p_live) {
        api->scene_root_online(made.root);
    }
    p_rig.pump();

    const Ref<netw::NetwEntity> seated = netw::NetwEntity::of(made.root);
    REQUIRE(seated.is_valid());
    made.view = seated->get_scene();
    REQUIRE(made.view.is_valid());
    return made;
}

void retire(Node *p_root) {
    if (p_root != nullptr && p_root->get_parent() != nullptr) {
        p_root->get_parent()->remove_child(p_root);
        memdelete(p_root);
    }
}

int refusal_of(const Ref<NetwPromise> &p_answer) {
    REQUIRE(p_answer.is_valid());
    CHECK(p_answer->get_is_settled());
    return p_answer->get_code();
}

TEST_CASE(
    "[Networked][Scene] PT1 travel answers a settled promise for every "
    "refusal, so a caller awaits one thing rather than testing for a null "
    "before it awaits"
) {
    LoopbackRig rig(0);
    rig.mount();
    const Destination arena = a_scene(rig, StringName("Arena"), true);

    NETW_CHECK_EQ(
        refusal_of(
            rig.server()->participant_travel(Ref<NetwParticipant>(), arena.view)
        ),
        int(ERR_INVALID_PARAMETER)
    );

    const Ref<NetwParticipant> traveller = rig.server()->participant_ensure(7);
    REQUIRE(traveller.is_valid());
    NETW_CHECK_EQ(
        refusal_of(
            rig.server()->participant_travel(traveller, Ref<NetwSceneHandle>())
        ),
        int(ERR_INVALID_PARAMETER)
    );

    retire(arena.root);
}

TEST_CASE(
    "[Networked][Scene] PT2 travel names a destination this session actually "
    "holds, so a handle to a scene that never went live is refused rather "
    "than seating a participant nowhere"
) {
    LoopbackRig rig(0);
    rig.mount();
    const Destination absent = a_scene(rig, StringName("Elsewhere"), false);
    const Ref<NetwParticipant> traveller = rig.server()->participant_ensure(7);

    NETW_CHECK_EQ(
        refusal_of(rig.server()->participant_travel(traveller, absent.view)),
        int(ERR_UNAVAILABLE)
    );

    retire(absent.root);
}

TEST_CASE(
    "[Networked][Scene] PT3 travel moves a peer this session has ADMITTED, "
    "so a peer it never admitted is refused rather than given a seat by the "
    "act of being moved"
) {
    LoopbackRig rig(0);
    rig.mount();
    const Destination arena = a_scene(rig, StringName("Arena"), true);
    const Ref<NetwParticipant> stranger
        = rig.server()->participant_ensure(4242);

    NETW_CHECK_EQ(
        refusal_of(rig.server()->participant_travel(stranger, arena.view)),
        int(ERR_UNAUTHORIZED)
    );

    retire(arena.root);
}

TEST_CASE(
    "[Networked][Scene] PT4 travel is an authority operation, so a client "
    "calling it directly is refused rather than moving a seat only its own "
    "peer would ever believe"
) {
    LoopbackRig rig(1);
    rig.mount();
    const Destination arena = a_scene(rig, StringName("Arena"), true);
    const Ref<NetwParticipant> traveller = rig.client(0)->participant_ensure(7);

    NETW_CHECK_EQ(
        refusal_of(rig.client(0)->participant_travel(traveller, arena.view)),
        int(ERR_UNAUTHORIZED)
    );

    retire(arena.root);
}

} // namespace TestParticipantTravelLaws

#endif

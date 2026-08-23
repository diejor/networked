#include "support/netw_test.h"

#include "netw/scene_core.hpp"

namespace TestNetwSceneReachLaws {

using namespace godot;
using netw::NetwSceneCore;

Ref<NetwSceneCore> at_reach(int p_reach) {
    Ref<NetwSceneCore> core;
    core.instantiate();
    core->set_request_reach(p_reach);
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW1 a scene declared session-wide replaces "
    "the session at either reach, so the mark outranks the setting"
) {
    Ref<NetwSceneCore> participant_reach =
        at_reach(NetwSceneCore::REACH_PARTICIPANT);
    Ref<NetwSceneCore> session_reach = at_reach(NetwSceneCore::REACH_SESSION);

    CHECK(participant_reach->change_replaces_session(true, true));
    CHECK(session_reach->change_replaces_session(true, true));

    CHECK_FALSE(participant_reach->change_replaces_session(false, true));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW2 a change with no participant to move "
    "names no smaller reach than the session"
) {
    Ref<NetwSceneCore> core = at_reach(NetwSceneCore::REACH_PARTICIPANT);

    CHECK(core->change_replaces_session(false, false));
    CHECK_FALSE(core->change_replaces_session(false, true));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SW3 the reach is read at the change rather "
    "than remembered, so raising it and lowering it both take effect at once"
) {
    Ref<NetwSceneCore> core = at_reach(NetwSceneCore::REACH_PARTICIPANT);
    CHECK_FALSE(core->change_replaces_session(false, true));

    core->set_request_reach(NetwSceneCore::REACH_SESSION);
    CHECK(core->change_replaces_session(false, true));

    core->set_request_reach(NetwSceneCore::REACH_PARTICIPANT);
    CHECK_FALSE(core->change_replaces_session(false, true));
}

} // namespace TestNetwSceneReachLaws

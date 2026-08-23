#include "support/netw_test.h"

#include "netw/scene_core.hpp"

namespace TestSceneMoveVerdictLaws {

using namespace godot;
using netw::NetwSceneCore;

TEST_CASE(
    "[Networked][Scene][Hosted] MV1 a move refuses before it resolves a "
    "destination, so a dead mover never activates a scene to arrive in"
) {
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(false, false, false)),
        int(NetwSceneCore::MOVE_REFUSED)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(false, true, false)),
        int(NetwSceneCore::MOVE_REFUSED)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(false, true, true)),
        int(NetwSceneCore::MOVE_REFUSED)
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] MV2 a live mover with no destination "
    "refuses, and refusal outranks already being there"
) {
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(true, false, false)),
        int(NetwSceneCore::MOVE_REFUSED)
    );
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(true, false, true)),
        int(NetwSceneCore::MOVE_REFUSED)
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] MV3 a mover already in the destination "
    "answers without carrying, so no physics frame is spent to stand still"
) {
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(true, true, true)),
        int(NetwSceneCore::MOVE_ALREADY_THERE)
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] MV4 only a live mover bound for a different "
    "live destination carries, which is the one verdict that awaits"
) {
    NETW_CHECK_EQ(
        int(NetwSceneCore::move_verdict(true, true, false)),
        int(NetwSceneCore::MOVE_CARRY)
    );
}

} // namespace TestSceneMoveVerdictLaws

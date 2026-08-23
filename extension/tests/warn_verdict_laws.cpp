#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/netw_multiplayer.hpp"

namespace TestWarnVerdictLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayerCore;

NetwMultiplayerCore *session_core(Object *p_api) {
    NetwMultiplayerCore *core = Object::cast_to<NetwMultiplayerCore>(
        p_api->get("_native_core")
    );
    REQUIRE(core != nullptr);
    return core;
}

TEST_CASE(
    "[Networked][Session] WV1 warning a verdict counts every "
    "occurrence and admits only the first, so a peer retrying cannot flood"
) {
    LoopbackRig rig(0);
    NetwMultiplayerCore *core = session_core(rig.server());

    const int64_t before = core->verdict_total(ERR_UNAUTHORIZED);

    NETW_CHECK_EQ(int(core->warn_verdict(ERR_UNAUTHORIZED, 0)), 1);
    NETW_CHECK_EQ(int(core->warn_verdict(ERR_UNAUTHORIZED, 0)), 0);
    NETW_CHECK_EQ(int(core->warn_verdict(ERR_UNAUTHORIZED, 0)), 0);

    NETW_CHECK_EQ(core->verdict_total(ERR_UNAUTHORIZED), before + 3);
}

TEST_CASE(
    "[Networked][Session] WV2 a warning is claimed per verdict and "
    "route together, so one route falling silent does not silence another"
) {
    LoopbackRig rig(0);
    NetwMultiplayerCore *core = session_core(rig.server());

    NETW_CHECK_EQ(int(core->warn_verdict(ERR_UNAUTHORIZED, 7)), 1);
    NETW_CHECK_EQ(int(core->warn_verdict(ERR_UNAUTHORIZED, 7)), 0);
    NETW_CHECK_EQ(int(core->warn_verdict(ERR_UNAUTHORIZED, 9)), 1);
}

} // namespace TestWarnVerdictLaws

#endif

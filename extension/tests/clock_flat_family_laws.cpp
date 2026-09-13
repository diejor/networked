#include "support/netw_test.h"

#include "netw/api/clock_config.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestClockFlatFamilyLaws {

using namespace godot;
using netw::NetwClockConfig;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

Ref<NetwClockConfig> config_at(int64_t p_tickrate) {
    Ref<NetwClockConfig> config;
    config.instantiate();
    config->set_tickrate(p_tickrate);
    return config;
}

TEST_CASE(
    "[Networked][Clock][Hosted] CF1 tickrate is the one clock knob every peer "
    "must "
    "agree on, so registration is its only door and the param table refuses it"
) {
    const Ref<NetwMultiplayer> core = session();

    NETW_CHECK_EQ(int(core->clock_configure(config_at(48)) == OK), 1);
    NETW_CHECK_EQ(
        int(core->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE)),
        48
    );

    NETW_CHECK_EQ(
        int(core->clock_set_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE, 12)
            == ERR_UNAUTHORIZED),
        1
    );
    NETW_CHECK_EQ(
        int(core->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE)),
        48
    );
}

TEST_CASE(
    "[Networked][Clock][Hosted] CF2 every knob but the tickrate round-trips "
    "through "
    "the param table, so a game tunes the clock live without a re-registration"
) {
    const Ref<NetwMultiplayer> core = session();

    core->clock_set_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET, 7);
    core->clock_set_param(NetwMultiplayer::CLOCK_PARAM_MANUAL_TICK, true);
    core->clock_set_param(
        NetwMultiplayer::CLOCK_PARAM_SYNC_MODE,
        int(NetwMultiplayer::SYNC_MODE_SNAP)
    );
    core->clock_set_param(
        NetwMultiplayer::CLOCK_PARAM_STRETCH_NUDGE_FACTOR,
        0.25
    );

    NETW_CHECK_EQ(
        int(core->clock_get_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET)),
        7
    );
    NETW_CHECK_EQ(
        int(bool(
            core->clock_get_param(NetwMultiplayer::CLOCK_PARAM_MANUAL_TICK)
        )),
        1
    );
    NETW_CHECK_EQ(
        int(core->clock_get_param(NetwMultiplayer::CLOCK_PARAM_SYNC_MODE)),
        int(NetwMultiplayer::SYNC_MODE_SNAP)
    );
    NETW_CHECK_CLOSE(
        double(core->clock_get_param(
            NetwMultiplayer::CLOCK_PARAM_STRETCH_NUDGE_FACTOR
        )),
        0.25,
        0.0001
    );
}

TEST_CASE(
    "[Networked][Clock][Hosted] CF3 the driver verbs move the one clock the "
    "session "
    "owns, so a test tier drives ticks without reaching past the flat surface"
) {
    const Ref<NetwMultiplayer> core = session();
    core->clock_configure(config_at(30));

    const int64_t before = core->clock_get_tick();
    core->clock_step(3);
    NETW_CHECK_EQ(core->clock_get_tick() - before, 3);

    NETW_CHECK_EQ(int(core->clock_is_gated()), 0);
    core->clock_set_gate(true);
    NETW_CHECK_EQ(int(core->clock_is_gated()), 1);
    core->clock_set_gate(false);
    NETW_CHECK_EQ(int(core->clock_is_gated()), 0);

    NETW_CHECK_EQ(int(core->clock_is_synchronized()), 0);
    core->clock_set_synchronized(true);
    NETW_CHECK_EQ(int(core->clock_is_synchronized()), 1);
}

TEST_CASE(
    "[Networked][Clock][Hosted] CF4 the monitors read the same engine the "
    "typed reads "
    "do, so cadence needs no fixed-key dictionary to leave the core"
) {
    const Ref<NetwMultiplayer> core = session();
    core->clock_configure(config_at(50));

    NETW_CHECK_CLOSE(
        core->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_TICKTIME),
        1.0 / 50.0,
        0.0001
    );
    NETW_CHECK_CLOSE(
        core->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_RTT),
        0.0,
        0.0001
    );
    NETW_CHECK_CLOSE(
        core->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_POLLS),
        0.0,
        0.0001
    );

    core->clock_ingest_pong(0.2, 0, 0.0, false);

    NETW_CHECK_CLOSE(
        core->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_RTT),
        0.2,
        0.0001
    );
    NETW_CHECK_CLOSE(
        core->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_ONE_WAY_LATENCY),
        0.1,
        0.0001
    );
}

TEST_CASE(
    "[Networked][Clock][Hosted] CF5 the session answers the clock it is "
    "actually running, not the Resource that registered it, so a knob turned "
    "since is read back off the same door"
) {
    const Ref<NetwMultiplayer> core = session();

    const Ref<NetwClockConfig> config = config_at(24);
    core->clock_configure(config);

    const Ref<NetwClockConfig> effective = core->clock_get_config();
    REQUIRE(effective.is_valid());
    CHECK(effective != config);
    NETW_CHECK_EQ(effective->get_tickrate(), int64_t(24));

    SUBCASE(
        "a knob turned since reads back through the same door, and the "
        "authored Resource is not retained as a second truth"
    ) {
        const int64_t authored = config->get_display_offset();
        core->clock_set_param(
            NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET,
            authored + 7
        );
        NETW_CHECK_EQ(
            core->clock_get_config()->get_display_offset(),
            authored + 7
        );
        NETW_CHECK_EQ(config->get_display_offset(), authored);
    }

    SUBCASE("a refused registration changes nothing") {
        NETW_CHECK_EQ(
            int(core->clock_configure(Ref<NetwClockConfig>())
                == ERR_INVALID_PARAMETER),
            1
        );
        NETW_CHECK_EQ(core->clock_get_config()->get_tickrate(), int64_t(24));
    }
}

} // namespace TestClockFlatFamilyLaws

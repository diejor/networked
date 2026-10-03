#include "support/netw_test.h"

#include "support/netw_recorder.h"

#include "netw/api/clock.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwClockDoor {

using namespace godot;
using netw::Netw;
using netw::NetwClock;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C1 one session mints one clock door, "
    "because a session has exactly one clock to hand out"
) {
    Ref<NetwMultiplayer> session = make_session();

    const Ref<NetwClock> door = session->get_clock();
    REQUIRE(door.is_valid());
    CHECK(door == session->get_clock());
    CHECK(make_session()->get_clock() != door);
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C2 every read on the clock door "
    "answers what the flat verb answers, settings and monitor included"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwClock> door = session->get_clock();

    NETW_CHECK_EQ(door->get_tick(), session->clock_get_tick());
    NETW_CHECK_EQ(
        door->get_is_synchronized(),
        session->clock_is_synchronized()
    );
    NETW_CHECK_EQ(door->get_is_configured(), session->clock_is_configured());
    NETW_CHECK_EQ(
        door->get_behind_count(),
        session->clock_get_simulation_behind_count()
    );
    NETW_CHECK_ORDER(
        door->monitor(NetwMultiplayer::CLOCK_MONITOR_RTT),
        session->clock_get_monitor(NetwMultiplayer::CLOCK_MONITOR_RTT),
        ==
    );
    NETW_CHECK_EQ(
        door->get_tickrate(),
        int64_t(session->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE))
    );
    NETW_CHECK_EQ(
        door->get_display_offset(),
        int64_t(session->clock_get_param(
            NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET
        ))
    );
    NETW_CHECK_EQ(door->get_sync_mode(), NetwMultiplayer::SYNC_MODE_STRETCH);
    NETW_CHECK_ORDER(
        door->get_ping_interval(),
        double(
            session->clock_get_param(NetwMultiplayer::CLOCK_PARAM_PING_INTERVAL)
        ),
        ==
    );
    NETW_CHECK_ORDER(
        door->get_physics_factor(),
        session->clock_get_physics_factor(),
        ==
    );
    NETW_CHECK_ORDER(
        door->get_tick_factor(),
        session->clock_get_tick_factor(),
        ==
    );
    NETW_CHECK_ORDER(
        door->get_tick_phase(),
        session->clock_get_tick_phase(),
        ==
    );
    NETW_CHECK_EQ(
        door->get_recommended_display_offset(),
        session->clock_get_recommended_display_offset()
    );
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C3 a setting written through the door "
    "is the same setting the flat verb reads back"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwClock> door = session->get_clock();

    door->set_display_offset(3);
    NETW_CHECK_EQ(
        int64_t(session->clock_get_param(
            NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET
        )),
        3
    );
    NETW_CHECK_EQ(door->get_display_offset(), 3);

    door->set_sync_mode(NetwMultiplayer::SYNC_MODE_SNAP);
    NETW_CHECK_EQ(
        int64_t(
            session->clock_get_param(NetwMultiplayer::CLOCK_PARAM_SYNC_MODE)
        ),
        NetwMultiplayer::SYNC_MODE_SNAP
    );

    door->set_ping_interval(0.25);
    NETW_CHECK_ORDER(
        double(
            session->clock_get_param(NetwMultiplayer::CLOCK_PARAM_PING_INTERVAL)
        ),
        0.25,
        ==
    );

    SUBCASE("the tickrate the declaration owns has no setter to reach it") {
        const int64_t declared = door->get_tickrate();
        door->set(StringName("tickrate"), 45);
        NETW_CHECK_EQ(door->get_tickrate(), declared);
    }
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C4 the door re-emits the tick edges, "
    "which is the whole reason it is a class rather than three statics"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwClock> door = session->get_clock();

    netw_test::Recorder heard(
        door.ptr(),
        {StringName("before_tick"),
         StringName("ticked"),
         StringName("synchronized")}
    );

    const int64_t tick = session->clock_engine().get_tick();
    session->clock_engine().force_step(1);

    NETW_REQUIRE_EQ(heard.count(StringName("before_tick")), 1);
    NETW_REQUIRE_EQ(heard.count(StringName("ticked")), 1);
    CHECK(
        heard.order()
        == Vector<StringName>({StringName("before_tick"), StringName("ticked")})
    );
    NETW_CHECK_EQ(int64_t(heard.args(StringName("ticked"))[1]), tick);

    session->clock_event(netw::ClockEngine::EVENT_SYNCHRONIZED, 0, 0);
    NETW_CHECK_EQ(heard.count(StringName("synchronized")), 1);
    NETW_CHECK_ORDER(
        double(heard.args(StringName("before_tick"))[0]),
        session->clock_engine().ticktime(),
        ==
    );
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C5 a clock door outliving its session "
    "answers a stopped clock rather than dereferencing what is gone"
) {
    Ref<NetwClock> door;
    {
        Ref<NetwMultiplayer> session = make_session();
        door = session->get_clock();
    }

    NETW_CHECK_EQ(door->get_tick(), 0);
    CHECK_FALSE(door->get_is_synchronized());
    CHECK_FALSE(door->get_is_configured());
    NETW_CHECK_EQ(door->get_behind_count(), 0);
    NETW_CHECK_ORDER(
        door->monitor(NetwMultiplayer::CLOCK_MONITOR_RTT),
        0.0,
        ==
    );
    NETW_CHECK_EQ(door->get_tickrate(), 0);
    NETW_CHECK_EQ(door->get_display_offset(), 0);
    NETW_CHECK_EQ(door->get_recommended_display_offset(), 0);

    door->set_display_offset(3);
    NETW_CHECK_EQ(door->get_display_offset(), 0);
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C6 a node that reaches no session "
    "reaches no clock, because the clock is read off the session"
) {
    Node *orphan = memnew(Node);
    CHECK(Netw::session(orphan).is_null());
    memdelete(orphan);

    CHECK(Netw::session(nullptr).is_null());
}

} // namespace TestNetwClockDoor

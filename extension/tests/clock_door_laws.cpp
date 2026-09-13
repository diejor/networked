#include "support/netw_test.h"

#include "support/netw_recorder.h"

#include "netw/api/clock_handle.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwClockDoor {

using namespace godot;
using netw::Netw;
using netw::NetwClockHandle;
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

    const Ref<NetwClockHandle> door = session->get_clock();
    REQUIRE(door.is_valid());
    CHECK(door == session->get_clock());
    CHECK(make_session()->get_clock() != door);
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C2 every read on the clock door "
    "answers what the flat verb answers, param and monitor included"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwClockHandle> door = session->get_clock();

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
        int64_t(door->param(NetwMultiplayer::CLOCK_PARAM_TICKRATE)),
        int64_t(session->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE))
    );
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C3 a param written through the door "
    "is the same param the flat verb reads back"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwClockHandle> door = session->get_clock();

    NETW_CHECK_EQ(
        door->set_param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET, 3),
        OK
    );
    NETW_CHECK_EQ(
        int64_t(session->clock_get_param(
            NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET
        )),
        3
    );
    NETW_CHECK_EQ(
        int64_t(door->param(NetwMultiplayer::CLOCK_PARAM_DISPLAY_OFFSET)),
        3
    );

    SUBCASE("a refusal reaches the caller rather than being swallowed") {
        NETW_CHECK_EQ(
            door->set_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE, 45),
            session->clock_set_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE, 45)
        );
        NETW_CHECK_EQ(
            door->set_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE, 45),
            ERR_UNAUTHORIZED
        );
    }
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C4 the door re-emits the tick edges, "
    "which is the whole reason it is a class rather than three statics"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwClockHandle> door = session->get_clock();

    netw_test::Recorder heard(
        door.ptr(),
        {StringName("before_tick"), StringName("on_tick")}
    );

    session->emit_signal(StringName("clock_before_tick"), 0.25, int64_t(7));
    session->emit_signal(StringName("clock_on_tick"), 0.25, int64_t(7));

    NETW_CHECK_EQ(heard.count(StringName("before_tick")), 1);
    NETW_CHECK_EQ(heard.count(StringName("on_tick")), 1);
    NETW_CHECK_EQ(int64_t(heard.args(StringName("on_tick"))[1]), 7);
    NETW_CHECK_ORDER(
        double(heard.args(StringName("before_tick"))[0]),
        0.25,
        ==
    );
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C5 a clock door outliving its session "
    "answers a stopped clock rather than dereferencing what is gone"
) {
    Ref<NetwClockHandle> door;
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
    NETW_CHECK_EQ(
        int(door->param(NetwMultiplayer::CLOCK_PARAM_TICKRATE).get_type()),
        int(Variant::NIL)
    );
    NETW_CHECK_EQ(
        door->set_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE, 45),
        ERR_UNCONFIGURED
    );
}

TEST_CASE(
    "[Networked][Clock][Door][Hosted] C6 Netw.clock answers the clock of the "
    "session the node reaches, and nothing when the node reaches none"
) {
    Node *orphan = memnew(Node);
    CHECK(Netw::clock(orphan).is_null());
    memdelete(orphan);

    CHECK(Netw::clock(nullptr).is_null());
}

} // namespace TestNetwClockDoor

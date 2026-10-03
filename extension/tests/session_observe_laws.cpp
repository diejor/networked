#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSessionObserve {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

NetwMultiplayer *dropping_from = nullptr;
const Callable *dropped = nullptr;
int dropper_calls = 0;

void drop_the_next(const Variant &) {
    dropper_calls += 1;
    if (dropping_from != nullptr && dropped != nullptr) {
        dropping_from->unobserve(NetwMultiplayer::PEER_KICKED, *dropped);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] OB1 observe refuses an event that is not a "
    "Notification and an invalid callback, and a second observe of the same "
    "pair is one subscription"
) {
    Ref<NetwMultiplayer> session = make_session();
    const CallLog heard;
    const Callable kicked = heard.callable("kicked");

    NETW_CHECK_EQ(
        int(session->observe(
            static_cast<NetwMultiplayer::Notification>(999),
            kicked
        )),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(
        int(session->observe(NetwMultiplayer::PEER_KICKED, Callable())),
        int(ERR_INVALID_PARAMETER)
    );

    NETW_CHECK_EQ(
        int(session->observe(NetwMultiplayer::PEER_KICKED, kicked)),
        int(OK)
    );
    NETW_CHECK_EQ(
        int(session->observe(NetwMultiplayer::PEER_KICKED, kicked)),
        int(OK)
    );
    session->notify(NetwMultiplayer::PEER_KICKED, String("bye"));
    NETW_CHECK_EQ(heard.count(StringName("kicked")), 1);
    CHECK(String(heard.args(StringName("kicked"))[0]) == String("bye"));
}

TEST_CASE(
    "[Networked][Session][Hosted] OB2 a notification reaches only the "
    "callbacks observing that event, with its arguments, and unobserve stops "
    "it"
) {
    Ref<NetwMultiplayer> session = make_session();
    const CallLog heard;
    const Callable kicked = heard.callable("kicked");
    session->observe(NetwMultiplayer::PEER_KICKED, kicked);
    session->observe(
        NetwMultiplayer::SESSION_TREE_PAUSED,
        heard.callable("paused")
    );

    session->notify(NetwMultiplayer::PEER_KICKED, String("one"));
    NETW_CHECK_EQ(heard.count(StringName("kicked")), 1);
    NETW_CHECK_EQ(heard.count(StringName("paused")), 0);

    session->unobserve(NetwMultiplayer::PEER_KICKED, kicked);
    session->notify(NetwMultiplayer::PEER_KICKED, String("two"));
    NETW_CHECK_EQ(heard.count(StringName("kicked")), 1);
    session->unobserve(NetwMultiplayer::PEER_KICKED, kicked);
}

TEST_CASE(
    "[Networked][Session][Hosted] OB3 a callback that unobserves another "
    "subscriber during a notification stops that subscriber from hearing the "
    "same notification"
) {
    Ref<NetwMultiplayer> session = make_session();
    const CallLog heard;
    const Callable kicked = heard.callable("kicked");
    dropping_from = session.ptr();
    dropped = &kicked;
    dropper_calls = 0;

    session->observe(
        NetwMultiplayer::PEER_KICKED,
        callable_mp_static(&drop_the_next)
    );
    session->observe(NetwMultiplayer::PEER_KICKED, kicked);
    session->notify(NetwMultiplayer::PEER_KICKED, String("once"));

    NETW_CHECK_EQ(dropper_calls, 1);
    NETW_CHECK_EQ(heard.count(StringName("kicked")), 0);

    dropping_from = nullptr;
    dropped = nullptr;
}

TEST_CASE(
    "[Networked][Session][Hosted] OB4 a forced tick notifies before, on and "
    "after in that order, with the delta and the tick"
) {
    Ref<NetwMultiplayer> session = make_session();
    const CallLog heard;
    session->observe(
        NetwMultiplayer::CLOCK_BEFORE_TICK,
        heard.callable("before")
    );
    session->observe(NetwMultiplayer::CLOCK_ON_TICK, heard.callable("on"));
    session->observe(
        NetwMultiplayer::CLOCK_AFTER_TICK,
        heard.callable("after")
    );

    const int64_t tick = session->clock_engine().get_tick();
    session->clock_engine().force_step(1);

    CHECK(
        heard.order()
        == Vector<StringName>({StringName("before"),
                               StringName("on"),
                               StringName("after")})
    );
    NETW_CHECK_EQ(int64_t(heard.args(StringName("on"))[1]), tick);
    CHECK(double(heard.args(StringName("on"))[0]) > 0.0);
}

TEST_CASE(
    "[Networked][Session][Hosted] OB5 a frame brackets its ticks with the "
    "loop notifications, a frame that runs no tick still brackets, and a "
    "forced tick runs its three phases with no brackets"
) {
    Ref<NetwMultiplayer> session = make_session();
    CallLog heard;
    session->observe(
        NetwMultiplayer::CLOCK_BEFORE_TICK_LOOP,
        heard.callable("loop_in")
    );
    session->observe(
        NetwMultiplayer::CLOCK_BEFORE_TICK,
        heard.callable("before")
    );
    session->observe(NetwMultiplayer::CLOCK_ON_TICK, heard.callable("on"));
    session->observe(
        NetwMultiplayer::CLOCK_AFTER_TICK,
        heard.callable("after")
    );
    session->observe(
        NetwMultiplayer::CLOCK_AFTER_TICK_LOOP,
        heard.callable("loop_out")
    );
    netw::ClockEngine &clock = session->clock_engine();

    clock.physics_step(clock.ticktime());
    CHECK(
        heard.order()
        == Vector<StringName>({StringName("loop_in"),
                               StringName("before"),
                               StringName("on"),
                               StringName("after"),
                               StringName("loop_out")})
    );

    heard.clear();
    clock.physics_step(0.0);
    CHECK(
        heard.order()
        == Vector<StringName>({StringName("loop_in"), StringName("loop_out")})
    );

    heard.clear();
    clock.force_step(1);
    CHECK(
        heard.order()
        == Vector<StringName>({StringName("before"),
                               StringName("on"),
                               StringName("after")})
    );
}

} // namespace TestNetwSessionObserve

#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include <cmath>

#include "godot/spatial_node.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"

namespace TestNetwDisplayPump {

using namespace godot;
using netw::NetwInterpolate;
using netw::display::Channel;
using netw::display::Decl;
using netw::display::History;
using netw::display::Hooks;
using netw::display::PumpStats;
using netw::display::Runtime;
using netw::display::Timing;

PumpStats make_stats() {
    return PumpStats();
}

PumpStats &discarded_stats() {
    static PumpStats stats;
    stats.reset();
    return stats;
}

Timing make_timing(int p_display_tick, double p_frame_delta) {
    Timing timing;
    timing.display_tick = p_display_tick;
    timing.tick_factor = 0.0;
    timing.ticktime = 1.0 / 30.0;
    timing.frame_delta = p_frame_delta;
    return timing;
}

Channel *attach_channel(Runtime *p_runtime, const Callable &p_output) {
    Channel *channel = p_runtime->add_channel();
    channel->set_name("position");
    channel->set_state_key("Body:position");
    channel->set_target_prop("position");
    channel->set_source_prop("position");
    channel->set_output(p_output);
    channel->set_last_written(Vector2(0.0, 0.0));

    Ref<NetwInterpolate> spec;
    spec.instantiate();
    channel->set_spec(spec);

    channel->display_history().set_mode(spec->get_mode());

    p_runtime->display_tracks().declare(
        channel->get_state_key(),
        channel->get_name()
    );
    return channel;
}

struct RuntimeRig {
    Runtime held;

    RuntimeRig(Node *p_owner, int p_pump_mode) {
        held.bind(nullptr, p_owner);
        held.set_pump_mode(p_pump_mode);
        held.set_config(Decl());
    }

    RuntimeRig(const RuntimeRig &) = delete;
    RuntimeRig &operator=(const RuntimeRig &) = delete;

    Runtime *operator->() {
        return &held;
    }
    operator Runtime *() {
        return &held;
    }
};

TEST_CASE(
    "[Networked][Display][Hosted] PK1 an UNRESOLVED runtime asks its role "
    "resolver, and a runtime that already knows its mode never asks"
) {
    netw_test::CallLog log;
    Node2D *owner = memnew(Node2D);
    Hooks hooks;
    hooks.resolve_role = log.callable("resolve");

    RuntimeRig unresolved(owner, netw::display::PUMP_UNRESOLVED);
    netw::display::pump_runtime(
        unresolved,
        make_timing(0, 1.0 / 60.0),
        discarded_stats(),
        hooks
    );
    NETW_CHECK_EQ(log.count("resolve"), 1);

    RuntimeRig settled(owner, netw::display::PUMP_DISABLED);
    netw::display::pump_runtime(
        settled,
        make_timing(0, 1.0 / 60.0),
        discarded_stats(),
        hooks
    );
    NETW_CHECK_EQ(log.count("resolve"), 1);

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK2 a DISABLED runtime is not counted, "
    "because a pass that declined reads differently from one that ran"
) {
    Node2D *owner = memnew(Node2D);
    PumpStats stats = make_stats();
    RuntimeRig runtime(owner, netw::display::PUMP_DISABLED);

    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        stats,
        Hooks()
    );

    NETW_CHECK_EQ(stats.runtimes, 0);
    NETW_CHECK_EQ(runtime->get_pumped(), int64_t(0));
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK3 a runtime whose owner is gone is passed "
    "over entirely, so a freed body never costs a resolve or a count"
) {
    netw_test::CallLog log;
    PumpStats stats = make_stats();
    Hooks hooks;
    hooks.resolve_role = log.callable("resolve");

    Node2D *owner = memnew(Node2D);
    RuntimeRig runtime(owner, netw::display::PUMP_UNRESOLVED);
    memdelete(owner);

    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        stats,
        hooks
    );

    NETW_CHECK_EQ(log.count("resolve"), 0);
    NETW_CHECK_EQ(stats.runtimes, 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK4 the CHASE pump reads the live body and "
    "shows a value eased toward it rather than the body itself"
) {
    Node2D *owner = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    body->set_position(Vector2(10.0, 0.0));

    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_CHASE);
    Channel *channel = attach_channel(runtime, log.callable("shown"));
    channel->set_source_obj(body);

    PumpStats stats = make_stats();
    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        stats,
        Hooks()
    );

    NETW_CHECK_EQ(log.count("shown"), 1);
    NETW_CHECK_EQ(stats.runtimes, 1);
    const Vector2 shown = log.args("shown")[0];
    CHECK(shown.x > 0.0);
    CHECK(shown.x < 10.0);

    memdelete(body);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK5 a CHASE channel that feeds itself is "
    "left alone, because smoothing a simulation into itself is a drag"
) {
    Node2D *owner = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    body->set_position(Vector2(10.0, 0.0));

    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_CHASE);
    Channel *channel = attach_channel(runtime, log.callable("shown"));
    channel->set_source_obj(body);
    channel->set_self_feedback(true);

    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        discarded_stats(),
        Hooks()
    );

    NETW_CHECK_EQ(log.count("shown"), 0);
    memdelete(body);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK5b a BRACKETED channel that feeds itself "
    "is left alone, and a redirected one on the same pass still shows"
) {
    Node2D *owner = memnew(Node2D);
    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_BRACKETED);
    Decl config;
    config.set_param(netw::display::PARAM_SMART_DILATION, false);
    runtime->set_config(config);

    Channel *sampled = attach_channel(runtime, log.callable("sampled"));
    sampled->set_self_feedback(true);
    sampled->display_history().record(0, Vector2(0.0, 0.0), false);
    sampled->display_history().record(4, Vector2(8.0, 0.0), false);

    Channel *redirected = attach_channel(runtime, log.callable("redirected"));
    redirected->set_name("visual");
    redirected->set_state_key("Body:visual");
    redirected->set_self_feedback(false);
    redirected->display_history().record(0, Vector2(0.0, 0.0), false);
    redirected->display_history().record(4, Vector2(8.0, 0.0), false);

    netw::display::pump_runtime(
        runtime,
        make_timing(2, 1.0 / 60.0),
        discarded_stats(),
        Hooks()
    );

    NETW_CHECK_EQ(log.count("sampled"), 0);
    NETW_CHECK_EQ(log.count("redirected"), 1);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK6 an empty history shows nothing, so the "
    "pump defers to whatever last wrote rather than clobbering it"
) {
    Node2D *owner = memnew(Node2D);
    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_REMOTE);
    attach_channel(runtime, log.callable("shown"));

    netw::display::pump_runtime(
        runtime,
        make_timing(4, 1.0 / 60.0),
        discarded_stats(),
        Hooks()
    );

    NETW_CHECK_EQ(log.count("shown"), 0);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK7 a REMOTE pump samples the history under "
    "the playhead and shows the interpolation, not the newest row"
) {
    Node2D *owner = memnew(Node2D);
    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_REMOTE);
    Channel *channel = attach_channel(runtime, log.callable("shown"));
    Decl config;
    config.set_param(netw::display::PARAM_SMART_DILATION, false);
    runtime->set_config(config);

    History &history = channel->display_history();
    history.record(0, Vector2(0.0, 0.0), false);
    history.record(4, Vector2(8.0, 0.0), false);

    netw::display::pump_runtime(
        runtime,
        make_timing(2, 1.0 / 60.0),
        discarded_stats(),
        Hooks()
    );

    REQUIRE(log.count("shown") == 1);
    const Vector2 shown = log.args("shown")[0];
    CHECK(shown.x > 0.0);
    CHECK(shown.x < 8.0);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK8 a teleported recovery CLEARS every "
    "offset, because a genuine desync should be seen to snap"
) {
    Node2D *owner = memnew(Node2D);
    RuntimeRig runtime(owner, netw::display::PUMP_CHASE);
    Channel *channel = attach_channel(runtime, Callable());
    channel->render_offset().absorb(Vector2(5.0, 0.0), INFINITY);
    REQUIRE(channel->render_offset().is_held());

    netw::display::absorb_recovery(runtime, Dictionary(), true, Hooks());

    CHECK(!channel->render_offset().is_held());
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK9 a recovery absorbs into the channel its "
    "delta NAMES, clamped by the entity's own teleport tier"
) {
    netw_test::CallLog log;
    Node2D *owner = memnew(Node2D);
    RuntimeRig runtime(owner, netw::display::PUMP_CHASE);
    Channel *channel = attach_channel(runtime, Callable());

    Hooks hooks;
    hooks.chase_clamp = log.answering("clamp", 2.0);

    Dictionary deltas;
    deltas["position"] = Vector2(5.0, 0.0);
    netw::display::absorb_recovery(runtime, deltas, false, hooks);

    NETW_CHECK_EQ(log.count("clamp"), 1);
    CHECK(runtime->get_display_offset_limit() == doctest::Approx(2.0));
    REQUIRE(channel->render_offset().is_held());
    CHECK(
        Vector2(channel->render_offset().residual).length()
        <= doctest::Approx(2.0)
    );
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK10 a recovery reaching a runtime that is "
    "no longer chasing is dropped, so a stale signal cannot seed an offset"
) {
    Node2D *owner = memnew(Node2D);
    RuntimeRig runtime(owner, netw::display::PUMP_REMOTE);
    Channel *channel = attach_channel(runtime, Callable());

    Dictionary deltas;
    deltas["position"] = Vector2(5.0, 0.0);
    netw::display::absorb_recovery(runtime, deltas, false, Hooks());

    CHECK(!channel->render_offset().is_held());
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK11 a trace interval of zero never traces, "
    "and an interval of N traces once every N frames"
) {
    RuntimeRig quiet(nullptr, netw::display::PUMP_UNRESOLVED);
    Decl decl = quiet->get_config();

    CHECK(!netw::display::take_trace_frame(quiet));

    decl.set_param(netw::display::PARAM_TRACE_INTERVAL, 3);
    quiet->set_config(decl);
    CHECK(!netw::display::take_trace_frame(quiet));
    CHECK(!netw::display::take_trace_frame(quiet));
    CHECK(netw::display::take_trace_frame(quiet));
}

TEST_CASE(
    "[Networked][Display][Hosted] PK12 the chase smoothing time falls back to "
    "most of one tick, so a runtime with no override still eases"
) {
    RuntimeRig runtime(nullptr, netw::display::PUMP_UNRESOLVED);
    Decl decl = runtime->get_config();

    const Timing timing = make_timing(0, 1.0 / 60.0);
    CHECK(
        netw::display::chase_smooth_time(runtime, timing)
        == doctest::Approx((1.0 / 30.0) * 0.85)
    );

    decl.set_param(netw::display::PARAM_PREDICTED_SMOOTH_TIME, 0.5);
    runtime->set_config(decl);
    CHECK(
        netw::display::chase_smooth_time(runtime, timing)
        == doctest::Approx(0.5)
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] PK13 a reset shows the LIVE source value and "
    "forgets the recorded past, so the display resumes from truth"
) {
    Node2D *owner = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    body->set_position(Vector2(42.0, 0.0));

    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_REMOTE);
    Channel *channel = attach_channel(runtime, log.callable("shown"));
    channel->set_source_obj(body);
    channel->display_history().record(0, Vector2(0.0, 0.0), false);
    channel->render_offset().absorb(Vector2(9.0, 0.0), INFINITY);

    runtime->reset(2, 2);

    CHECK(channel->display_history().is_empty());
    CHECK(!channel->render_offset().is_held());
    CHECK(
        Vector2(channel->get_last_written()).is_equal_approx(Vector2(42.0, 0.0))
    );
    REQUIRE(log.count("shown") == 1);
    CHECK(Vector2(log.args("shown")[0]).is_equal_approx(Vector2(42.0, 0.0)));

    memdelete(body);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK14 a channel with no source falls back to "
    "its TARGET, so a write-only stream still resets to what is on screen"
) {
    Node2D *target = memnew(Node2D);
    target->set_position(Vector2(3.0, 0.0));

    Channel channel;
    channel.set_target_prop("position");
    channel.set_target_obj(target);

    CHECK(Vector2(channel.current_source_value())
              .is_equal_approx(Vector2(3.0, 0.0)));

    Channel bare;
    CHECK(bare.current_source_value().get_type() == Variant::NIL);

    memdelete(target);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK15 a snap by NAME reaches the channel that "
    "declared the name, and a name nothing declared is a no-op"
) {
    Node2D *owner = memnew(Node2D);
    netw_test::CallLog log;
    RuntimeRig runtime(owner, netw::display::PUMP_REMOTE);
    Channel *channel = attach_channel(runtime, log.callable("shown"));
    channel->display_history().record(0, Vector2(0.0, 0.0), false);

    runtime->snap_named("position", Vector2(5.0, 5.0));

    CHECK(channel->display_history().is_empty());
    CHECK(
        Vector2(channel->get_last_written()).is_equal_approx(Vector2(5.0, 5.0))
    );
    NETW_CHECK_EQ(log.count("shown"), 1);

    runtime->snap_named("nothing_declares_this", Vector2(9.0, 9.0));
    NETW_CHECK_EQ(log.count("shown"), 1);

    memdelete(owner);
}

} // namespace TestNetwDisplayPump

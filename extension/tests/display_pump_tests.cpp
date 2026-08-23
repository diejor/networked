#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include <cmath>

#include "godot/spatial_node.hpp"
#include "netw/display_channel.hpp"
#include "netw/api/display_decl.hpp"
#include "netw/display_history.hpp"
#include "netw/display_pump.hpp"
#include "netw/display_runtime.hpp"
#include "netw/display_timing.hpp"
#include "netw/api/interpolate.hpp"

namespace TestNetwDisplayPump {

using namespace godot;
using netw::NetwDisplayChannel;
using netw::NetwDisplayDecl;
using netw::NetwDisplayHistory;
using netw::NetwDisplayRuntime;
using netw::NetwDisplayTiming;
using netw::NetwInterpolate;
using netw::NetwPumpStats;
using netw::display::DisplayHooks;

Ref<NetwPumpStats> make_stats() {
    Ref<NetwPumpStats> stats;
    stats.instantiate();
    return stats;
}

Ref<NetwDisplayTiming> make_timing(int p_display_tick, double p_frame_delta) {
    Ref<NetwDisplayTiming> timing;
    timing.instantiate();
    timing->set_display_tick(p_display_tick);
    timing->set_tick_factor(0.0);
    timing->set_ticktime(1.0 / 30.0);
    timing->set_frame_delta(p_frame_delta);
    return timing;
}

Ref<NetwDisplayChannel> attach_channel(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Callable &p_output
) {
    Ref<NetwDisplayChannel> channel;
    channel.instantiate();
    channel->set_name("position");
    channel->set_state_key("Body:position");
    channel->set_target_prop("position");
    channel->set_source_prop("position");
    channel->set_output(p_output);
    channel->set_last_written(Vector2(0.0, 0.0));

    Ref<NetwInterpolate> spec;
    spec.instantiate();
    channel->set_spec(spec);

    Ref<NetwDisplayHistory> history;
    history.instantiate();
    history->set_mode(spec->get_mode());
    channel->set_history(history);

    TypedArray<NetwDisplayChannel> states = p_runtime->get_states();
    states.append(channel);
    p_runtime->set_states(states);
    p_runtime->get_tracks()->declare(
        channel->get_state_key(),
        channel->get_name()
    );
    return channel;
}

Ref<NetwDisplayRuntime> make_runtime(Node *p_owner, int p_pump_mode) {
    Ref<NetwDisplayRuntime> runtime;
    runtime.instantiate();
    runtime->bind(nullptr, p_owner);
    runtime->set_pump_mode(p_pump_mode);
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();
    runtime->set_config(decl);
    return runtime;
}

TEST_CASE(
    "[Networked][Display][Hosted] PK1 an UNRESOLVED runtime asks its role "
    "resolver, and a runtime that already knows its mode never asks"
) {
    netw_test::CallLog log;
    Node2D *owner = memnew(Node2D);
    DisplayHooks hooks;
    hooks.resolve_role = log.callable("resolve");

    Ref<NetwDisplayRuntime> unresolved
        = make_runtime(owner, NetwDisplayDecl::PUMP_UNRESOLVED);
    netw::display::pump_runtime(
        unresolved,
        make_timing(0, 1.0 / 60.0),
        make_stats(),
        hooks
    );
    NETW_CHECK_EQ(log.count("resolve"), 1);

    Ref<NetwDisplayRuntime> settled
        = make_runtime(owner, NetwDisplayDecl::PUMP_DISABLED);
    netw::display::pump_runtime(
        settled,
        make_timing(0, 1.0 / 60.0),
        make_stats(),
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
    Ref<NetwPumpStats> stats = make_stats();
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_DISABLED);

    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        stats,
        DisplayHooks()
    );

    NETW_CHECK_EQ(stats->get_runtimes(), 0);
    NETW_CHECK_EQ(runtime->get_pumped(), int64_t(0));
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK3 a runtime whose owner is gone is passed "
    "over entirely, so a freed body never costs a resolve or a count"
) {
    netw_test::CallLog log;
    Ref<NetwPumpStats> stats = make_stats();
    DisplayHooks hooks;
    hooks.resolve_role = log.callable("resolve");

    Node2D *owner = memnew(Node2D);
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_UNRESOLVED);
    memdelete(owner);

    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        stats,
        hooks
    );

    NETW_CHECK_EQ(log.count("resolve"), 0);
    NETW_CHECK_EQ(stats->get_runtimes(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK4 the CHASE pump reads the live body and "
    "shows a value eased toward it rather than the body itself"
) {
    Node2D *owner = memnew(Node2D);
    Node2D *body = memnew(Node2D);
    body->set_position(Vector2(10.0, 0.0));

    netw_test::CallLog log;
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_CHASE);
    Ref<NetwDisplayChannel> channel
        = attach_channel(runtime, log.callable("shown"));
    channel->set_source_obj(body);

    Ref<NetwPumpStats> stats = make_stats();
    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        stats,
        DisplayHooks()
    );

    NETW_CHECK_EQ(log.count("shown"), 1);
    NETW_CHECK_EQ(stats->get_runtimes(), 1);
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
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_CHASE);
    Ref<NetwDisplayChannel> channel
        = attach_channel(runtime, log.callable("shown"));
    channel->set_source_obj(body);
    channel->set_self_feedback(true);

    netw::display::pump_runtime(
        runtime,
        make_timing(0, 1.0 / 60.0),
        make_stats(),
        DisplayHooks()
    );

    NETW_CHECK_EQ(log.count("shown"), 0);
    memdelete(body);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK6 an empty history shows nothing, so the "
    "pump defers to whatever last wrote rather than clobbering it"
) {
    Node2D *owner = memnew(Node2D);
    netw_test::CallLog log;
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_REMOTE);
    attach_channel(runtime, log.callable("shown"));

    netw::display::pump_runtime(
        runtime,
        make_timing(4, 1.0 / 60.0),
        make_stats(),
        DisplayHooks()
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
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_REMOTE);
    Ref<NetwDisplayChannel> channel
        = attach_channel(runtime, log.callable("shown"));
    runtime->get_config()->set_param(
        NetwDisplayDecl::PARAM_SMART_DILATION,
        false
    );

    const Ref<NetwDisplayHistory> history = channel->get_history();
    history->record(0, Vector2(0.0, 0.0), false);
    history->record(4, Vector2(8.0, 0.0), false);

    netw::display::pump_runtime(
        runtime,
        make_timing(2, 1.0 / 60.0),
        make_stats(),
        DisplayHooks()
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
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_CHASE);
    Ref<NetwDisplayChannel> channel = attach_channel(runtime, Callable());
    channel->render_offset().absorb(Vector2(5.0, 0.0), INFINITY);
    REQUIRE(channel->render_offset().is_held());

    netw::display::absorb_recovery(runtime, Dictionary(), true, DisplayHooks());

    CHECK(!channel->render_offset().is_held());
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK9 a recovery absorbs into the channel its "
    "delta NAMES, clamped by the entity's own teleport tier"
) {
    netw_test::CallLog log;
    Node2D *owner = memnew(Node2D);
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_CHASE);
    Ref<NetwDisplayChannel> channel = attach_channel(runtime, Callable());

    DisplayHooks hooks;
    hooks.chase_clamp = log.answering("clamp", 2.0);

    Dictionary deltas;
    deltas["position"] = Vector2(5.0, 0.0);
    netw::display::absorb_recovery(runtime, deltas, false, hooks);

    NETW_CHECK_EQ(log.count("clamp"), 1);
    CHECK(runtime->get_display_offset_limit() == doctest::Approx(2.0));
    REQUIRE(channel->render_offset().is_held());
    CHECK(Vector2(channel->render_offset().residual).length()
          <= doctest::Approx(2.0));
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK10 a recovery reaching a runtime that is "
    "no longer chasing is dropped, so a stale signal cannot seed an offset"
) {
    Node2D *owner = memnew(Node2D);
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_REMOTE);
    Ref<NetwDisplayChannel> channel = attach_channel(runtime, Callable());

    Dictionary deltas;
    deltas["position"] = Vector2(5.0, 0.0);
    netw::display::absorb_recovery(runtime, deltas, false, DisplayHooks());

    CHECK(!channel->render_offset().is_held());
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK11 a trace interval of zero never traces, "
    "and an interval of N traces once every N frames"
) {
    Ref<NetwDisplayRuntime> quiet;
    quiet.instantiate();
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();
    quiet->set_config(decl);

    CHECK(!netw::display::take_trace_frame(quiet));

    decl->set_param(NetwDisplayDecl::PARAM_TRACE_INTERVAL, 3);
    CHECK(!netw::display::take_trace_frame(quiet));
    CHECK(!netw::display::take_trace_frame(quiet));
    CHECK(netw::display::take_trace_frame(quiet));
}

TEST_CASE(
    "[Networked][Display][Hosted] PK12 the chase smoothing time falls back to "
    "most of one tick, so a runtime with no override still eases"
) {
    Ref<NetwDisplayRuntime> runtime;
    runtime.instantiate();
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();
    runtime->set_config(decl);

    Ref<NetwDisplayTiming> timing = make_timing(0, 1.0 / 60.0);
    CHECK(
        netw::display::chase_smooth_time(runtime, timing)
        == doctest::Approx((1.0 / 30.0) * 0.85)
    );

    decl->set_param(NetwDisplayDecl::PARAM_PREDICTED_SMOOTH_TIME, 0.5);
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
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_REMOTE);
    Ref<NetwDisplayChannel> channel
        = attach_channel(runtime, log.callable("shown"));
    channel->set_source_obj(body);
    channel->get_history()->record(0, Vector2(0.0, 0.0), false);
    channel->render_offset().absorb(Vector2(9.0, 0.0), INFINITY);

    runtime->reset(2, 2);

    CHECK(channel->get_history()->is_empty());
    CHECK(!channel->render_offset().is_held());
    CHECK(Vector2(channel->get_last_written())
              .is_equal_approx(Vector2(42.0, 0.0)));
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

    Ref<NetwDisplayChannel> channel;
    channel.instantiate();
    channel->set_target_prop("position");
    channel->set_target_obj(target);

    CHECK(Vector2(channel->current_source_value())
              .is_equal_approx(Vector2(3.0, 0.0)));

    Ref<NetwDisplayChannel> bare;
    bare.instantiate();
    CHECK(bare->current_source_value().get_type() == Variant::NIL);

    memdelete(target);
}

TEST_CASE(
    "[Networked][Display][Hosted] PK15 a snap by NAME reaches the channel that "
    "declared the name, and a name nothing declared is a no-op"
) {
    Node2D *owner = memnew(Node2D);
    netw_test::CallLog log;
    Ref<NetwDisplayRuntime> runtime
        = make_runtime(owner, NetwDisplayDecl::PUMP_REMOTE);
    Ref<NetwDisplayChannel> channel
        = attach_channel(runtime, log.callable("shown"));
    channel->get_history()->record(0, Vector2(0.0, 0.0), false);

    runtime->snap_named("position", Vector2(5.0, 5.0));

    CHECK(channel->get_history()->is_empty());
    CHECK(Vector2(channel->get_last_written())
              .is_equal_approx(Vector2(5.0, 5.0)));
    NETW_CHECK_EQ(log.count("shown"), 1);

    runtime->snap_named("nothing_declares_this", Vector2(9.0, 9.0));
    NETW_CHECK_EQ(log.count("shown"), 1);

    memdelete(owner);
}

} // namespace TestNetwDisplayPump

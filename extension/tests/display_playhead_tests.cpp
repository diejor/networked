#include "support/netw_test.h"

#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/roles.hpp"
#include "netw/display/runtime.hpp"

namespace TestNetwPlayhead {

using namespace godot;
using netw::display::Channel;
using netw::display::Decl;
using netw::display::dilate_playhead;
using netw::display::hear_arrivals;
using netw::display::Playhead;
using netw::display::PumpStats;
using netw::display::Runtime;
using netw::display::Timing;

Playhead playhead(int expected_interval_ticks) {
    Playhead out;
    out.set_expected_interval_ticks(expected_interval_ticks);
    return out;
}

TEST_CASE(
    "[Networked][Display][Hosted] the lag floor buys the snapshot interval the "
    "clock does not already trail by"
) {
    Playhead head = playhead(3);
    const Decl settings;

    NETW_CHECK_CLOSE(head.min_lag(settings, 0, 0), 4.0, 0.0);
    NETW_CHECK_CLOSE(head.min_lag(settings, 3, 3), 1.0, 0.0);
    NETW_CHECK_CLOSE(head.min_lag(settings, 9, 9), 0.0, 0.0);
    NETW_CHECK_CLOSE(head.min_lag(settings, 3, 5), 3.0, 0.0);
    NETW_CHECK_CLOSE(head.min_lag(settings, 5, 3), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a forecasting timeline buffers nothing at all"
) {
    Playhead head = playhead(3);
    Decl settings;
    settings.set_param(
        netw::display::PARAM_TIMELINE_MODE,
        netw::display::TIMELINE_FORECAST
    );

    NETW_CHECK_CLOSE(head.min_lag(settings, 0, 8), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] the effective tick is the clock's, moved "
    "back by the whole ticks of the lag"
) {
    Playhead head = playhead(3);

    head.set_display_lag(0.0);
    NETW_CHECK_EQ(head.effective_tick(100, 0.0), 100);

    head.set_display_lag(2.5);
    NETW_CHECK_EQ(head.effective_tick(100, 0.0), 97);
    NETW_CHECK_EQ(head.effective_tick(100, 0.5), 98);

    head.set_display_lag(-1.5);
    NETW_CHECK_EQ(head.effective_tick(100, 0.0), 101);
}

TEST_CASE(
    "[Networked][Display][Hosted] a fed stream adapts toward the floor rather "
    "than jumping onto it"
) {
    Playhead head = playhead(3);
    Decl settings;
    settings.set_param(netw::display::PARAM_FLOOR_SMOOTHING, 0.5);
    settings.set_param(netw::display::PARAM_LAG_ADAPT_RATE, 0.5);

    head.dilate(settings, 0, 0, false);

    NETW_CHECK_CLOSE(head.get_smoothed_floor(), 2.0, 0.0);
    NETW_CHECK_CLOSE(head.get_display_lag(), 1.0, 0.0);
    NETW_CHECK_EQ(head.get_starvation_ticks(), 0);

    head.dilate(settings, 0, 0, false);
    NETW_CHECK_CLOSE(head.get_smoothed_floor(), 3.0, 0.0);
    NETW_CHECK_CLOSE(head.get_display_lag(), 2.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a starved frame never grows the lag, and "
    "the starvation count resets on the first fed frame"
) {
    Playhead head = playhead(3);
    Decl settings;
    settings.set_param(netw::display::PARAM_LAG_ADAPT_RATE, 0.0);
    settings.set_param(netw::display::PARAM_FLOOR_SMOOTHING, 0.0);

    head.dilate(settings, 0, 0, true);
    head.dilate(settings, 0, 0, true);
    NETW_CHECK_EQ(head.get_starvation_ticks(), 2);
    NETW_CHECK_CLOSE(head.get_display_lag(), 0.0, 0.0);

    head.dilate(settings, 0, 0, false);
    NETW_CHECK_EQ(head.get_starvation_ticks(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a bracketed playhead follows the clock one "
    "whole tick behind and carries no lag"
) {
    Playhead head = playhead(3);
    head.set_display_lag(4.0);

    NETW_CHECK_CLOSE(head.place(10, 0.25, true), 0.25, 0.0);

    NETW_CHECK_EQ(head.get_display_tick(), 9);
    NETW_CHECK_CLOSE(head.get_display_lag(), 0.0, 0.0);

    head.place(0, 0.5, true);
    NETW_CHECK_EQ(head.get_display_tick(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] FX1 a bracketed playhead moves the whole "
    "ticks of a factor above one into its tick and keeps the fraction"
) {
    Playhead head = playhead(3);

    NETW_CHECK_CLOSE(head.place(10, 1.4, true), 0.4, 1e-9);
    NETW_CHECK_EQ(head.get_display_tick(), 10);

    NETW_CHECK_CLOSE(head.place(10, 2.25, true), 0.25, 1e-9);
    NETW_CHECK_EQ(head.get_display_tick(), 11);
}

TEST_CASE(
    "[Networked][Display][Hosted] FX2 a playhead past the newest row by its "
    "tick factor alone counts as starving"
) {
    Runtime holder;
    Channel *channel = holder.add_channel();
    channel->set_name("position");
    channel->display_history().record(9, Vector2(0.0, 0.0), false);
    channel->display_history().record(10, Vector2(1.0, 0.0), false);

    Timing timing;
    timing.display_tick = 9;
    timing.frame_ticks = 1.0;

    PumpStats short_of_newest;
    timing.tick_factor = 0.8;
    dilate_playhead(&holder, timing, short_of_newest, false);
    NETW_CHECK_EQ(short_of_newest.starving, 0);

    PumpStats past_newest;
    timing.tick_factor = 1.2;
    dilate_playhead(&holder, timing, past_newest, false);
    NETW_CHECK_EQ(past_newest.starving, 1);
}

TEST_CASE(
    "[Networked][Display][Hosted] a dilated playhead samples the lag behind "
    "the clock, fraction and all"
) {
    Playhead head = playhead(3);
    head.set_display_lag(2.75);

    NETW_CHECK_CLOSE(head.place(10, 0.5, false), 0.75, 0.0);

    NETW_CHECK_EQ(head.get_display_tick(), 7);
    NETW_CHECK_CLOSE(head.get_display_lag(), 2.75, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a settled playhead is at its floor with no "
    "starvation behind it"
) {
    Playhead head = playhead(3);
    const Decl settings;
    head.set_display_lag(9.0);
    head.set_starvation_ticks(5);

    head.settle(settings, 1, 2);

    NETW_CHECK_CLOSE(head.get_display_lag(), 4.0, 0.0);
    NETW_CHECK_CLOSE(head.get_smoothed_floor(), 4.0, 0.0);
    NETW_CHECK_EQ(head.get_starvation_ticks(), 0);
}

void hear_ages(
    Playhead &r_head,
    const Decl &p_settings,
    int64_t &r_tick,
    int p_count,
    double p_even_age,
    double p_odd_age
) {
    for (int at = 0; at < p_count; ++at) {
        ++r_tick;
        const double age = at % 2 == 0 ? p_even_age : p_odd_age;
        r_head.hear_newest(p_settings, r_tick, double(r_tick) + age);
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] FX3 the lag floor settles at the arrival "
    "age plus its spread and one tick, and never above the cap"
) {
    const Decl settings;
    Playhead head = playhead(3);
    int64_t tick = 100;
    hear_ages(head, settings, tick, 400, 3.0, 5.0);

    const double spread = 4.0 + settings.arrival_spread_weight * 1.0 + 1.0;
    NETW_CHECK_CLOSE(head.min_lag(settings, 2, 2), spread, 0.2);

    Playhead far = playhead(3);
    int64_t far_tick = 100;
    hear_ages(far, settings, far_tick, 400, 40.0, 40.0);
    NETW_CHECK_CLOSE(
        far.min_lag(settings, 2, 2),
        settings.max_arrival_lag,
        0.0
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] FX4 a resting author feeds no arrival "
    "ages, so the lag floor holds across the rest"
) {
    Runtime holder;
    Channel *channel = holder.add_channel();
    channel->set_name("position");
    netw::display::History &history = channel->display_history();
    const Decl &settings = holder.get_config();

    Timing timing;
    timing.tick_factor = 0.5;
    for (int64_t tick = 10; tick < 210; ++tick) {
        history.record(tick, Vector2(double(tick), 0.0), false);
        timing.display_tick = int(tick) + 4;
        hear_arrivals(&holder, timing);
    }
    const double moving = holder.display_playhead().min_lag(settings, 2, 2);
    NETW_CHECK_CLOSE(moving, 5.5, 1e-9);

    history.set_sleeping(true);
    for (int frame = 0; frame < 300; ++frame) {
        timing.display_tick += 1;
        hear_arrivals(&holder, timing);
    }
    NETW_CHECK_CLOSE(
        holder.display_playhead().min_lag(settings, 2, 2),
        moving,
        0.0
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] FX5 a teleport, a snap and a role change "
    "each forget the arrival ages the lag floor was built from"
) {
    const Decl settings;
    Playhead head = playhead(3);
    int64_t tick = 100;
    hear_ages(head, settings, tick, 50, 6.0, 6.0);
    head.settle(settings, 2, 2);
    CHECK_FALSE(head.hears_arrivals());
    NETW_CHECK_CLOSE(head.get_display_lag(), 2.0, 0.0);

    Runtime snapped;
    Channel *channel = snapped.add_channel();
    channel->set_name("position");
    snapped.display_tracks().declare("Body:position", "position");
    hear_ages(snapped.display_playhead(), settings, tick, 50, 6.0, 6.0);
    snapped.snap_named("position", Vector2(1.0, 1.0));
    CHECK_FALSE(snapped.display_playhead().hears_arrivals());

    Runtime handed;
    Decl disabled;
    disabled.set_param(netw::display::PARAM_ROLE, netw::display::ROLE_DISABLED);
    handed.set_config(disabled);
    handed.set_pump_mode(netw::display::PUMP_REMOTE);
    hear_ages(handed.display_playhead(), settings, tick, 50, 6.0, 6.0);
    netw::display::resolve_role(&handed, netw::display::Hooks());
    CHECK_FALSE(handed.display_playhead().hears_arrivals());
}

} // namespace TestNetwPlayhead

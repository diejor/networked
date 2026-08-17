#include "support/netw_test.h"

#include "netw/display_playhead.hpp"

namespace TestNetwDisplayPlayhead {

using namespace godot;
using netw::NetwDisplayDecl;
using netw::NetwDisplayPlayhead;

Ref<NetwDisplayPlayhead> playhead(int expected_interval_ticks) {
    Ref<NetwDisplayPlayhead> out;
    out.instantiate();
    out->set_expected_interval_ticks(expected_interval_ticks);
    return out;
}

Ref<NetwDisplayDecl> decl() {
    Ref<NetwDisplayDecl> out;
    out.instantiate();
    return out;
}

TEST_CASE(
    "[Networked][Display][Hosted] the lag floor buys the snapshot interval the "
    "clock does not already trail by"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    const Ref<NetwDisplayDecl> settings = decl();

    NETW_CHECK_CLOSE(head->min_lag(settings, 0, 0), 4.0, 0.0);
    NETW_CHECK_CLOSE(head->min_lag(settings, 3, 3), 1.0, 0.0);
    NETW_CHECK_CLOSE(head->min_lag(settings, 9, 9), 0.0, 0.0);
    NETW_CHECK_CLOSE(head->min_lag(settings, 3, 5), 3.0, 0.0);
    NETW_CHECK_CLOSE(head->min_lag(settings, 5, 3), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a forecasting timeline buffers nothing at all"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    const Ref<NetwDisplayDecl> settings = decl();
    settings->set_param(
        NetwDisplayDecl::PARAM_TIMELINE_MODE,
        NetwDisplayDecl::TIMELINE_FORECAST
    );

    NETW_CHECK_CLOSE(head->min_lag(settings, 0, 8), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] the effective tick is the clock's, moved "
    "back by the whole ticks of the lag"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);

    head->set_display_lag(0.0);
    NETW_CHECK_EQ(head->effective_tick(100), 100);

    head->set_display_lag(2.5);
    NETW_CHECK_EQ(head->effective_tick(100), 97);

    head->set_display_lag(-1.5);
    NETW_CHECK_EQ(head->effective_tick(100), 101);
}

TEST_CASE(
    "[Networked][Display][Hosted] a fed stream adapts toward the floor rather "
    "than jumping onto it"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    const Ref<NetwDisplayDecl> settings = decl();
    settings->set_param(NetwDisplayDecl::PARAM_FLOOR_SMOOTHING, 0.5);
    settings->set_param(NetwDisplayDecl::PARAM_LAG_ADAPT_RATE, 0.5);

    head->dilate(settings, 1.0, 0, 0, false);

    NETW_CHECK_CLOSE(head->get_smoothed_floor(), 2.0, 0.0);
    NETW_CHECK_CLOSE(head->get_display_lag(), 1.0, 0.0);
    NETW_CHECK_EQ(head->get_starvation_ticks(), 0);

    head->dilate(settings, 1.0, 0, 0, false);
    NETW_CHECK_CLOSE(head->get_smoothed_floor(), 3.0, 0.0);
    NETW_CHECK_CLOSE(head->get_display_lag(), 2.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] one starved frame inside the grace window "
    "costs nothing, and the window resets on the first fed one"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    const Ref<NetwDisplayDecl> settings = decl();
    settings->set_param(NetwDisplayDecl::PARAM_STARVATION_GRACE_FRAMES, 3);
    settings->set_param(NetwDisplayDecl::PARAM_LAG_ADAPT_RATE, 0.0);
    settings->set_param(NetwDisplayDecl::PARAM_FLOOR_SMOOTHING, 0.0);

    head->dilate(settings, 1.0, 0, 0, true);
    head->dilate(settings, 1.0, 0, 0, true);
    NETW_CHECK_EQ(head->get_starvation_ticks(), 2);
    NETW_CHECK_CLOSE(head->get_display_lag(), 0.0, 0.0);

    head->dilate(settings, 1.0, 0, 0, false);
    NETW_CHECK_EQ(head->get_starvation_ticks(), 0);

    head->dilate(settings, 1.0, 0, 0, true);
    head->dilate(settings, 1.0, 0, 0, true);
    NETW_CHECK_CLOSE(head->get_display_lag(), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a starving stream grows the lag by the "
    "frame, bounded by the floor and the extra dilation allowed"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    const Ref<NetwDisplayDecl> settings = decl();
    settings->set_param(NetwDisplayDecl::PARAM_STARVATION_GRACE_FRAMES, 1);
    settings->set_param(NetwDisplayDecl::PARAM_STARVATION_GROWTH, 0.5);
    settings->set_param(NetwDisplayDecl::PARAM_MAX_EXTRA_DILATION, 0.0);
    settings->set_param(NetwDisplayDecl::PARAM_FLOOR_SMOOTHING, 1.0);

    head->dilate(settings, 2.0, 0, 0, true);
    NETW_CHECK_CLOSE(head->get_display_lag(), 1.0, 0.0);

    head->dilate(settings, 2.0, 0, 0, true);
    NETW_CHECK_CLOSE(head->get_display_lag(), 2.0, 0.0);

    head->dilate(settings, 2.0, 0, 0, true);
    NETW_CHECK_CLOSE(head->get_display_lag(), 3.0, 0.0);

    head->dilate(settings, 2.0, 0, 0, true);
    NETW_CHECK_CLOSE(head->get_display_lag(), 4.0, 0.0);

    head->dilate(settings, 2.0, 0, 0, true);
    NETW_CHECK_CLOSE(head->get_display_lag(), 4.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a bracketed playhead follows the clock one "
    "whole tick behind and carries no lag"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    head->set_display_lag(4.0);

    NETW_CHECK_CLOSE(head->place(10, 0.25, true), 0.25, 0.0);

    NETW_CHECK_EQ(head->get_display_tick(), 9);
    NETW_CHECK_CLOSE(head->get_display_lag(), 0.0, 0.0);

    head->place(0, 0.5, true);
    NETW_CHECK_EQ(head->get_display_tick(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a dilated playhead samples the lag behind "
    "the clock, fraction and all"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    head->set_display_lag(2.75);

    NETW_CHECK_CLOSE(head->place(10, 0.5, false), 0.75, 0.0);

    NETW_CHECK_EQ(head->get_display_tick(), 7);
    NETW_CHECK_CLOSE(head->get_display_lag(), 2.75, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a settled playhead is at its floor with no "
    "starvation behind it"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    const Ref<NetwDisplayDecl> settings = decl();
    head->set_display_lag(9.0);
    head->set_starvation_ticks(5);

    head->settle(settings, 1, 2);

    NETW_CHECK_CLOSE(head->get_display_lag(), 4.0, 0.0);
    NETW_CHECK_CLOSE(head->get_smoothed_floor(), 4.0, 0.0);
    NETW_CHECK_EQ(head->get_starvation_ticks(), 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] dilating with no settings is refused and "
    "moves nothing"
) {
    const Ref<NetwDisplayPlayhead> head = playhead(3);
    head->set_display_lag(2.0);

    ERR_PRINT_OFF;
    head->dilate(Ref<NetwDisplayDecl>(), 1.0, 0, 0, true);
    ERR_PRINT_ON;

    NETW_CHECK_CLOSE(head->get_display_lag(), 2.0, 0.0);
    NETW_CHECK_EQ(head->get_starvation_ticks(), 0);
}

} // namespace TestNetwDisplayPlayhead

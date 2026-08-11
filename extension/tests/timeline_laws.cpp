#include "support/netw_test.h"

#include "netw/timeline.hpp"

namespace TestNetwTimelineLaws {

using namespace godot;
using netw::NetwTimeline;

Dictionary motion(double p_x) {
    Dictionary out;
    out[StringName("motion")] = Vector2(real_t(p_x), 0.0);
    return out;
}

double x_of(const Dictionary &p_row) {
    return double(Vector2(p_row[StringName("motion")]).x);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] state carries forward and input does "
    "not"
) {
    const Ref<NetwTimeline> timeline = NetwTimeline::create(64);
    timeline->record_state(4, motion(1.0));
    timeline->record_input(4, motion(9.0));

    NETW_CHECK_EQ(x_of(timeline->state_at(4)), 1.0);
    NETW_CHECK_EQ(x_of(timeline->latest_state_at_or_before(7)), 1.0);
    NETW_CHECK_EQ(timeline->latest_state_tick_at_or_before(7), 4);

    // A tick with no state reads the newest before it, because nothing moved.
    CHECK(timeline->state_at(7).is_empty());

    // A tick with no input reads NOTHING, because a missing input is a
    // deliberate no-action and repeating the last one would invent a command.
    CHECK(timeline->input_at(7).is_empty());
    CHECK(!timeline->has_input_at(7));
    CHECK(timeline->has_input_at(4));
    NETW_CHECK_EQ(timeline->newest_input_tick(), 4);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] an empty timeline answers absence "
    "rather than a zeroth tick"
) {
    const Ref<NetwTimeline> timeline = NetwTimeline::create(64);
    CHECK(timeline->state_at(0).is_empty());
    CHECK(timeline->latest_state_at_or_before(0).is_empty());
    NETW_CHECK_EQ(timeline->latest_state_tick_at_or_before(0), -1);
    CHECK(timeline->input_at(0).is_empty());
    NETW_CHECK_EQ(timeline->newest_input_tick(), -1);
    NETW_CHECK_EQ(int(timeline->inputs_in_range(0, 10).size()), 0);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] the replay window is tick-ascending "
    "and skips the ticks that carry no input"
) {
    const Ref<NetwTimeline> timeline = NetwTimeline::create(64);
    timeline->record_input(3, motion(3.0));
    timeline->record_input(5, motion(5.0));
    timeline->record_input(6, motion(6.0));

    const Array window = timeline->inputs_in_range(3, 6);
    NETW_CHECK_EQ(int(window.size()), 3);
    NETW_CHECK_EQ(int64_t(Dictionary(window[0])[StringName("tick")]), 3);
    NETW_CHECK_EQ(int64_t(Dictionary(window[1])[StringName("tick")]), 5);
    NETW_CHECK_EQ(int64_t(Dictionary(window[2])[StringName("tick")]), 6);
    NETW_CHECK_EQ(
        x_of(Dictionary(Dictionary(window[2])[StringName("input")])),
        6.0
    );

    NETW_CHECK_EQ(int(timeline->inputs_in_range(6, 3).size()), 0);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] the trim watermark is a floor every "
    "read honours, and it never rewinds"
) {
    const Ref<NetwTimeline> timeline = NetwTimeline::create(64);
    timeline->record_state(2, motion(2.0));
    timeline->record_state(6, motion(6.0));
    timeline->record_input(2, motion(2.0));
    timeline->record_input(6, motion(6.0));

    timeline->trim_before(5);
    NETW_CHECK_EQ(timeline->floor(), 5);

    // The rows are still physically in the ring; what changed is that no read
    // is entitled to them, which is what makes the trim free.
    CHECK(timeline->state_at(2).is_empty());
    CHECK(timeline->input_at(2).is_empty());
    CHECK(timeline->latest_state_at_or_before(4).is_empty());
    NETW_CHECK_EQ(timeline->latest_state_tick_at_or_before(4), -1);

    // The read tick is AT the floor and the row it would carry forward is
    // below it. Rejecting only reads below the floor would answer this one
    // with a row the trim already retired.
    CHECK(timeline->latest_state_at_or_before(5).is_empty());
    NETW_CHECK_EQ(timeline->latest_state_tick_at_or_before(5), -1);
    NETW_CHECK_EQ(x_of(timeline->latest_state_at_or_before(6)), 6.0);
    NETW_CHECK_EQ(int(timeline->inputs_in_range(0, 6).size()), 1);

    // Monotonic: a later trim at an older tick cannot resurrect what an
    // earlier one retired, or a replay could walk rows already reclaimed.
    timeline->trim_before(1);
    NETW_CHECK_EQ(timeline->floor(), 5);
    CHECK(timeline->state_at(2).is_empty());
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] the retained window is bounded by the "
    "declared limit"
) {
    const Ref<NetwTimeline> timeline = NetwTimeline::create(4);
    for (int64_t tick = 0; tick < 10; ++tick) {
        timeline->record_state(tick, motion(double(tick)));
    }
    NETW_CHECK_EQ(x_of(timeline->latest_state_at_or_before(9)), 9.0);
    CHECK(timeline->state_at(2).is_empty());

    // A limit below one row would make a store that records and holds
    // nothing, which reads at every query as an entity with no history.
    const Ref<NetwTimeline> degenerate = NetwTimeline::create(0);
    degenerate->record_state(3, motion(3.0));
    NETW_CHECK_EQ(x_of(degenerate->state_at(3)), 3.0);
}

} // namespace TestNetwTimelineLaws

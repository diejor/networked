#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/tap.hpp"

#if defined(NETW_MODULE)
#include "core/core_bind.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/os.hpp>
#endif

namespace TestNetwPredictTapLaws {

using namespace godot;

namespace {

using netw::predict::TAP_AGGREGATE_PERIOD;
using netw::predict::tap_episode_stamp;
using netw::predict::tap_row_settled;
using netw::predict::tap_settlement;
using netw::predict::tap_stats_delta;

Dictionary stats_of(int64_t p_corrections, int p_arrivals) {
    Dictionary out;
    out[StringName("corrections")] = p_corrections;
    Array arrivals;
    arrivals.push_back(p_arrivals);
    out[StringName("arrivals")] = arrivals;
    return out;
}

Dictionary digest_of(int p_id, int p_revision) {
    Dictionary out;
    out[StringName("id")] = p_id;
    out[StringName("revision")] = p_revision;
    return out;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Tap] the first line is complete and a later "
    "line carries only what changed"
) {
    Dictionary carried;

    const Dictionary first = tap_stats_delta(carried, stats_of(3, 7), 0);
    CHECK(first.has(StringName("corrections")));
    CHECK(first.has(StringName("arrivals")));

    const Dictionary unchanged = tap_stats_delta(carried, stats_of(3, 7), 1);
    CHECK_FALSE(unchanged.has(StringName("corrections")));

    const Dictionary moved = tap_stats_delta(carried, stats_of(4, 7), 2);
    NETW_CHECK_EQ(int(moved[StringName("corrections")]), 4);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Tap] a cumulative aggregate rides every "
    "period rather than every line, because it is never small"
) {
    Dictionary carried;
    tap_stats_delta(carried, stats_of(3, 7), 0);

    const Dictionary between = tap_stats_delta(carried, stats_of(3, 9), 1);
    CHECK_FALSE(between.has(StringName("arrivals")));

    const Dictionary due
        = tap_stats_delta(carried, stats_of(3, 9), TAP_AGGREGATE_PERIOD);
    CHECK(due.has(StringName("arrivals")));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Tap] the episode stamp is empty for no "
    "episode and moves only when the revision does"
) {
    CHECK(tap_episode_stamp(Dictionary()).is_empty());

    const String held = tap_episode_stamp(digest_of(4, 9));
    CHECK_FALSE(held.is_empty());

    const bool same = held == tap_episode_stamp(digest_of(4, 9));
    CHECK(same);

    const bool revised = held != tap_episode_stamp(digest_of(4, 10));
    CHECK(revised);

    const bool reopened = held != tap_episode_stamp(digest_of(5, 9));
    CHECK(reopened);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Tap] a row settles on an acknowledgement or "
    "a substitution, and on nothing else"
) {
    CHECK(tap_row_settled(netw::predict::ROW_ACKED));
    CHECK(tap_row_settled(netw::predict::ROW_SUBSTITUTED));
    CHECK(tap_row_settled(netw::predict::ROW_SUPERSEDED));

    CHECK_FALSE(tap_row_settled(0));
    CHECK_FALSE(tap_row_settled(netw::predict::ROW_CLOSED));
    CHECK_FALSE(tap_row_settled(netw::predict::ROW_DIVERGENT));
    CHECK_FALSE(tap_row_settled(netw::predict::ROW_CHAIN_BROKEN));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Tap] a settlement carries the mutable "
    "overlay and never repeats the sealed evidence"
) {
    Dictionary row;
    row[StringName("transition")] = 12;
    row[StringName("flags")] = int(netw::predict::ROW_ACKED);
    row[StringName("attribution")] = 3;
    row[StringName("domain")] = 1;
    row[StringName("aligned_error")] = 0.5;
    row[StringName("pre_fp")] = 999;
    row[StringName("post_families")] = Array();

    const Dictionary settled = tap_settlement(row);

    NETW_CHECK_EQ(settled.size(), 5);
    NETW_CHECK_EQ(int(settled[StringName("transition")]), 12);
    NETW_CHECK_EQ(int(settled[StringName("attribution")]), 3);
    CHECK_FALSE(settled.has(StringName("pre_fp")));
    CHECK_FALSE(settled.has(StringName("post_families")));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Tap] the pump is what arms the tap, and it "
    "arms from the environment on the first frame it runs rather than from "
    "the session's own configuration"
) {
    Ref<netw::NetwMultiplayer> session;
    session.instantiate();

#if defined(NETW_MODULE)
    CoreBind::OS *os = CoreBind::OS::get_singleton();
#else
    godot::OS *os = godot::OS::get_singleton();
#endif
    const String held_dir = os->get_environment("NETW_PREDICT_TAP");
    const String held_stride = os->get_environment("NETW_PREDICT_TAP_EVERY");
    CHECK(session->predict_get_tap_cost().is_empty());

    os->set_environment("NETW_PREDICT_TAP", "user://");
    os->set_environment("NETW_PREDICT_TAP_EVERY", "2");

    session->predict_tap_pump();
    const Dictionary armed = session->predict_get_tap_cost();

    os->set_environment("NETW_PREDICT_TAP", held_dir);
    os->set_environment("NETW_PREDICT_TAP_EVERY", held_stride);
    session->predict_tap_close();

    CHECK(armed.has(StringName("drains")));
    NETW_CHECK_EQ(int64_t(armed[StringName("drains")]), int64_t(0));
}

} // namespace TestNetwPredictTapLaws

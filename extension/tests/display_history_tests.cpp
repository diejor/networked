// Laws for NetwDisplayHistory, one display channel's resample.
//
// The numbers below are the trace tests/crossing/display_arm.gd recorded from
// the GDScript history at c4f11bda, before any of it was native. They are the
// parity claim, restated where the tier reads them every run rather than left
// in a file nothing opens.

#include "support/netw_test.h"

#include "netw/display_history.hpp"
#include "netw/interpolate.hpp"

namespace TestNetwDisplayHistory {

using namespace godot;
using netw::NetwDisplayHistory;
using netw::NetwInterpolate;

// The tick period every projecting case advances by, exactly representable in
// binary so an age multiplied by it is exact rather than close.
constexpr double TICKTIME = 0.0625;
constexpr double EPSILON = 0.000001;
constexpr double HALF_TURN = 3.1415926535897932384626433833;

Ref<NetwDisplayHistory> make_history() {
    Ref<NetwDisplayHistory> history;
    history.instantiate();
    history->set_mode(NetwInterpolate::MODE_LERP);
    return history;
}

void check_planar(const Variant &value, double x, double y) {
    const Vector2 sampled = value;
    NETW_CHECK_CLOSE(sampled.x, x, EPSILON);
    NETW_CHECK_CLOSE(sampled.y, y, EPSILON);
}

Variant hold(
    const Ref<NetwDisplayHistory> &history,
    int64_t dt,
    double factor,
    const Variant &last_written,
    int64_t expected_interval_ticks
) {
    return history->sample(
        dt,
        factor,
        last_written,
        expected_interval_ticks,
        false,
        0,
        0.0,
        Variant(),
        false
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] a repeated value is not recorded twice"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(4, Vector2(2.0, 0.0), false);
    history->record(6, Vector2(2.0, 0.0), false);
    NETW_CHECK_EQ(history->newest_tick(), 4);

    history->record(8, Vector2(3.0, 0.0), false);
    NETW_CHECK_EQ(history->newest_tick(), 8);
}

TEST_CASE(
    "[Networked][Display][Hosted] clearing empties the history and frees the "
    "tick domain"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(4, Vector2(2.0, 0.0), false);
    history->clear();
    CHECK(history->is_empty());

    // The domain pins on the first record and a clear frees it, so a teleported
    // channel may be refed from the other one.
    history->record(40, Vector2(2.0, 0.0), true);
    NETW_CHECK_EQ(history->newest_tick(), 40);
}

TEST_CASE(
    "[Networked][Display][Hosted] an authoring stream brackets by authoring "
    "ticks"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(100, Vector2(0.0, 0.0), true);
    history->record(104, Vector2(40.0, 0.0), true);

    CHECK(history->bracketing_ticks(99) == Vector2i(-1, 100));
    CHECK(history->bracketing_ticks(100) == Vector2i(100, 104));
    CHECK(history->bracketing_ticks(102) == Vector2i(100, 104));
    CHECK(history->bracketing_ticks(104) == Vector2i(104, -1));
    CHECK(history->bracketing_ticks(106) == Vector2i(104, -1));

    CHECK(history->has_tick_after(102));
    CHECK_FALSE(history->has_tick_after(104));
}

TEST_CASE(
    "[Networked][Display][Hosted] before the first tick the last written value "
    "holds"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(10, Vector2(4.0, 0.0), false);
    check_planar(hold(history, 6, 0.0, Vector2(-1.0, -2.0), 3), -1.0, -2.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] an interior playhead lerps across its bracket"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(10, Vector2(0.0, 0.0), false);
    history->record(14, Vector2(8.0, 4.0), false);

    check_planar(hold(history, 12, 0.0, Vector2(), 3), 4.0, 2.0);
    check_planar(hold(history, 12, 0.25, Vector2(), 3), 4.5, 2.25);
    check_planar(hold(history, 12, 0.5, Vector2(), 3), 5.0, 2.5);
    check_planar(hold(history, 12, 0.75, Vector2(), 3), 5.5, 2.75);
}

TEST_CASE(
    "[Networked][Display][Hosted] a gap wider than twice the interval holds "
    "then lerps"
) {
    Ref<NetwDisplayHistory> wide = make_history();
    wide->record(0, Vector2(0.0, 0.0), false);
    wide->record(20, Vector2(20.0, 0.0), false);
    check_planar(hold(wide, 4, 0.0, Vector2(), 3), 0.0, 0.0);
    check_planar(hold(wide, 16, 0.0, Vector2(), 3), 0.0, 0.0);
    check_planar(hold(wide, 18, 0.0, Vector2(), 3), 6.666667, 0.0);
    check_planar(hold(wide, 19, 0.0, Vector2(), 3), 13.333334, 0.0);

    // Seven exceeds twice the interval and six does not, so the pair pins the
    // comparison as strict. A case that only ever takes the wide branch would
    // pass against a threshold set anywhere below its gap.
    Ref<NetwDisplayHistory> near = make_history();
    near->record(0, Vector2(0.0, 0.0), false);
    near->record(7, Vector2(14.0, 0.0), false);
    check_planar(hold(near, 2, 0.0, Vector2(), 3), 0.0, 0.0);
    check_planar(hold(near, 5, 0.0, Vector2(), 3), 4.666667, 0.0);

    Ref<NetwDisplayHistory> at_threshold = make_history();
    at_threshold->record(0, Vector2(0.0, 0.0), false);
    at_threshold->record(6, Vector2(12.0, 0.0), false);
    check_planar(hold(at_threshold, 1, 0.0, Vector2(), 3), 2.0, 0.0);
    check_planar(hold(at_threshold, 3, 0.0, Vector2(), 3), 6.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a bracket wider than the snap distance jumps"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->set_snap_distance(5.0);
    history->record(10, Vector2(0.0, 0.0), false);
    history->record(12, Vector2(40.0, 0.0), false);
    check_planar(hold(history, 10, 0.5, Vector2(), 3), 40.0, 0.0);
    CHECK(history->has_snapped());

    Ref<NetwDisplayHistory> near = make_history();
    near->set_snap_distance(5.0);
    near->record(10, Vector2(0.0, 0.0), false);
    near->record(12, Vector2(4.0, 0.0), false);
    check_planar(hold(near, 10, 0.5, Vector2(), 3), 1.0, 0.0);
    CHECK_FALSE(near->has_snapped());
}

TEST_CASE(
    "[Networked][Display][Hosted] a settled channel holds and sleeps until a "
    "distinct record"
) {
    Ref<NetwDisplayHistory> history = make_history();
    const Vector2 settled(5.0, 0.0);
    history->record(0, settled, false);
    check_planar(hold(history, 4, 0.0, settled, 3), 5.0, 0.0);
    CHECK(history->is_sleeping());

    // A repeat of the settled value is not a revival, or a quiet stream that
    // keeps resending would never let another writer take over.
    history->record(1, settled, false);
    CHECK(history->is_sleeping());

    history->record(6, Vector2(50.0, 0.0), false);
    CHECK_FALSE(history->is_sleeping());
}

TEST_CASE("[Networked][Display][Hosted] a moved channel holds awake") {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(0, Vector2(5.0, 0.0), false);
    check_planar(hold(history, 4, 0.0, Vector2(-30.0, 0.0), 3), 5.0, 0.0);
    CHECK_FALSE(history->is_sleeping());
}

TEST_CASE(
    "[Networked][Display][Hosted] the forecasting tail projects by finite "
    "difference"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(0, Vector2(0.0, 0.0), false);
    history->record(4, Vector2(8.0, 0.0), false);

    check_planar(
        history->sample(6, 0.0, Vector2(), 3, true, 8, TICKTIME, Variant(), false),
        12.0,
        0.0
    );
    CHECK(history->has_projected());
    NETW_CHECK_CLOSE(history->get_project_age(), 2.0, EPSILON);

    check_planar(
        history->sample(6, 0.5, Vector2(), 3, true, 8, TICKTIME, Variant(), false),
        13.0,
        0.0
    );
    NETW_CHECK_CLOSE(history->get_project_age(), 2.5, EPSILON);
}

TEST_CASE(
    "[Networked][Display][Hosted] an explicit derivative replaces the finite "
    "difference"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(0, Vector2(0.0, 0.0), false);
    history->record(4, Vector2(8.0, 0.0), false);
    check_planar(
        history->sample(
            6,
            0.0,
            Vector2(),
            3,
            true,
            8,
            TICKTIME,
            Vector2(0.0, 64.0),
            true
        ),
        8.0,
        8.0
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] the projected age is capped by the forecast "
    "budget"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->record(0, Vector2(0.0, 0.0), false);
    history->record(4, Vector2(8.0, 0.0), false);

    check_planar(
        history->sample(12, 0.0, Vector2(), 3, true, 0, TICKTIME, Variant(), false),
        8.0,
        0.0
    );
    NETW_CHECK_CLOSE(history->get_project_age(), 0.0, EPSILON);

    check_planar(
        history->sample(12, 0.0, Vector2(), 3, true, 2, TICKTIME, Variant(), false),
        12.0,
        0.0
    );
    NETW_CHECK_CLOSE(history->get_project_age(), 2.0, EPSILON);

    check_planar(
        history->sample(12, 0.0, Vector2(), 3, true, 8, TICKTIME, Variant(), false),
        24.0,
        0.0
    );
    NETW_CHECK_CLOSE(history->get_project_age(), 8.0, EPSILON);
}

TEST_CASE(
    "[Networked][Display][Hosted] a channel that cannot project falls through "
    "to the buffered hold"
) {
    // A projection too small to move the display is worth less than the sleep
    // it forfeits.
    Ref<NetwDisplayHistory> settled = make_history();
    settled->record(0, Vector2(5.0, 0.0), false);
    check_planar(
        settled->sample(
            6,
            0.0,
            Vector2(5.0, 0.0),
            3,
            true,
            8,
            TICKTIME,
            Variant(),
            false
        ),
        5.0,
        0.0
    );
    CHECK_FALSE(settled->has_projected());
    CHECK(settled->is_sleeping());

    Ref<NetwDisplayHistory> discrete = make_history();
    discrete->record(0, Color(0.25, 0.5, 0.75, 1.0), false);
    discrete->record(4, Color(0.75, 0.5, 0.25, 1.0), false);
    const Color shown = discrete->sample(
        6,
        0.0,
        Color(0.0, 0.0, 0.0, 1.0),
        3,
        true,
        8,
        TICKTIME,
        Variant(),
        false
    );
    CHECK(shown.is_equal_approx(Color(0.75, 0.5, 0.25, 1.0)));
    CHECK_FALSE(discrete->has_projected());
}

TEST_CASE(
    "[Networked][Display][Hosted] smoothing blends toward the sample and snaps "
    "past its distance"
) {
    Ref<NetwDisplayHistory> history = make_history();
    check_planar(
        history->smooth_toward(Vector2(), Vector2(8.0, 4.0), 0.25),
        2.0,
        1.0
    );
    check_planar(
        history->smooth_toward(Vector2(), Vector2(8.0, 4.0), 0.5),
        4.0,
        2.0
    );
    check_planar(
        history->smooth_toward(Vector2(), Vector2(8.0, 4.0), 1.0),
        8.0,
        4.0
    );
    CHECK_FALSE(history->has_snapped());

    Ref<NetwDisplayHistory> snapping = make_history();
    snapping->set_snap_distance(5.0);
    check_planar(
        snapping->smooth_toward(Vector2(), Vector2(40.0, 0.0), 0.25),
        40.0,
        0.0
    );
    CHECK(snapping->has_snapped());
    check_planar(
        snapping->smooth_toward(Vector2(), Vector2(2.0, 0.0), 0.25),
        0.5,
        0.0
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] the angle mode lerps the short way around"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->set_mode(NetwInterpolate::MODE_ANGLE);
    history->record(0, -3.0, false);
    history->record(4, 3.0, false);

    NETW_CHECK_CLOSE(
        double(hold(history, 2, 0.0, 0.0, 3)),
        -3.141593,
        0.000001
    );
    NETW_CHECK_CLOSE(
        double(hold(history, 2, 0.5, 0.0, 3)),
        -3.176991,
        0.000001
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] a rotation walks the arc between two "
    "quaternions"
) {
    Ref<NetwDisplayHistory> history = make_history();
    history->set_mode(NetwInterpolate::MODE_SLERP);
    history->record(0, Quaternion(), false);
    history->record(4, Quaternion(Vector3(0.0, 0.0, 1.0), HALF_TURN * 0.5), false);

    const Quaternion half = hold(history, 2, 0.0, Quaternion(), 3);
    NETW_CHECK_CLOSE(half.z, 0.382683, 0.000001);
    NETW_CHECK_CLOSE(half.w, 0.923880, 0.000001);

    const Quaternion past = hold(history, 2, 0.5, Quaternion(), 3);
    NETW_CHECK_CLOSE(past.z, 0.471397, 0.000001);
    NETW_CHECK_CLOSE(past.w, 0.881921, 0.000001);
}

} // namespace TestNetwDisplayHistory

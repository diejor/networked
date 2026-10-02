#include "support/netw_test.h"

#include "netw/lagcomp_book.hpp"
#include "netw/predict/drive.hpp"

namespace TestNetwTimelineLaws {

using namespace godot;
using netw::LagBook;
using netw::predict::Slot;

void open_motion_lane(Slot &r_slot) {
    r_slot.input_codec.append(
        StringName("motion"),
        Ref<netw::NetwQuantize>(),
        int(Variant::VECTOR2)
    );
    r_slot.input_codec.seal();
    r_slot.open_lane();
}

Dictionary motion(double p_x) {
    Dictionary out;
    out[StringName("motion")] = Vector2(real_t(p_x), 0.0);
    return out;
}

double x_of(const Dictionary &p_row) {
    return double(Vector2(p_row[StringName("motion")]).x);
}

netw::predict::StateRow motion_row(double p_x) {
    netw::predict::StateRow out;
    out.resize(1);
    out.set(0, Vector2(real_t(p_x), 0.0));
    return out;
}

void author(Slot &r_lane, int64_t p_tick, double p_x) {
    r_lane.record_authored_row(p_tick, p_tick, true, motion_row(p_x));
}

bool authored_any(const Slot &p_lane, int64_t p_tick) {
    netw::predict::StateRow row;
    p_lane.authored_row(p_tick, row);
    return row.any();
}

LocalVector<int64_t> window_of(
    const Slot &p_lane,
    int64_t p_from,
    int64_t p_to
) {
    LocalVector<int64_t> ticks;
    p_lane.authored_ticks(p_from, p_to, ticks);
    return ticks;
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] state carries forward and input does "
    "not"
) {
    LagBook book;
    Slot lane;
    open_motion_lane(lane);
    book.record(4, motion(1.0));
    author(lane, 4, 9.0);

    NETW_CHECK_CLOSE(x_of(book.sample(4)), 1.0, 0.0);
    NETW_CHECK_CLOSE(x_of(book.sample(7)), 1.0, 0.0);
    NETW_CHECK_EQ(book.sample_tick(7), 4);

    CHECK_FALSE(authored_any(lane, 7));
    CHECK(!lane.has_authored(7));
    CHECK(lane.has_authored(4));
    NETW_CHECK_EQ(lane.newest_authored(), 4);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] an empty timeline answers absence "
    "rather than a zeroth tick"
) {
    const LagBook book;
    Slot lane;
    open_motion_lane(lane);
    CHECK(book.sample(0).is_empty());
    NETW_CHECK_EQ(book.sample_tick(0), -1);
    CHECK_FALSE(authored_any(lane, 0));
    NETW_CHECK_EQ(lane.newest_authored(), -1);
    NETW_CHECK_EQ(int(window_of(lane, 0, 10).size()), 0);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] the replay window is tick-ascending "
    "and skips the ticks that carry no input"
) {
    Slot lane;
    open_motion_lane(lane);
    author(lane, 3, 3.0);
    author(lane, 5, 5.0);
    author(lane, 6, 6.0);

    const LocalVector<int64_t> window = window_of(lane, 3, 6);
    REQUIRE(window.size() == 3);
    NETW_CHECK_EQ(window[0], 3);
    NETW_CHECK_EQ(window[1], 5);
    NETW_CHECK_EQ(window[2], 6);
    netw::predict::StateRow input;
    lane.authored_row(window[2], input);
    REQUIRE(input.has(0));
    NETW_CHECK_CLOSE(double(Vector2(input.values[0]).x), 6.0, 0.0);

    NETW_CHECK_EQ(int(window_of(lane, 6, 3).size()), 0);
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] the trim watermark is a floor every "
    "read honours, and it never rewinds"
) {
    LagBook book;
    Slot lane;
    open_motion_lane(lane);
    book.record(2, motion(2.0));
    book.record(6, motion(6.0));
    author(lane, 2, 2.0);
    author(lane, 6, 6.0);

    book.trim_before(5);
    lane.trim_history(5);
    NETW_CHECK_EQ(book.floor(), 5);
    NETW_CHECK_EQ(lane.input_floor, 5);

    CHECK(book.sample(4).is_empty());
    NETW_CHECK_EQ(book.sample_tick(4), -1);
    CHECK_FALSE(authored_any(lane, 2));

    CHECK(book.sample(5).is_empty());
    NETW_CHECK_EQ(book.sample_tick(5), -1);
    NETW_CHECK_CLOSE(x_of(book.sample(6)), 6.0, 0.0);
    NETW_CHECK_EQ(int(window_of(lane, 0, 6).size()), 1);

    book.trim_before(1);
    lane.trim_history(1);
    NETW_CHECK_EQ(book.floor(), 5);
    NETW_CHECK_EQ(lane.input_floor, 5);
    CHECK(book.sample(2).is_empty());
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] a despawned entity stays answerable "
    "until the window passes the tick it died at"
) {
    LagBook book;
    for (int64_t tick = 0; tick < 30; ++tick) {
        book.record(tick, motion(double(tick)));
    }
    const int64_t despawned_at = 29;

    NETW_CHECK_CLOSE(x_of(book.sample(20)), 20.0, 0.0);

    book.trim_before(despawned_at + 1);
    CHECK(book.sample(20).is_empty());
}

TEST_CASE(
    "[Networked][Sync][Hosted][Timeline] the retained window is bounded by the "
    "declared limit"
) {
    LagBook book;
    book.set_window(4);
    for (int64_t tick = 0; tick < 10; ++tick) {
        book.record(tick, motion(double(tick)));
    }
    NETW_CHECK_CLOSE(x_of(book.sample(9)), 9.0, 0.0);
    CHECK(book.sample(2).is_empty());

    LagBook degenerate;
    degenerate.set_window(0);
    degenerate.record(3, motion(3.0));
    NETW_CHECK_CLOSE(x_of(degenerate.sample(3)), 3.0, 0.0);
}

} // namespace TestNetwTimelineLaws

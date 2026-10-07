#include "support/netw_test.h"

#include "netw/stage.hpp"

#if defined(DEBUG_ENABLED)

namespace TestNetwStage {

using namespace godot;
namespace stage = netw::stage;

double nanos_of(const Array &p_rows, stage::Stage p_stage) {
    return double(p_rows[2 + 2 * int(p_stage)]) * 1.0e9;
}

double total_of(const Array &p_rows) {
    double total = 0.0;
    for (int at = 0; at < stage::COUNT; ++at) {
        total += nanos_of(p_rows, stage::Stage(at));
    }
    return total;
}

TEST_CASE(
    "[Networked][Profiler][Hosted] a nested stage's time leaves its outer "
    "stage, so the rows sum to the time spent"
) {
    const stage::Book saved = stage::book;
    stage::book = stage::Book();
    stage::set_timing(true);

    NETW_CHECK_EQ(stage::enter(stage::SEND, 1000), stage::COUNT);
    NETW_CHECK_EQ(stage::enter(stage::FLUSH, 1300), stage::SEND);
    stage::leave(stage::FLUSH, stage::SEND, 1500);
    stage::leave(stage::SEND, stage::COUNT, 2000);
    const Array rows = stage::take();

    NETW_REQUIRE_EQ(rows.size(), 1 + 2 * stage::COUNT);
    const bool category = String(rows[0]) == String("networked");
    CHECK(category);
    const bool named = String(rows[1 + 2 * stage::SEND]) == String("send");
    CHECK(named);
    NETW_CHECK_CLOSE(nanos_of(rows, stage::SEND), 800.0, 0.5);
    NETW_CHECK_CLOSE(nanos_of(rows, stage::FLUSH), 200.0, 0.5);
    NETW_CHECK_CLOSE(total_of(rows), 1000.0, 0.5);

    stage::book = saved;
}

TEST_CASE(
    "[Networked][Profiler][Hosted] stage rows flush once per process frame "
    "and each frame starts from zero"
) {
    const stage::Book saved = stage::book;
    stage::book = stage::Book();

    CHECK(stage::turn(7));
    CHECK_FALSE(stage::turn(7));
    CHECK(stage::turn(8));

    stage::set_timing(true);
    stage::enter(stage::PULL, 10);
    stage::leave(stage::PULL, stage::COUNT, 50);
    const Array first = stage::take();
    NETW_CHECK_CLOSE(nanos_of(first, stage::PULL), 40.0, 0.5);
    const Array second = stage::take();
    NETW_CHECK_CLOSE(total_of(second), 0.0, 0.5);

    stage::set_timing(false);
    {
        stage::Scope idle(stage::SEND);
        NETW_CHECK_EQ(stage::book.open, stage::COUNT);
    }
    CHECK(stage::take().is_empty());

    stage::book = saved;
}

} // namespace TestNetwStage

#endif

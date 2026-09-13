#include "support/netw_test.h"

#include "netw/display/pump_stats.hpp"

namespace TestPumpStats {

using netw::display::PumpStats;

PumpStats counted(
    int runtimes,
    int starving,
    int sleeping,
    int projecting,
    int snaps,
    double max_display_lag,
    double max_forecast_age
) {
    PumpStats stats;
    stats.runtimes = runtimes;
    stats.starving = starving;
    stats.sleeping = sleeping;
    stats.projecting = projecting;
    stats.snaps = snaps;
    stats.max_display_lag = max_display_lag;
    stats.max_forecast_age = max_forecast_age;
    return stats;
}

TEST_CASE(
    "[Networked][Display][Hosted] a merge sums the counters and takes the "
    "larger maximum"
) {
    PumpStats left = counted(2, 1, 3, 4, 5, 6.5, 1.5);
    const PumpStats right = counted(7, 8, 9, 10, 11, 2.5, 12.5);

    left.merge(right);

    NETW_CHECK_EQ(left.runtimes, 9);
    NETW_CHECK_EQ(left.starving, 9);
    NETW_CHECK_EQ(left.sleeping, 12);
    NETW_CHECK_EQ(left.projecting, 14);
    NETW_CHECK_EQ(left.snaps, 16);
    NETW_CHECK_CLOSE(left.max_display_lag, 6.5, 0.0);
    NETW_CHECK_CLOSE(left.max_forecast_age, 12.5, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] two slices merge to the same record in "
    "either order"
) {
    PumpStats forward = counted(2, 1, 3, 4, 5, 6.5, 1.5);
    PumpStats backward = counted(7, 8, 9, 10, 11, 2.5, 12.5);

    forward.merge(counted(7, 8, 9, 10, 11, 2.5, 12.5));
    backward.merge(counted(2, 1, 3, 4, 5, 6.5, 1.5));

    NETW_CHECK_EQ(forward.runtimes, backward.runtimes);
    NETW_CHECK_EQ(forward.starving, backward.starving);
    NETW_CHECK_EQ(forward.sleeping, backward.sleeping);
    NETW_CHECK_EQ(forward.projecting, backward.projecting);
    NETW_CHECK_EQ(forward.snaps, backward.snaps);
    NETW_CHECK_CLOSE(forward.max_display_lag, backward.max_display_lag, 0.0);
    NETW_CHECK_CLOSE(forward.max_forecast_age, backward.max_forecast_age, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] a reset zeroes every counter, so one record "
    "serves every pump"
) {
    PumpStats stats = counted(2, 1, 3, 4, 5, 6.5, 1.5);

    stats.reset();

    NETW_CHECK_EQ(stats.runtimes, 0);
    NETW_CHECK_EQ(stats.starving, 0);
    NETW_CHECK_EQ(stats.sleeping, 0);
    NETW_CHECK_EQ(stats.projecting, 0);
    NETW_CHECK_EQ(stats.snaps, 0);
    NETW_CHECK_CLOSE(stats.max_display_lag, 0.0, 0.0);
    NETW_CHECK_CLOSE(stats.max_forecast_age, 0.0, 0.0);
}

} // namespace TestPumpStats

#include "support/netw_test.h"

#include "netw/pump_stats.hpp"

namespace TestNetwPumpStats {

using namespace godot;
using netw::NetwPumpStats;

Ref<NetwPumpStats> counted(
    int runtimes,
    int starving,
    int sleeping,
    int projecting,
    int snaps,
    double max_display_lag,
    double max_forecast_age
) {
    Ref<NetwPumpStats> stats;
    stats.instantiate();
    stats->set_runtimes(runtimes);
    stats->set_starving(starving);
    stats->set_sleeping(sleeping);
    stats->set_projecting(projecting);
    stats->set_snaps(snaps);
    stats->set_max_display_lag(max_display_lag);
    stats->set_max_forecast_age(max_forecast_age);
    return stats;
}

TEST_CASE(
    "[Networked][Display][Hosted] a merge sums the counters and takes the "
    "larger maximum"
) {
    const Ref<NetwPumpStats> left = counted(2, 1, 3, 4, 5, 6.5, 1.5);
    const Ref<NetwPumpStats> right = counted(7, 8, 9, 10, 11, 2.5, 12.5);

    left->merge(right);

    NETW_CHECK_EQ(left->get_runtimes(), 9);
    NETW_CHECK_EQ(left->get_starving(), 9);
    NETW_CHECK_EQ(left->get_sleeping(), 12);
    NETW_CHECK_EQ(left->get_projecting(), 14);
    NETW_CHECK_EQ(left->get_snaps(), 16);
    NETW_CHECK_CLOSE(left->get_max_display_lag(), 6.5, 0.0);
    NETW_CHECK_CLOSE(left->get_max_forecast_age(), 12.5, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] two slices merge to the same record in "
    "either order"
) {
    const Ref<NetwPumpStats> forward = counted(2, 1, 3, 4, 5, 6.5, 1.5);
    const Ref<NetwPumpStats> backward = counted(7, 8, 9, 10, 11, 2.5, 12.5);

    forward->merge(counted(7, 8, 9, 10, 11, 2.5, 12.5));
    backward->merge(counted(2, 1, 3, 4, 5, 6.5, 1.5));

    NETW_CHECK_EQ(forward->get_runtimes(), backward->get_runtimes());
    NETW_CHECK_EQ(forward->get_starving(), backward->get_starving());
    NETW_CHECK_EQ(forward->get_sleeping(), backward->get_sleeping());
    NETW_CHECK_EQ(forward->get_projecting(), backward->get_projecting());
    NETW_CHECK_EQ(forward->get_snaps(), backward->get_snaps());
    NETW_CHECK_CLOSE(
        forward->get_max_display_lag(),
        backward->get_max_display_lag(),
        0.0
    );
    NETW_CHECK_CLOSE(
        forward->get_max_forecast_age(),
        backward->get_max_forecast_age(),
        0.0
    );
}

TEST_CASE(
    "[Networked][Display][Hosted] a reset zeroes every counter, so one record "
    "serves every pump"
) {
    const Ref<NetwPumpStats> stats = counted(2, 1, 3, 4, 5, 6.5, 1.5);

    stats->reset();

    NETW_CHECK_EQ(stats->get_runtimes(), 0);
    NETW_CHECK_EQ(stats->get_starving(), 0);
    NETW_CHECK_EQ(stats->get_sleeping(), 0);
    NETW_CHECK_EQ(stats->get_projecting(), 0);
    NETW_CHECK_EQ(stats->get_snaps(), 0);
    NETW_CHECK_CLOSE(stats->get_max_display_lag(), 0.0, 0.0);
    NETW_CHECK_CLOSE(stats->get_max_forecast_age(), 0.0, 0.0);
}

TEST_CASE(
    "[Networked][Display][Hosted] merging nothing is refused and leaves the "
    "record alone"
) {
    const Ref<NetwPumpStats> stats = counted(2, 1, 3, 4, 5, 6.5, 1.5);

    ERR_PRINT_OFF;
    stats->merge(Ref<NetwPumpStats>());
    ERR_PRINT_ON;

    NETW_CHECK_EQ(stats->get_runtimes(), 2);
    NETW_CHECK_CLOSE(stats->get_max_display_lag(), 6.5, 0.0);
}

} // namespace TestNetwPumpStats

#include "support/netw_test.h"

#include "netw/monitor.hpp"

#if defined(DEBUG_ENABLED)

#include "godot/performance.hpp"

namespace TestNetwMonitor {

using namespace godot;
namespace monitor = netw::monitor;

monitor::Upload upload_of(int64_t p_peer, int64_t p_sent, double p_budget) {
    monitor::Upload upload;
    upload.peer = p_peer;
    upload.sent_bytes = p_sent;
    upload.budget_per_second = p_budget;
    return upload;
}

TEST_CASE(
    "[Networked][Profiler][Hosted] monitor rates come from counter deltas "
    "and never run backwards"
) {
    monitor::Sample before;
    before.late_inputs = 10;
    before.corrections = 5;
    before.uploads.push_back(upload_of(2, 1000, 4000.0));
    monitor::Sample now;
    now.peers = 2;
    now.late_inputs = 40;
    now.corrections = 2;
    now.replayed_ticks = 30;
    now.uploads.push_back(upload_of(2, 3000, 4000.0));
    now.uploads.push_back(upload_of(3, 500, 4000.0));

    double values[monitor::COUNT] = {};
    monitor::read(before, now, 0.5, values);
    NETW_CHECK_CLOSE(values[monitor::PEERS], 2.0, 1.0e-9);
    NETW_CHECK_CLOSE(values[monitor::LATE_INPUTS], 60.0, 1.0e-9);
    NETW_CHECK_CLOSE(values[monitor::CORRECTIONS], 0.0, 1.0e-9);
    NETW_CHECK_CLOSE(values[monitor::REPLAYED_TICKS], 60.0, 1.0e-9);
    NETW_CHECK_CLOSE(values[monitor::UPLOAD_BUDGET_USED], 1.0, 1.0e-9);

    monitor::read(before, now, 0.0, values);
    NETW_CHECK_CLOSE(values[monitor::LATE_INPUTS], 0.0, 1.0e-9);
    NETW_CHECK_CLOSE(values[monitor::UPLOAD_BUDGET_USED], 0.0, 1.0e-9);
}

#if defined(NETW_TIER_HOSTED)
TEST_CASE(
    "[Networked][Profiler] a debug build lists every Networked monitor in "
    "Performance"
) {
    for (int at = 0; at < monitor::COUNT; ++at) {
        const StringName id(monitor::SPECS[at].id);
        NETW_FORMAT_TEXT(listed, monitor::SPECS[at].id);
        CAPTURE(listed);
        CHECK(netw::gd::has_custom_monitor(id));
    }
}
#endif

} // namespace TestNetwMonitor

#endif

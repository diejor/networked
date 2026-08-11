#include "support/netw_test.h"

#include "netw/predict/engine.hpp"

namespace TestNetwPredictAxisLaws {

using namespace godot;
using namespace netw;
using namespace netw::predict;

struct AxisRow {
    const char *label;
    int schedule;
    int role;
    int correction;
    int restore;
    int island;
    bool admitted;
};

const AxisRow AXIS[] = {
    {"predict, the reference point",
     int(Schedule::TICK),
     int(Role::PREDICT),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"consume, the server's authoritative intake",
     int(Schedule::TICK),
     int(Role::CONSUME),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"host local, authority and controller at once",
     int(Schedule::TICK),
     int(Role::HOST_LOCAL),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"remote, which a fallback can latch into authoring",
     int(Schedule::TICK),
     int(Role::REMOTE),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"simulate without an island has no timeline to drive from",
     int(Schedule::TICK),
     int(Role::SIMULATE),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     false},
    {"simulate inside a declared island",
     int(Schedule::TICK),
     int(Role::SIMULATE),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_DECLARED,
     true},
    {"replay restores exactly and re-runs the commands",
     int(Schedule::TICK),
     int(Role::PREDICT),
     int(CorrectionMode::REPLAY),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"replay projects the reseed the same way a snap restore does",
     int(Schedule::TICK),
     int(Role::PREDICT),
     int(CorrectionMode::REPLAY),
     int(RestoreMode::EXTRAPOLATED),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"a snap restore projects a carried field forward",
     int(Schedule::FRAME),
     int(Role::PREDICT),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXTRAPOLATED),
     NetwPredictionEngine::ISLAND_NONE,
     true},
    {"auto names a body-type guess a slot cannot resolve",
     int(Schedule::TICK),
     int(Role::PREDICT),
     int(CorrectionMode::AUTO),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     false},
    {"a joint pass replays one shared floor a tick declares",
     int(Schedule::TICK),
     int(Role::PREDICT),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_JOINT,
     true},
    {"a stepped schedule buys the same fixed steps a tick does",
     int(Schedule::STEPPED),
     int(Role::PREDICT),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_JOINT,
     true},
    {"a stepped simulate member of a joint group",
     int(Schedule::STEPPED),
     int(Role::SIMULATE),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_JOINT,
     true},
    {"a joint pass under a frame schedule, whose steps vary per drive",
     int(Schedule::FRAME),
     int(Role::PREDICT),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_JOINT,
     false},
    {"an island beyond the declared ones",
     int(Schedule::TICK),
     int(Role::PREDICT),
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_JOINT + 1,
     false},
    {"a role beyond the declared ones",
     int(Schedule::TICK),
     int(Role::SIMULATE) + 1,
     int(CorrectionMode::SNAP),
     int(RestoreMode::EXACT),
     NetwPredictionEngine::ISLAND_NONE,
     false},
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] every role is driven and only an "
    "unperformable configuration is refused"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    for (const AxisRow &row : AXIS) {
        CAPTURE(row.label);
        NETW_CHECK_EQ(
            pool->supports(
                row.schedule,
                row.role,
                row.correction,
                row.restore,
                row.island
            ),
            row.admitted
        );
    }
}

// The refusal has to be loud at the one place a caller can read it, because
// `configure` answers false and stores nothing rather than raising. A shell
// that ignored the answer would run an entity the pool never configured.
TEST_CASE(
    "[Networked][Predict][Hosted][Law] a slot holds an axis point exactly "
    "when the pool admits it"
) {
    for (const AxisRow &row : AXIS) {
        CAPTURE(row.label);
        Ref<NetwPredictionEngine> pool;
        pool.instantiate();
        const int64_t slot = pool->open(Ref<NetwPredictDeclaration>());
        NETW_CHECK_EQ(
            pool->configure(
                slot,
                row.schedule,
                row.role,
                row.correction,
                row.restore,
                6,
                row.island
            ),
            row.admitted
        );
        if (row.admitted) {
            NETW_CHECK_EQ(pool->role_of(slot), row.role);
            NETW_CHECK_EQ(pool->schedule_of(slot), row.schedule);
        }
    }
}

} // namespace TestNetwPredictAxisLaws

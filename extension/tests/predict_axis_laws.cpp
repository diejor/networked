#include "support/netw_test.h"

#include "netw/predict/axes.hpp"
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
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
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

TEST_CASE(
    "[Networked][Predict][Hosted][Law] a slot holds an axis point exactly "
    "when the pool admits it"
) {
    for (const AxisRow &row : AXIS) {
        CAPTURE(row.label);
        NetwPredictionEngine held_pool;
        NetwPredictionEngine *const pool = &held_pool;
        const int64_t slot = pool->open();
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

struct RoleRow {
    const char *label;
    int input_source;
    int sim_mode;
    int role;
};

const RoleRow ROLES[] = {
    {"a local controller that is also authority",
     int(InputSource::LOCAL),
     int(SimMode::AUTHORITATIVE),
     int(Role::HOST_LOCAL)},
    {"a local controller speculating ahead of authority",
     int(InputSource::LOCAL),
     int(SimMode::SPECULATIVE),
     int(Role::PREDICT)},
    {"a local controller that runs no simulation",
     int(InputSource::LOCAL),
     int(SimMode::DISPLAY),
     int(Role::REMOTE)},
    {"authority consuming a received command",
     int(InputSource::RECEIVED),
     int(SimMode::AUTHORITATIVE),
     int(Role::CONSUME)},
    {"a received command speculated on, which no peer reaches",
     int(InputSource::RECEIVED),
     int(SimMode::SPECULATIVE),
     int(Role::REMOTE)},
    {"a received command nothing simulates",
     int(InputSource::RECEIVED),
     int(SimMode::DISPLAY),
     int(Role::REMOTE)},
    {"a predicted command taken as authority, which no peer reaches",
     int(InputSource::PREDICTED),
     int(SimMode::AUTHORITATIVE),
     int(Role::REMOTE)},
    {"a predicted command stepping a replicated member",
     int(InputSource::PREDICTED),
     int(SimMode::SPECULATIVE),
     int(Role::SIMULATE)},
    {"a predicted command nothing simulates",
     int(InputSource::PREDICTED),
     int(SimMode::DISPLAY),
     int(Role::REMOTE)},
    {"no command, on the authority that owns the body",
     int(InputSource::NONE),
     int(SimMode::AUTHORITATIVE),
     int(Role::HOST_LOCAL)},
    {"no command, on a peer reproducing a body something drags",
     int(InputSource::NONE),
     int(SimMode::SPECULATIVE),
     int(Role::SIMULATE)},
    {"no command and no simulation",
     int(InputSource::NONE),
     int(SimMode::DISPLAY),
     int(Role::REMOTE)},
};

TEST_CASE(
    "[Networked][Predict][Hosted][Law] every command and simulation pair "
    "names one role, and a contradictory pair names REMOTE"
) {
    for (const RoleRow &row : ROLES) {
        NETW_FORMAT_TEXT(label_text, row.label);
        CAPTURE(label_text);
        NETW_CHECK_EQ(
            NetwPredictionEngine::role_for_axes(row.input_source, row.sim_mode),
            row.role
        );
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] an archetype declares a whole axis "
    "point and NONE declares nothing"
) {
    const ArchetypeAxes none = archetype_axes(int(Archetype::NONE));
    CHECK_FALSE(none.declared);
    CHECK_FALSE(none.declares_snap_restore);
    CHECK_FALSE(none.declares_teleport_threshold);

    const ArchetypeAxes kinematic = archetype_axes(int(Archetype::KINEMATIC));
    CHECK(kinematic.declared);
    NETW_CHECK_EQ(kinematic.schedule, int(Schedule::TICK));
    NETW_CHECK_EQ(kinematic.missing_policy, int(MissingInput::STALL));
    NETW_CHECK_EQ(
        kinematic.recovery_policy,
        int(RecoveryPolicy::REBASE_REPLAY)
    );
    CHECK_FALSE(kinematic.declares_snap_restore);
    CHECK_FALSE(kinematic.declares_teleport_threshold);

    const ArchetypeAxes solver = archetype_axes(int(Archetype::SOLVER_BODY));
    CHECK(solver.declared);
    NETW_CHECK_EQ(solver.schedule, int(Schedule::FRAME));
    NETW_CHECK_EQ(solver.missing_policy, int(MissingInput::REPEAT_LAST));
    NETW_CHECK_EQ(solver.recovery_policy, int(RecoveryPolicy::REBASE_RECOVER));
    CHECK(solver.declares_snap_restore);
    NETW_CHECK_EQ(solver.snap_restore, int(RestoreMode::EXTRAPOLATED));
    CHECK(solver.declares_teleport_threshold);
    NETW_CHECK_CLOSE(solver.teleport_threshold, 3.0, 1e-9);

    Dictionary published
        = NetwPredictionEngine::archetype_axes(int(Archetype::SOLVER_BODY));
    const bool published_declared = published[StringName("declared")];
    const int64_t published_schedule = published[StringName("schedule")];
    const int64_t published_policy = published[StringName("recovery_policy")];
    const bool published_teleports
        = published[StringName("declares_teleport_threshold")];
    CHECK(published_declared);
    NETW_CHECK_EQ(published_schedule, solver.schedule);
    NETW_CHECK_EQ(published_policy, solver.recovery_policy);
    CHECK(published_teleports);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Law] a recovery policy runs through REPLAY "
    "only when it rebases by replaying"
) {
    NETW_CHECK_EQ(
        NetwPredictionEngine::correction_for_recovery_policy(
            int(RecoveryPolicy::REBASE_REPLAY)
        ),
        int(CorrectionMode::REPLAY)
    );
    NETW_CHECK_EQ(
        NetwPredictionEngine::correction_for_recovery_policy(
            int(RecoveryPolicy::REBASE_RECOVER)
        ),
        int(CorrectionMode::SNAP)
    );
    NETW_CHECK_EQ(
        NetwPredictionEngine::correction_for_recovery_policy(
            int(RecoveryPolicy::DELAY_CLOSED)
        ),
        int(CorrectionMode::SNAP)
    );
    NETW_CHECK_EQ(
        NetwPredictionEngine::correction_for_recovery_policy(
            int(RecoveryPolicy::OBSERVE)
        ),
        int(CorrectionMode::SNAP)
    );
}

} // namespace TestNetwPredictAxisLaws

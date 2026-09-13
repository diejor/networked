#pragma once

#include "netw/prediction_core.hpp"

namespace netw::predict {

enum class InputSource : int {
    LOCAL = 0,
    RECEIVED = 1,
    PREDICTED = 2,
    NONE = 3,
};

enum class SimMode : int {
    AUTHORITATIVE = 0,
    SPECULATIVE = 1,
    DISPLAY = 2,
};

enum class Archetype : int {
    NONE = 0,
    KINEMATIC = 1,
    SOLVER_BODY = 2,
};

struct ArchetypeAxes {
    bool declared = false;
    int schedule = int(Schedule::TICK);
    int missing_policy = int(MissingInput::STALL);
    int recovery_policy = int(RecoveryPolicy::REBASE_REPLAY);
    int snap_restore = int(RestoreMode::EXACT);
    double teleport_threshold = 0.0;
    bool declares_snap_restore = false;
    bool declares_teleport_threshold = false;
};

ArchetypeAxes archetype_axes(int p_archetype);

int role_for_axes(int p_input_source, int p_sim_mode);

int correction_for_recovery_policy(int p_policy);

} // namespace netw::predict

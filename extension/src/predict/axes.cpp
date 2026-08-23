#include "netw/predict/axes.hpp"

namespace netw {

namespace predict {

ArchetypeAxes archetype_axes(int p_archetype) {
    ArchetypeAxes axes;
    switch (Archetype(p_archetype)) {
        case Archetype::KINEMATIC: {
            axes.declared = true;
            axes.schedule = int(Schedule::TICK);
            axes.missing_policy = int(MissingInput::STALL);
            axes.recovery_policy = int(RecoveryPolicy::REBASE_REPLAY);
        } break;
        case Archetype::SOLVER_BODY: {
            axes.declared = true;
            axes.schedule = int(Schedule::FRAME);
            axes.missing_policy = int(MissingInput::REPEAT_LAST);
            axes.recovery_policy = int(RecoveryPolicy::REBASE_RECOVER);
            axes.snap_restore = int(RestoreMode::EXTRAPOLATED);
            axes.declares_snap_restore = true;
            axes.teleport_threshold = 3.0;
            axes.declares_teleport_threshold = true;
        } break;
        default:
            break;
    }
    return axes;
}

int role_for_axes(int p_input_source, int p_sim_mode) {
    if (p_sim_mode == int(SimMode::DISPLAY)) {
        return int(Role::REMOTE);
    }
    if (p_input_source == int(InputSource::LOCAL)) {
        return p_sim_mode == int(SimMode::AUTHORITATIVE)
            ? int(Role::HOST_LOCAL)
            : int(Role::PREDICT);
    }
    if (p_input_source == int(InputSource::RECEIVED)
        && p_sim_mode == int(SimMode::AUTHORITATIVE)) {
        return int(Role::CONSUME);
    }
    if (p_input_source == int(InputSource::PREDICTED)
        && p_sim_mode == int(SimMode::SPECULATIVE)) {
        return int(Role::SIMULATE);
    }
    return int(Role::REMOTE);
}

int correction_for_recovery_policy(int p_policy) {
    return p_policy == int(RecoveryPolicy::REBASE_REPLAY)
        ? int(CorrectionMode::REPLAY)
        : int(CorrectionMode::SNAP);
}

} // namespace predict

} // namespace netw

#include "netw/api/predict.hpp"

#include <algorithm>

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Dictionary NetwPredict::joint_floor(
    const Dictionary &p_bases,
    const Dictionary &p_relay_floors,
    int64_t p_epoch_floor,
    int64_t p_history_floor,
    int64_t p_present
) {
    int64_t settled = p_present;
    const Array bases = p_bases.keys();
    for (int at = 0; at < bases.size(); ++at) {
        const int64_t basis = p_bases[bases[at]];
        if (basis >= 0) {
            settled = std::min(settled, basis);
        }
    }
    const Array relayed = p_relay_floors.keys();
    for (int at = 0; at < relayed.size(); ++at) {
        const int64_t basis = p_relay_floors[relayed[at]];
        if (basis >= 0) {
            settled = std::min(settled, basis);
        }
    }
    if (p_epoch_floor >= 0) {
        settled = std::min(settled, p_epoch_floor);
    }
    bool heal = false;
    if (settled < p_history_floor) {
        heal = true;
        settled = p_present;
    }
    Dictionary out;
    out[StringName("floor")] = settled;
    out[StringName("heal")] = heal;
    return out;
}

int NetwPredict::joint_cell(
    bool p_authored,
    bool p_relayed,
    bool p_predictor_valid
) {
    if (p_authored) {
        return CELL_PROVENANCE_AUTHORED;
    }
    if (p_relayed) {
        return CELL_PROVENANCE_RELAYED;
    }
    if (p_predictor_valid) {
        return CELL_PROVENANCE_SUBSTITUTED;
    }
    return CELL_PROVENANCE_COAST;
}

String NetwPredict::schedule_name(Schedule p_schedule) {
    switch (p_schedule) {
        case SCHEDULE_TICK:
            return "TICK";
        case SCHEDULE_FRAME:
            return "FRAME";
        case SCHEDULE_STEPPED:
            return "STEPPED";
        default:
            return String::num_int64(p_schedule);
    }
}

String NetwPredict::drive_kind_name(DriveKind p_kind) {
    switch (p_kind) {
        case DRIVE_KIND_NONE:
            return "NONE";
        case DRIVE_KIND_FRESH:
            return "FRESH";
        case DRIVE_KIND_REPEAT:
            return "REPEAT";
        case DRIVE_KIND_HOLD:
            return "HOLD";
        case DRIVE_KIND_STARVED:
            return "STARVED";
        case DRIVE_KIND_FOLD_DRIVE:
            return "FOLD_DRIVE";
        case DRIVE_KIND_MISSING:
            return "MISSING";
        case DRIVE_KIND_SUBSTITUTED:
            return "SUBSTITUTED";
        default:
            return String::num_int64(p_kind);
    }
}

String NetwPredict::verdict_reason_name(VerdictReason p_reason) {
    switch (p_reason) {
        case VERDICT_REASON_NONE:
            return "NONE";
        case VERDICT_REASON_AWAITING_RECONSTRUCTION:
            return "AWAITING_RECONSTRUCTION";
        case VERDICT_REASON_REALIGN_PENDING:
            return "REALIGN_PENDING";
        case VERDICT_REASON_RESEED_IGNORED:
            return "RESEED_IGNORED";
        case VERDICT_REASON_PROBATION_REQUARANTINE:
            return "PROBATION_REQUARANTINE";
        case VERDICT_REASON_EVIDENCE_EXHAUSTED:
            return "EVIDENCE_EXHAUSTED";
        case VERDICT_REASON_TRANSPORT_PENDING:
            return "TRANSPORT_PENDING";
        case VERDICT_REASON_DISSIPATE_PENDING:
            return "DISSIPATE_PENDING";
        case VERDICT_REASON_DISSIPATED:
            return "DISSIPATED";
        case VERDICT_REASON_WITNESS_DEFERRED:
            return "WITNESS_DEFERRED";
        case VERDICT_REASON_DECLINED:
            return "DECLINED";
        default:
            return String::num_int64(p_reason);
    }
}

String NetwPredict::episode_state_name(EpisodeState p_state) {
    switch (p_state) {
        case EPISODE_STATE_OPEN:
            return "OPEN";
        case EPISODE_STATE_CLOSED:
            return "CLOSED";
        case EPISODE_STATE_FALLBACK:
            return "FALLBACK";
        default:
            return String::num_int64(p_state);
    }
}
String NetwPredict::operator_outcome_name(int p_outcome) {
    switch (p_outcome) {
        case OPERATOR_OUTCOME_PENDING:
            return "PENDING";
        case OPERATOR_OUTCOME_CONTRACTED:
            return "CONTRACTED";
        case OPERATOR_OUTCOME_FAILED_TO_CONTRACT:
            return "FAILED_TO_CONTRACT";
        case OPERATOR_OUTCOME_INTRODUCED_BOUNDARY:
            return "INTRODUCED_BOUNDARY";
        case OPERATOR_OUTCOME_WITHHELD:
            return "WITHHELD";
        default:
            return String::num_int64(p_outcome);
    }
}
void NetwPredict::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD(
            "joint_floor",
            "bases",
            "relay_floors",
            "epoch_floor",
            "history_floor",
            "present"
        ),
        &NetwPredict::joint_floor
    );
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD("joint_cell", "authored", "relayed", "predictor_valid"),
        &NetwPredict::joint_cell
    );
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD("schedule_name", "schedule"),
        &NetwPredict::schedule_name
    );
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD("drive_kind_name", "kind"),
        &NetwPredict::drive_kind_name
    );
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD("verdict_reason_name", "reason"),
        &NetwPredict::verdict_reason_name
    );
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD("episode_state_name", "state"),
        &NetwPredict::episode_state_name
    );
    ClassDB::bind_static_method(
        "NetwPredict",
        D_METHOD("operator_outcome_name", "outcome"),
        &NetwPredict::operator_outcome_name
    );
    BIND_CONSTANT(ACK_AGE_MAX);

    BIND_ENUM_CONSTANT(ROLE_PREDICT);
    BIND_ENUM_CONSTANT(ROLE_CONSUME);
    BIND_ENUM_CONSTANT(ROLE_HOST_LOCAL);
    BIND_ENUM_CONSTANT(ROLE_REMOTE);
    BIND_ENUM_CONSTANT(ROLE_SIMULATE);

    BIND_ENUM_CONSTANT(INPUT_SOURCE_LOCAL);
    BIND_ENUM_CONSTANT(INPUT_SOURCE_RECEIVED);
    BIND_ENUM_CONSTANT(INPUT_SOURCE_PREDICTED);
    BIND_ENUM_CONSTANT(INPUT_SOURCE_NONE);

    BIND_ENUM_CONSTANT(SIM_MODE_AUTHORITATIVE);
    BIND_ENUM_CONSTANT(SIM_MODE_SPECULATIVE);
    BIND_ENUM_CONSTANT(SIM_MODE_DISPLAY);

    BIND_ENUM_CONSTANT(SCHEDULE_TICK);
    BIND_ENUM_CONSTANT(SCHEDULE_FRAME);
    BIND_ENUM_CONSTANT(SCHEDULE_STEPPED);

    BIND_ENUM_CONSTANT(CONTACT_CLASS_NONE);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_DECLARED_SUPPORT);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_OTHER_STATIC);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_PREDICTED_DYNAMIC);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_UNPREDICTED_DYNAMIC);
    BIND_ENUM_CONSTANT(CONTACT_CLASS_KINEMATIC_PROXY);

    BIND_ENUM_CONSTANT(WITNESS_CLASS_NONE);
    BIND_ENUM_CONSTANT(WITNESS_CLASS_SUPPORT);
    BIND_ENUM_CONSTANT(WITNESS_CLASS_STATIC);
    BIND_ENUM_CONSTANT(WITNESS_CLASS_DYNAMIC_ENTITY);

    BIND_ENUM_CONSTANT(COMMAND_ORIGIN_PREDICTED);
    BIND_ENUM_CONSTANT(COMMAND_ORIGIN_RELAYED);

    BIND_ENUM_CONSTANT(DRIVE_KIND_NONE);
    BIND_ENUM_CONSTANT(DRIVE_KIND_FRESH);
    BIND_ENUM_CONSTANT(DRIVE_KIND_REPEAT);
    BIND_ENUM_CONSTANT(DRIVE_KIND_HOLD);
    BIND_ENUM_CONSTANT(DRIVE_KIND_STARVED);
    BIND_ENUM_CONSTANT(DRIVE_KIND_FOLD_DRIVE);
    BIND_ENUM_CONSTANT(DRIVE_KIND_MISSING);
    BIND_ENUM_CONSTANT(DRIVE_KIND_SUBSTITUTED);

    BIND_ENUM_CONSTANT(TRIGGER_SHAPE_NONE);
    BIND_ENUM_CONSTANT(TRIGGER_SHAPE_MIXED);
    BIND_ENUM_CONSTANT(TRIGGER_SHAPE_ALL_WITHHELD);

    BIND_ENUM_CONSTANT(CONSUME_ACTION_REPLAY);
    BIND_ENUM_CONSTANT(CONSUME_ACTION_HOLD);
    BIND_ENUM_CONSTANT(CONSUME_ACTION_STARVED);

    BIND_ENUM_CONSTANT(EXACT_VERDICT_UNJUDGED);
    BIND_ENUM_CONSTANT(EXACT_VERDICT_EQUAL);
    BIND_ENUM_CONSTANT(EXACT_VERDICT_UNEQUAL);

    BIND_ENUM_CONSTANT(MISSING_INPUT_STALL);
    BIND_ENUM_CONSTANT(MISSING_INPUT_REPEAT_LAST);

    BIND_ENUM_CONSTANT(CORRECTION_MODE_AUTO);
    BIND_ENUM_CONSTANT(CORRECTION_MODE_REPLAY);
    BIND_ENUM_CONSTANT(CORRECTION_MODE_SNAP);

    BIND_ENUM_CONSTANT(RESTORE_MODE_EXACT);
    BIND_ENUM_CONSTANT(RESTORE_MODE_EXTRAPOLATED);

    BIND_ENUM_CONSTANT(ARCHETYPE_NONE);
    BIND_ENUM_CONSTANT(ARCHETYPE_KINEMATIC);
    BIND_ENUM_CONSTANT(ARCHETYPE_SOLVER_BODY);

    BIND_ENUM_CONSTANT(RECOVERY_POLICY_REBASE_REPLAY);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_REBASE_RECOVER);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_DELAY_CLOSED);
    BIND_ENUM_CONSTANT(RECOVERY_POLICY_OBSERVE);

    BIND_ENUM_CONSTANT(FIDELITY_PROXY);
    BIND_ENUM_CONSTANT(FIDELITY_SIMULATED);

    BIND_ENUM_CONSTANT(PROMOTION_NONE);
    BIND_ENUM_CONSTANT(PROMOTION_NEAREST);
    BIND_ENUM_CONSTANT(PROMOTION_WITHIN);
    BIND_ENUM_CONSTANT(PROMOTION_ALL);

    BIND_ENUM_CONSTANT(PACING_SPECULATE);
    BIND_ENUM_CONSTANT(PACING_DELAY_CLOSED);

    BIND_ENUM_CONSTANT(RECONCILE_INDEPENDENT);
    BIND_ENUM_CONSTANT(RECONCILE_JOINT);

    BIND_ENUM_CONSTANT(CELL_PROVENANCE_COAST);
    BIND_ENUM_CONSTANT(CELL_PROVENANCE_SUBSTITUTED);
    BIND_ENUM_CONSTANT(CELL_PROVENANCE_RELAYED);
    BIND_ENUM_CONSTANT(CELL_PROVENANCE_AUTHORED);

    BIND_ENUM_CONSTANT(BREACH_RESPONSE_PREDICT_THROUGH);
    BIND_ENUM_CONSTANT(BREACH_RESPONSE_DEMOTE);

    BIND_ENUM_CONSTANT(EPISODE_STATE_OPEN);
    BIND_ENUM_CONSTANT(EPISODE_STATE_CLOSED);
    BIND_ENUM_CONSTANT(EPISODE_STATE_FALLBACK);

    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_PENDING);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_CONTRACTED);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_FAILED_TO_CONTRACT);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_INTRODUCED_BOUNDARY);
    BIND_ENUM_CONSTANT(OPERATOR_OUTCOME_WITHHELD);

    BIND_ENUM_CONSTANT(VERDICT_REASON_NONE);
    BIND_ENUM_CONSTANT(VERDICT_REASON_AWAITING_RECONSTRUCTION);
    BIND_ENUM_CONSTANT(VERDICT_REASON_REALIGN_PENDING);
    BIND_ENUM_CONSTANT(VERDICT_REASON_RESEED_IGNORED);
    BIND_ENUM_CONSTANT(VERDICT_REASON_PROBATION_REQUARANTINE);
    BIND_ENUM_CONSTANT(VERDICT_REASON_EVIDENCE_EXHAUSTED);
    BIND_ENUM_CONSTANT(VERDICT_REASON_TRANSPORT_PENDING);
    BIND_ENUM_CONSTANT(VERDICT_REASON_DISSIPATE_PENDING);
    BIND_ENUM_CONSTANT(VERDICT_REASON_DISSIPATED);
    BIND_ENUM_CONSTANT(VERDICT_REASON_WITNESS_DEFERRED);
    BIND_ENUM_CONSTANT(VERDICT_REASON_DECLINED);
}

} // namespace netw

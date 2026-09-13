#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwPredict : public godot::Object {
    GDCLASS(NetwPredict, godot::Object)

protected:
    static void _bind_methods();

public:
    static constexpr int ACK_AGE_MAX = 64;

    enum Role {
        ROLE_PREDICT = 0,
        ROLE_CONSUME = 1,
        ROLE_HOST_LOCAL = 2,
        ROLE_REMOTE = 3,
        ROLE_SIMULATE = 4,
    };

    enum InputSource {
        INPUT_SOURCE_LOCAL = 0,
        INPUT_SOURCE_RECEIVED = 1,
        INPUT_SOURCE_PREDICTED = 2,
        INPUT_SOURCE_NONE = 3,
    };

    enum SimMode {
        SIM_MODE_AUTHORITATIVE = 0,
        SIM_MODE_SPECULATIVE = 1,
        SIM_MODE_DISPLAY = 2,
    };

    enum Schedule {
        SCHEDULE_TICK = 0,
        SCHEDULE_FRAME = 1,
        SCHEDULE_STEPPED = 2,
    };

    enum ContactClass {
        CONTACT_CLASS_NONE = 0,
        CONTACT_CLASS_DECLARED_SUPPORT = 1,
        CONTACT_CLASS_OTHER_STATIC = 2,
        CONTACT_CLASS_PREDICTED_DYNAMIC = 3,
        CONTACT_CLASS_UNPREDICTED_DYNAMIC = 4,
        CONTACT_CLASS_KINEMATIC_PROXY = 5,
    };

    enum WitnessClass {
        WITNESS_CLASS_NONE = 0,
        WITNESS_CLASS_SUPPORT = 1,
        WITNESS_CLASS_STATIC = 2,
        WITNESS_CLASS_DYNAMIC_ENTITY = 4,
    };

    enum CommandOrigin {
        COMMAND_ORIGIN_PREDICTED = 0,
        COMMAND_ORIGIN_RELAYED = 1,
    };

    enum DriveKind : int {
        DRIVE_KIND_NONE = 0,
        DRIVE_KIND_FRESH = 1,
        DRIVE_KIND_REPEAT = 2,
        DRIVE_KIND_HOLD = 3,
        DRIVE_KIND_STARVED = 4,
        DRIVE_KIND_FOLD_DRIVE = 5,
        DRIVE_KIND_MISSING = 6,
        DRIVE_KIND_SUBSTITUTED = 7,
    };

    enum TriggerShape {
        TRIGGER_SHAPE_NONE = 0,
        TRIGGER_SHAPE_MIXED = 1,
        TRIGGER_SHAPE_ALL_WITHHELD = 2,
    };

    enum ConsumeAction {
        CONSUME_ACTION_REPLAY = 0,
        CONSUME_ACTION_HOLD = 1,
        CONSUME_ACTION_STARVED = 2,
    };

    enum ExactVerdict {
        EXACT_VERDICT_UNJUDGED = 0,
        EXACT_VERDICT_EQUAL = 1,
        EXACT_VERDICT_UNEQUAL = 2,
    };

    enum MissingInput {
        MISSING_INPUT_STALL = 0,
        MISSING_INPUT_REPEAT_LAST = 1,
    };

    enum CorrectionMode {
        CORRECTION_MODE_AUTO = 0,
        CORRECTION_MODE_REPLAY = 1,
        CORRECTION_MODE_SNAP = 2,
    };

    enum RestoreMode {
        RESTORE_MODE_EXACT = 0,
        RESTORE_MODE_EXTRAPOLATED = 1,
    };

    enum Archetype {
        ARCHETYPE_NONE = 0,
        ARCHETYPE_KINEMATIC = 1,
        ARCHETYPE_SOLVER_BODY = 2,
    };

    enum RecoveryPolicy {
        RECOVERY_POLICY_REBASE_REPLAY = 0,
        RECOVERY_POLICY_REBASE_RECOVER = 1,
        RECOVERY_POLICY_DELAY_CLOSED = 2,
        RECOVERY_POLICY_OBSERVE = 3,
    };

    enum Fidelity : int {
        FIDELITY_PROXY = 0,
        FIDELITY_SIMULATED = 1,
    };

    enum Promotion {
        PROMOTION_NONE = 0,
        PROMOTION_NEAREST = 1,
        PROMOTION_WITHIN = 2,
        PROMOTION_ALL = 3,
    };

    enum Pacing {
        PACING_SPECULATE = 0,
        PACING_DELAY_CLOSED = 1,
    };

    enum Reconcile {
        RECONCILE_INDEPENDENT = 0,
        RECONCILE_JOINT = 1,
    };

    enum CellProvenance {
        CELL_PROVENANCE_COAST = 0,
        CELL_PROVENANCE_SUBSTITUTED = 1,
        CELL_PROVENANCE_RELAYED = 2,
        CELL_PROVENANCE_AUTHORED = 3,
    };

    enum BreachResponse {
        BREACH_RESPONSE_PREDICT_THROUGH = 0,
        BREACH_RESPONSE_DEMOTE = 1,
    };

    enum EpisodeState {
        EPISODE_STATE_OPEN = 0,
        EPISODE_STATE_CLOSED = 1,
        EPISODE_STATE_FALLBACK = 2,
    };

    enum OperatorOutcome {
        OPERATOR_OUTCOME_PENDING = 0,
        OPERATOR_OUTCOME_CONTRACTED = 1,
        OPERATOR_OUTCOME_FAILED_TO_CONTRACT = 2,
        OPERATOR_OUTCOME_INTRODUCED_BOUNDARY = 3,
        OPERATOR_OUTCOME_WITHHELD = 4,
    };

    enum VerdictReason {
        VERDICT_REASON_NONE = 0,
        VERDICT_REASON_AWAITING_RECONSTRUCTION = 1,
        VERDICT_REASON_REALIGN_PENDING = 2,
        VERDICT_REASON_RESEED_IGNORED = 3,
        VERDICT_REASON_PROBATION_REQUARANTINE = 4,
        VERDICT_REASON_EVIDENCE_EXHAUSTED = 5,
        VERDICT_REASON_TRANSPORT_PENDING = 6,
        VERDICT_REASON_DISSIPATE_PENDING = 7,
        VERDICT_REASON_DISSIPATED = 8,
        VERDICT_REASON_WITNESS_DEFERRED = 9,
        VERDICT_REASON_DECLINED = 10,
    };

    static godot::Dictionary joint_floor(
        const godot::Dictionary &p_bases,
        const godot::Dictionary &p_relay_floors,
        int64_t p_epoch_floor,
        int64_t p_history_floor,
        int64_t p_present
    );

    static int joint_cell(
        bool p_authored,
        bool p_relayed,
        bool p_predictor_valid
    );

    static godot::String schedule_name(Schedule p_schedule);
    static godot::String drive_kind_name(DriveKind p_kind);
    static godot::String verdict_reason_name(VerdictReason p_reason);
    static godot::String episode_state_name(EpisodeState p_state);
    static godot::String operator_outcome_name(int p_outcome);
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredict::Role);
VARIANT_ENUM_CAST(netw::NetwPredict::InputSource);
VARIANT_ENUM_CAST(netw::NetwPredict::SimMode);
VARIANT_ENUM_CAST(netw::NetwPredict::Schedule);
VARIANT_ENUM_CAST(netw::NetwPredict::ContactClass);
VARIANT_ENUM_CAST(netw::NetwPredict::WitnessClass);
VARIANT_ENUM_CAST(netw::NetwPredict::CommandOrigin);
VARIANT_ENUM_CAST(netw::NetwPredict::DriveKind);
VARIANT_ENUM_CAST(netw::NetwPredict::TriggerShape);
VARIANT_ENUM_CAST(netw::NetwPredict::ConsumeAction);
VARIANT_ENUM_CAST(netw::NetwPredict::ExactVerdict);
VARIANT_ENUM_CAST(netw::NetwPredict::MissingInput);
VARIANT_ENUM_CAST(netw::NetwPredict::CorrectionMode);
VARIANT_ENUM_CAST(netw::NetwPredict::RestoreMode);
VARIANT_ENUM_CAST(netw::NetwPredict::Archetype);
VARIANT_ENUM_CAST(netw::NetwPredict::RecoveryPolicy);
VARIANT_ENUM_CAST(netw::NetwPredict::Fidelity);
VARIANT_ENUM_CAST(netw::NetwPredict::Promotion);
VARIANT_ENUM_CAST(netw::NetwPredict::Pacing);
VARIANT_ENUM_CAST(netw::NetwPredict::Reconcile);
VARIANT_ENUM_CAST(netw::NetwPredict::CellProvenance);
VARIANT_ENUM_CAST(netw::NetwPredict::BreachResponse);
VARIANT_ENUM_CAST(netw::NetwPredict::EpisodeState);
VARIANT_ENUM_CAST(netw::NetwPredict::OperatorOutcome);
VARIANT_ENUM_CAST(netw::NetwPredict::VerdictReason);

#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/api/simulation_handle.hpp"

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

    enum ContactClass {
        CONTACT_CLASS_NONE = 0,
        CONTACT_CLASS_DECLARED_SUPPORT = 1,
        CONTACT_CLASS_OTHER_STATIC = 2,
        CONTACT_CLASS_PREDICTED_DYNAMIC = 3,
        CONTACT_CLASS_UNPREDICTED_DYNAMIC = 4,
        CONTACT_CLASS_KINEMATIC_PROXY = 5,
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

    enum Archetype {
        ARCHETYPE_NONE = 0,
        ARCHETYPE_SCRIPTED = 1,
        ARCHETYPE_SOLVER_BODY = 2,
    };

    enum RecoveryPolicy {
        RECOVERY_POLICY_REBASE_REPLAY = 0,
        RECOVERY_POLICY_REBASE_RECOVER = 1,
        RECOVERY_POLICY_DELAY_CLOSED = 2,
        RECOVERY_POLICY_OBSERVE = 3,
        RECOVERY_POLICY_AUTO = 4,
    };

    enum Reconcile {
        RECONCILE_INDEPENDENT = 0,
        RECONCILE_JOINT = 1,
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

    enum Attribution {
        ATTRIBUTION_UNKNOWN = 0,
        ATTRIBUTION_PRE_STATE = 1,
        ATTRIBUTION_COMMAND = 2,
        ATTRIBUTION_ENVIRONMENT = 3,
        ATTRIBUTION_TOPOLOGY = 4,
        ATTRIBUTION_EXECUTION = 5,
        ATTRIBUTION_CONTACT = 6,
        ATTRIBUTION_CLOSURE = 7,
    };

    enum Operator {
        OPERATOR_NONE = 0,
        OPERATOR_REBASE_PROJECTED = 1,
        OPERATOR_REBASE_EXACT = 2,
        OPERATOR_FULL_CLOSURE = 3,
        OPERATOR_TRANSPORT_DELTA = 4,
        OPERATOR_RESEED = 5,
        OPERATOR_RESEED_ALIGN = 6,
        OPERATOR_DEMOTE = 7,
        OPERATOR_DISSIPATE = 8,
        OPERATOR_JOINT_REBASE = 9,
    };

    enum Domain {
        DOMAIN_IN = 0,
        DOMAIN_OUT = 1,
    };
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredict::ContactClass);
VARIANT_ENUM_CAST(netw::NetwPredict::CommandOrigin);
VARIANT_ENUM_CAST(netw::NetwPredict::DriveKind);
VARIANT_ENUM_CAST(netw::NetwPredict::TriggerShape);
VARIANT_ENUM_CAST(netw::NetwPredict::ConsumeAction);
VARIANT_ENUM_CAST(netw::NetwPredict::ExactVerdict);
VARIANT_ENUM_CAST(netw::NetwPredict::MissingInput);
VARIANT_ENUM_CAST(netw::NetwPredict::Archetype);
VARIANT_ENUM_CAST(netw::NetwPredict::RecoveryPolicy);
VARIANT_ENUM_CAST(netw::NetwPredict::Reconcile);
VARIANT_ENUM_CAST(netw::NetwPredict::BreachResponse);
VARIANT_ENUM_CAST(netw::NetwPredict::EpisodeState);
VARIANT_ENUM_CAST(netw::NetwPredict::OperatorOutcome);
VARIANT_ENUM_CAST(netw::NetwPredict::Attribution);
VARIANT_ENUM_CAST(netw::NetwPredict::Operator);
VARIANT_ENUM_CAST(netw::NetwPredict::Domain);

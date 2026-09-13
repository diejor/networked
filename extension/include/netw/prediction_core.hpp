#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/predict.hpp"

namespace netw {

enum class Domain : int {
    IN_DOMAIN = 0,
    OUT_OF_DOMAIN = 1,
};

enum class ExactVerdict : int {
    UNJUDGED = 0,
    EQUAL = 1,
    UNEQUAL = 2,
};

enum class Schedule : int {
    TICK = 0,
    FRAME = 1,
    STEPPED = 2,
};

inline bool rerunnable(int p_schedule) {
    return p_schedule == int(Schedule::TICK)
        || p_schedule == int(Schedule::STEPPED);
}

inline bool island_pass_admits(int p_pass_schedule, int p_schedule) {
    return p_pass_schedule == int(Schedule::TICK)
        ? rerunnable(p_schedule)
        : p_schedule == p_pass_schedule;
}

enum class Role : int {
    PREDICT = 0,
    CONSUME = 1,
    HOST_LOCAL = 2,
    REMOTE = 3,
    SIMULATE = 4,
};

enum class DriveKind : int {
    NONE = 0,
    FRESH = 1,
    REPEAT = 2,
    HOLD = 3,
    STARVED = 4,
    FOLD_DRIVE = 5,
    MISSING = 6,
    SUBSTITUTED = 7,
};

enum class ConsumeAction : int {
    REPLAY = 0,
    HOLD = 1,
    STARVED = 2,
};

enum class MissingInput : int {
    STALL = 0,
    REPEAT_LAST = 1,
};

enum class CorrectionMode : int {
    AUTO = 0,
    REPLAY = 1,
    SNAP = 2,
};

enum class RestoreMode : int {
    EXACT = 0,
    EXTRAPOLATED = 1,
};

enum class RecoveryPolicy : int {
    REBASE_REPLAY = 0,
    REBASE_RECOVER = 1,
    DELAY_CLOSED = 2,
    OBSERVE = 3,
};

enum class Attribution : int {
    UNKNOWN = 0,
    PRE_STATE = 1,
    COMMAND = 2,
    ENVIRONMENT = 3,
    TOPOLOGY = 4,
    EXECUTION = 5,
    CONTACT = 6,
    CLOSURE = 7,
};

enum class StateFamily : int {
    NONE = 0,
    POSE = 1,
    MOMENTUM = 2,
    CONTROLLER_LATCH = 3,
};

struct PredictionVerdict {
    int domain = 0;
    int verdict = 0;
    bool corrected = false;
    double max_error = 0.0;
};

struct Fold {
    int64_t label = -1;
    bool fresh = false;
    DriveKind kind = DriveKind::NONE;
};

struct RecoveryPlan {
    godot::Dictionary restore;
    godot::Dictionary write;
    bool teleport = false;
    bool skip = true;
};

class NetwPredictRecovery : public godot::RefCounted {
    GDCLASS(NetwPredictRecovery, godot::RefCounted)

    godot::Dictionary restored;
    godot::Dictionary written;
    bool teleported = false;
    bool skipped = true;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictRecovery> of(
        const godot::Dictionary &p_restore,
        const godot::Dictionary &p_write,
        bool p_teleport,
        bool p_skip
    );

    godot::Dictionary restore() const {
        return restored;
    }

    godot::Dictionary write() const {
        return written;
    }

    bool teleport() const {
        return teleported;
    }

    bool skip() const {
        return skipped;
    }
};

struct Judgement {
    double divergence = 0.0;
    bool corrected = false;
};

class NetwPredictJudgement : public godot::RefCounted {
    GDCLASS(NetwPredictJudgement, godot::RefCounted)

    Judgement judged;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictJudgement> of(
        double p_divergence,
        bool p_corrected
    );

    double divergence() const {
        return judged.divergence;
    }

    bool corrected() const {
        return judged.corrected;
    }
};

class NetwPredictFold : public godot::RefCounted {
    GDCLASS(NetwPredictFold, godot::RefCounted)

    Fold decided;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictFold> of(
        int64_t p_label,
        bool p_fresh,
        NetwPredict::DriveKind p_kind
    );

    int64_t label() const {
        return decided.label;
    }

    bool fresh() const {
        return decided.fresh;
    }

    NetwPredict::DriveKind kind() const {
        return static_cast<NetwPredict::DriveKind>(int(decided.kind));
    }
};

namespace prediction_core {

#if defined(NETW_TESTS)
int64_t records_minted();
void note_record_mint();
#define NETW_NOTE_RECORD_MINT() ::netw::prediction_core::note_record_mint()
#else
#define NETW_NOTE_RECORD_MINT() ((void)0)
#endif

Judgement judge(
    int domain,
    int verdict,
    const godot::Dictionary &predicted,
    const godot::Dictionary &payload,
    const godot::Dictionary &wiring,
    godot::Dictionary field_sink
);

godot::Ref<NetwPredictJudgement> evaluate(
    int domain,
    int verdict,
    const godot::Dictionary &predicted,
    const godot::Dictionary &payload,
    const godot::Dictionary &wiring,
    godot::Dictionary field_sink
);

int domain_of(
    bool declared,
    bool approximate,
    int64_t label,
    int64_t window_until
);

Fold fold(
    int64_t latest_input_tick,
    int64_t last_driven_input_tick,
    int64_t frame_tick
);

godot::Ref<NetwPredictFold> predict_fold(
    int64_t latest_input_tick,
    int64_t last_driven_input_tick,
    int64_t frame_tick
);

int consume_action(int depth, int buffer);

godot::Dictionary compared_state(
    const godot::Dictionary &payload,
    const godot::Dictionary &causal
);

godot::Dictionary project_payload(
    const godot::Dictionary &payload,
    const godot::Dictionary &projection,
    double age
);

godot::Dictionary converge_toward(
    const godot::Dictionary &restore,
    const godot::Dictionary &current,
    const godot::Dictionary &rules,
    const godot::Dictionary &angles
);

RecoveryPlan recover_plan(
    const godot::Dictionary &payload,
    int policy,
    int correction,
    int snap_restore,
    const godot::Dictionary &projection,
    const godot::Dictionary &current,
    const godot::Dictionary &pose_errors,
    const godot::Dictionary &wiring,
    const godot::Dictionary &verdict,
    double tick_delta
);

godot::Ref<NetwPredictRecovery> recover(
    const godot::Dictionary &payload,
    int policy,
    int correction,
    int snap_restore,
    const godot::Dictionary &projection,
    const godot::Dictionary &current,
    const godot::Dictionary &pose_errors,
    const godot::Dictionary &wiring,
    const godot::Dictionary &verdict,
    double tick_delta
);

godot::Dictionary escalation_after(
    int streak,
    int last_sign,
    double last_divergence,
    double divergence,
    int sign
);

int measure(
    const godot::Dictionary &field_sink,
    const godot::Dictionary &tolerances
);

int attribute(
    bool pre_equal,
    bool command_equal,
    bool environment_equal,
    bool topology_equal,
    bool raw_equal,
    bool witness_equal,
    int local_evidence,
    int peer_evidence,
    bool evidence_complete
);

int64_t raw_state_fingerprint(const godot::Dictionary &payload);

int64_t fact_fingerprint(const godot::Dictionary &facts);

int64_t topology_fingerprint(const godot::Dictionary &facts, int quantum);

int contact_count_bucket(int count);

int differing_family(
    const godot::PackedInt32Array &local,
    const godot::PackedInt32Array &peer
);

int64_t window_after(int64_t label, int64_t cooldown, int64_t window_until);

int64_t environment_digest(int64_t epoch, const godot::Dictionary &samples);

godot::Dictionary delta_direction(
    const godot::StringName &field,
    const godot::Variant &delta
);

godot::Dictionary guard_projection(
    const godot::Dictionary &projection,
    const godot::Dictionary &field_divergence,
    double epsilon,
    const godot::Dictionary &epsilon_overrides,
    int max_restore_ticks,
    int ack_age_ticks,
    double tick_delta
);

godot::Variant pose_delta(
    const godot::Variant &target,
    const godot::Variant &current,
    bool is_angle
);

godot::Variant pose_advance(
    const godot::Variant &current,
    const godot::Variant &delta
);

bool teleport_reached(
    const godot::Dictionary &pose_errors,
    const godot::Dictionary &thresholds,
    double default_threshold
);

PredictionVerdict evaluate_struct_verdict(
    int domain,
    int verdict,
    const godot::Dictionary &predicted,
    const godot::Dictionary &payload,
    const godot::Dictionary &wiring
);

godot::Dictionary calculate_joint_floor(
    const godot::Dictionary &bases,
    const godot::Dictionary &relay_floors,
    int64_t epoch_floor,
    int64_t history_floor,
    int64_t present
);

int calculate_joint_cell(bool authored, bool relayed, bool predictor_valid);

int admit_frame(
    int channel,
    int sender,
    int controller,
    bool receiver_is_server,
    bool payload_empty,
    int route_verdict
);

} // namespace prediction_core

} // namespace netw

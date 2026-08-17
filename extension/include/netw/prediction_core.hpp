#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

// The prediction vocabulary, owned by NetwPredict and NetwPredictJournal in
// GDScript until those shells cross. Every value here is that enum's value: a
// kernel answering a number of its own would agree with any case written
// against it and disagree with the engine, which is how the first crossing of
// `domain_of` shipped answering 1 and 2 for a Domain that reads 0 and 1. These
// cross ClassDB as plain ints, so a bound method returns `int` and casts at the
// boundary rather than exposing a second enum beside the GDScript one.
//
// TODO: delete the mirror and bind these as owned enums, with VARIANT_ENUM_CAST
// and BIND_ENUM_CONSTANT, once NetwPredictJournal and NetwPredict cross and
// stop publishing these values from GDScript.

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

// FRAME buys a step count the frame decides rather than the schedule, so a
// replay of it is a different run and a joint group cannot share a floor.
inline bool rerunnable(int p_schedule) {
    return p_schedule == int(Schedule::TICK)
        || p_schedule == int(Schedule::STEPPED);
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

// One comparison of a predicted transition against the payload that judged it.
// `max_error` is the worst per-field error behind the verdict, so a caller that
// only reports magnitude never has to re-walk the fields to find it.
struct PredictionVerdict {
    int domain = 0;
    int verdict = 0;
    bool corrected = false;
    double max_error = 0.0;
};

/* The one drive a pass applies, as the decision itself rather than as a
 * Dictionary of it.
 *
 * A drive path reaches this once per entity per tick, which is where the
 * Dictionary the seam still speaks would be paid for.
 */
struct Fold {
    int64_t label = -1;
    bool fresh = false;
    DriveKind kind = DriveKind::NONE;
};

/* The single write that corrects one settled divergence, staged whole.
 *
 * Minted rather than read back, because the seam it crosses is overridable and
 * a game deciding for itself has to answer in the currency the engine does.
 * `restore` and `write` are keyed by property name, which is the altitude the
 * seam speaks: the engine's own plan is keyed by field slot.
 */
class NetwPredictRecovery : public RefCounted {
    GDCLASS(NetwPredictRecovery, RefCounted)

    Dictionary restored;
    Dictionary written;
    bool teleported = false;
    bool skipped = true;

protected:
    static void _bind_methods();

public:
    static Ref<NetwPredictRecovery> of(
        const Dictionary &p_restore,
        const Dictionary &p_write,
        bool p_teleport,
        bool p_skip
    );

    Dictionary restore() const {
        return restored;
    }

    Dictionary write() const {
        return written;
    }

    bool teleport() const {
        return teleported;
    }

    bool skip() const {
        return skipped;
    }
};

/* What one acknowledged transition was judged to be worth.
 *
 * `divergence` is a magnitude and `corrected` is not a threshold over it: in
 * domain the two peers claimed reproducibility, so an exact predicate decides
 * and the magnitude is only ever reported.
 */
struct Judgement {
    double divergence = 0.0;
    bool corrected = false;
};

/* One judgement, as the record the seam answers with.
 *
 * Minted rather than read back, because the seam it crosses is overridable and
 * a game deciding for itself has to answer in the currency the engine does.
 */
class NetwPredictJudgement : public RefCounted {
    GDCLASS(NetwPredictJudgement, RefCounted)

    Judgement judged;

protected:
    static void _bind_methods();

public:
    static Ref<NetwPredictJudgement> of(double p_divergence, bool p_corrected);

    double divergence() const {
        return judged.divergence;
    }

    bool corrected() const {
        return judged.corrected;
    }
};

/* One drive choice, as the record the seam answers with.
 *
 * Minted rather than read back, because the seam it crosses is overridable and
 * a game deciding for itself has to answer in the currency the engine does.
 */
class NetwPredictFold : public RefCounted {
    GDCLASS(NetwPredictFold, RefCounted)

    Fold decided;

protected:
    static void _bind_methods();

public:
    static Ref<NetwPredictFold> of(int64_t p_label, bool p_fresh, int p_kind);

    int64_t label() const {
        return decided.label;
    }

    bool fresh() const {
        return decided.fresh;
    }

    int kind() const {
        return int(decided.kind);
    }
};

// Whole-engine simulation and prediction calculations for replicated entities.
class NetwPredictionCore : public RefCounted {
    GDCLASS(NetwPredictionCore, RefCounted)

protected:
    static void _bind_methods();

public:
    static Ref<NetwPredictJudgement> evaluate(
        int domain,
        int verdict,
        const Dictionary &predicted,
        const Dictionary &payload,
        const Dictionary &wiring,
        Dictionary field_sink
    );

    static int domain_of(
        bool declared,
        bool approximate,
        int64_t label,
        int64_t window_until
    );

    static Fold fold(
        int64_t latest_input_tick,
        int64_t last_driven_input_tick,
        int64_t frame_tick
    );

    static Ref<NetwPredictFold> predict_fold(
        int64_t latest_input_tick,
        int64_t last_driven_input_tick,
        int64_t frame_tick
    );

    static int consume_action(int depth, int buffer);

    static Dictionary compared_state(
        const Dictionary &payload,
        const Dictionary &causal
    );

    static Dictionary project_payload(
        const Dictionary &payload,
        const Dictionary &projection,
        double age
    );

    static Dictionary converge_toward(
        const Dictionary &restore,
        const Dictionary &current,
        const Dictionary &rules,
        const Dictionary &angles
    );

    static Ref<NetwPredictRecovery> recover(
        const Dictionary &payload,
        int policy,
        int correction,
        int snap_restore,
        const Dictionary &projection,
        const Dictionary &current,
        const Dictionary &pose_errors,
        const Dictionary &wiring,
        const Dictionary &verdict,
        double tick_delta
    );

    static Dictionary escalation_after(
        int streak,
        int last_sign,
        double last_divergence,
        double divergence,
        int sign
    );

    static int measure(
        const Dictionary &field_sink,
        const Dictionary &tolerances
    );

    static int attribute(
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

    static int64_t raw_state_fingerprint(const Dictionary &payload);

    static int64_t fact_fingerprint(const Dictionary &facts);

    static int64_t topology_fingerprint(const Dictionary &facts, int quantum);

    static int contact_count_bucket(int count);

    static int differing_family(
        const PackedInt32Array &local,
        const PackedInt32Array &peer
    );

    static int64_t window_after(
        int64_t label,
        int64_t cooldown,
        int64_t window_until
    );

    static int64_t environment_digest(int64_t epoch, const Dictionary &samples);

    static Dictionary delta_direction(
        const StringName &field,
        const Variant &delta
    );

    static Dictionary guard_projection(
        const Dictionary &projection,
        const Dictionary &field_divergence,
        double epsilon,
        const Dictionary &epsilon_overrides,
        int max_restore_ticks,
        int ack_age_ticks,
        double tick_delta
    );

    static Dictionary transport(
        const Dictionary &predicted,
        const Dictionary &authority,
        const Dictionary &current,
        const Dictionary &pose_fields,
        const Dictionary &angles
    );

    static Variant pose_delta(
        const Variant &target,
        const Variant &current,
        bool is_angle
    );

    static bool teleport_reached(
        const Dictionary &pose_errors,
        const Dictionary &thresholds,
        double default_threshold
    );

    static PredictionVerdict evaluate_struct_verdict(
        int domain,
        int verdict,
        const Dictionary &predicted,
        const Dictionary &payload,
        const Dictionary &wiring
    );

    static Dictionary calculate_joint_floor(
        const Dictionary &bases,
        const Dictionary &relay_floors,
        int64_t epoch_floor,
        int64_t history_floor,
        int64_t present
    );

    static int calculate_joint_cell(
        bool authored,
        bool relayed,
        bool predictor_valid
    );

    static int admit_frame(
        int channel,
        int sender,
        int controller,
        bool receiver_is_server,
        bool payload_empty,
        int route_verdict
    );
};

} // namespace netw

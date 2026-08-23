#include "netw/prediction_core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/frames.hpp"
#include "netw/predict/sensors.hpp"
#include "netw/profile.hpp"
#include "netw/api/project.hpp"

using namespace godot;

namespace netw {

// How long a run of non-shrinking divergence escalates on.
constexpr int ESCALATE_NONSHRINK = 3;

// The families differing_family walks, in the causal order it charges them.
constexpr int FAMILY_COUNT = 3;

// An infinite or missing field error measures further than any finite meter
// rather than overflowing the int the meter crosses the seam as.
constexpr int MEASURE_SATURATED = 0x7FFFFFFF;

constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

constexpr double TAU = 6.2831853071795864769252867666;

static double angle_difference(double p_from, double p_to) {
    double diff = std::fmod(p_to - p_from, TAU);
    return std::fmod(2.0 * diff, TAU) - diff;
}

static Variant pose_delta(
    const Variant &target,
    const Variant &current,
    bool is_angle
) {
    switch (target.get_type()) {
        case Variant::FLOAT:
            return is_angle ? angle_difference(double(current), double(target))
                            : double(target) - double(current);
        case Variant::VECTOR2:
            return Vector2(target) - Vector2(current);
        case Variant::VECTOR3:
            return Vector3(target) - Vector3(current);
        default:
            return Variant();
    }
}

static Variant pose_scale(const Variant &value, double factor) {
    switch (value.get_type()) {
        case Variant::FLOAT:
            return double(value) * factor;
        case Variant::VECTOR2:
            return Vector2(value) * real_t(factor);
        case Variant::VECTOR3:
            return Vector3(value) * real_t(factor);
        default:
            return value;
    }
}

static Variant pose_sum(const Variant &a, const Variant &b) {
    switch (a.get_type()) {
        case Variant::FLOAT:
            return double(a) + double(b);
        case Variant::VECTOR2:
            return Vector2(a) + Vector2(b);
        case Variant::VECTOR3:
            return Vector3(a) + Vector3(b);
        default:
            return a;
    }
}

static bool teleport_reached(
    const Dictionary &pose_errors,
    const Dictionary &thresholds,
    double default_threshold
) {
    const Array keys = pose_errors.keys();
    for (int index = 0; index < keys.size(); ++index) {
        const Variant key = keys[index];
        if (double(pose_errors[key])
            >= double(thresholds.get(key, default_threshold))) {
            return true;
        }
    }
    return false;
}

static double divergence_by_field(
    const Dictionary &predicted,
    const Dictionary &payload,
    const Dictionary &angles,
    Dictionary &field_sink
) {
    field_sink.clear();
    double worst = 0.0;
    const Array keys = payload.keys();
    for (int index = 0; index < keys.size(); ++index) {
        const Variant key = keys[index];
        const double error = predicted.has(key)
            ? predict::value_error(
                  predicted[key],
                  payload[key],
                  angles.has(key)
              )
            : std::numeric_limits<double>::infinity();
        field_sink[key] = error;
        worst = std::max(worst, error);
    }
    return worst;
}

static bool diverged(
    const Dictionary &predicted,
    const Dictionary &payload,
    double epsilon,
    const Dictionary &overrides,
    const Dictionary &excludes,
    const Dictionary &angles
) {
    const Array keys = payload.keys();
    for (int index = 0; index < keys.size(); ++index) {
        const Variant key = keys[index];
        if (excludes.has(key)) {
            continue;
        }
        if (!predicted.has(key)) {
            return true;
        }
        const double tolerance = overrides.get(key, epsilon);
        if (predict::value_error(predicted[key], payload[key], angles.has(key))
            > tolerance) {
            return true;
        }
    }
    return false;
}

static int64_t fnv1a(const PackedByteArray &bytes) {
    uint32_t hash = FNV_OFFSET;
    for (int i = 0; i < bytes.size(); i++) {
        hash = (hash ^ uint32_t(uint8_t(bytes[i]))) * FNV_PRIME;
    }
    const int64_t folded = int64_t(hash);
    return hash >= 0x80000000u ? folded - 0x100000000LL : folded;
}

// Keys fold in text order, which is the only order two peers can agree on.
// Sorting the StringName keys themselves compares interning pointers, so the
// order would depend on which names each process interned first and two peers
// holding identical facts could digest them differently.
static void append_sorted(const Dictionary &source, PackedByteArray &bytes) {
    PackedStringArray names;
    const Array keys = source.keys();
    for (int i = 0; i < keys.size(); i++) {
        names.push_back(String(keys[i]));
    }
    names.sort();

    for (int i = 0; i < names.size(); i++) {
        const StringName key = StringName(names[i]);
        bytes.append_array(gd::var_to_bytes(key));
        bytes.append_array(gd::var_to_bytes(source[key]));
    }
}

static int64_t fingerprint_of(const Dictionary &source) {
    NETW_ZONE_NC("NetwPredict fingerprint", colors::PREDICTION);
    PackedByteArray bytes;
    append_sorted(source, bytes);
    return fnv1a(bytes);
}

void NetwPredictionCore::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "evaluate",
            "domain",
            "verdict",
            "predicted",
            "payload",
            "wiring",
            "field_sink"
        ),
        &NetwPredictionCore::evaluate
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "domain_of",
            "declared",
            "approximate",
            "label",
            "window_until"
        ),
        &NetwPredictionCore::domain_of
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "predict_fold",
            "latest_input_tick",
            "last_driven_input_tick",
            "frame_tick"
        ),
        &NetwPredictionCore::predict_fold
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("consume_action", "depth", "buffer"),
        &NetwPredictionCore::consume_action
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("compared_state", "payload", "causal"),
        &NetwPredictionCore::compared_state
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("pose_delta", "target", "current", "is_angle"),
        &NetwPredictionCore::pose_delta
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "teleport_reached",
            "pose_errors",
            "thresholds",
            "default_threshold"
        ),
        &NetwPredictionCore::teleport_reached
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("project_payload", "payload", "projection", "age"),
        &NetwPredictionCore::project_payload
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("converge_toward", "restore", "current", "rules", "angles"),
        &NetwPredictionCore::converge_toward
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "recover",
            "payload",
            "policy",
            "correction",
            "snap_restore",
            "projection",
            "current",
            "pose_errors",
            "wiring",
            "verdict",
            "tick_delta"
        ),
        &NetwPredictionCore::recover
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "escalation_after",
            "streak",
            "last_sign",
            "last_divergence",
            "divergence",
            "sign"
        ),
        &NetwPredictionCore::escalation_after
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("measure", "field_sink", "tolerances"),
        &NetwPredictionCore::measure
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "attribute",
            "pre_equal",
            "command_equal",
            "environment_equal",
            "topology_equal",
            "raw_equal",
            "witness_equal",
            "local_evidence",
            "peer_evidence",
            "evidence_complete"
        ),
        &NetwPredictionCore::attribute
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("raw_state_fingerprint", "payload"),
        &NetwPredictionCore::raw_state_fingerprint
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("topology_fingerprint", "facts", "quantum"),
        &NetwPredictionCore::topology_fingerprint
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("fact_fingerprint", "facts"),
        &NetwPredictionCore::fact_fingerprint
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "admit_frame",
            "channel",
            "sender",
            "controller",
            "receiver_is_server",
            "payload_empty",
            "route_verdict"
        ),
        &NetwPredictionCore::admit_frame
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("contact_count_bucket", "count"),
        &NetwPredictionCore::contact_count_bucket
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("differing_family", "local", "peer"),
        &NetwPredictionCore::differing_family
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("window_after", "label", "cooldown", "window_until"),
        &NetwPredictionCore::window_after
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("environment_digest", "epoch", "samples"),
        &NetwPredictionCore::environment_digest
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD("delta_direction", "field", "delta"),
        &NetwPredictionCore::delta_direction
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "guard_projection",
            "projection",
            "field_divergence",
            "epsilon",
            "epsilon_overrides",
            "max_restore_ticks",
            "ack_age_ticks",
            "tick_delta"
        ),
        &NetwPredictionCore::guard_projection
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "transport",
            "predicted",
            "authority",
            "current",
            "pose_fields",
            "angles"
        ),
        &NetwPredictionCore::transport
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "calculate_joint_floor",
            "bases",
            "relay_floors",
            "epoch_floor",
            "history_floor",
            "present"
        ),
        &NetwPredictionCore::calculate_joint_floor
    );
    ClassDB::bind_static_method(
        "NetwPredictionCore",
        D_METHOD(
            "calculate_joint_cell",
            "authored",
            "relayed",
            "predictor_valid"
        ),
        &NetwPredictionCore::calculate_joint_cell
    );
}

Ref<NetwPredictJudgement> NetwPredictionCore::evaluate(
    int domain,
    int verdict,
    const Dictionary &predicted,
    const Dictionary &payload,
    const Dictionary &wiring,
    Dictionary field_sink
) {
    NETW_ZONE_NC("NetwPredict evaluate", colors::PREDICTION);
    const bool in_domain = domain == int(Domain::IN_DOMAIN);
    field_sink.clear();
    if (predicted.is_empty()) {
        return NetwPredictJudgement::of(
            std::numeric_limits<double>::infinity(),
            true
        );
    }
    const Dictionary angles
        = wiring.get(StringName("angle_fields"), Dictionary());
    const double divergence
        = divergence_by_field(predicted, payload, angles, field_sink);
    const bool exact = in_domain && verdict != int(ExactVerdict::UNJUDGED);
    const bool corrected = exact
        ? verdict == int(ExactVerdict::UNEQUAL)
        : diverged(
              predicted,
              payload,
              wiring.get(StringName("epsilon"), 0.0),
              wiring.get(StringName("epsilon_overrides"), Dictionary()),
              wiring.get(StringName("vote_excludes"), Dictionary()),
              angles
          );
    return NetwPredictJudgement::of(divergence, corrected);
}

Ref<NetwPredictJudgement> NetwPredictJudgement::of(
    double p_divergence,
    bool p_corrected
) {
    Ref<NetwPredictJudgement> out;
    out.instantiate();
    out->judged.divergence = p_divergence;
    out->judged.corrected = p_corrected;
    return out;
}

void NetwPredictJudgement::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictJudgement",
        D_METHOD("of", "divergence", "corrected"),
        &NetwPredictJudgement::of
    );
    ClassDB::bind_method(
        D_METHOD("divergence"),
        &NetwPredictJudgement::divergence
    );
    ClassDB::bind_method(
        D_METHOD("corrected"),
        &NetwPredictJudgement::corrected
    );
}

Fold NetwPredictionCore::fold(
    int64_t latest_input_tick,
    int64_t last_driven_input_tick,
    int64_t frame_tick
) {
    Fold out;
    out.fresh = latest_input_tick > last_driven_input_tick;
    out.label = latest_input_tick >= 0 ? latest_input_tick : frame_tick;
    out.kind = out.fresh ? DriveKind::FRESH : DriveKind::REPEAT;
    return out;
}

Ref<NetwPredictFold> NetwPredictionCore::predict_fold(
    int64_t latest_input_tick,
    int64_t last_driven_input_tick,
    int64_t frame_tick
) {
    const Fold decided
        = fold(latest_input_tick, last_driven_input_tick, frame_tick);
    return NetwPredictFold::of(decided.label, decided.fresh, int(decided.kind));
}

Ref<NetwPredictFold> NetwPredictFold::of(
    int64_t p_label,
    bool p_fresh,
    int p_kind
) {
    NETW_ERR_COND_V(
        p_kind < int(DriveKind::NONE) || p_kind > int(DriveKind::SUBSTITUTED),
        Ref<NetwPredictFold>(),
        "predict",
        "NetwPredictFold.of: kind %d names no drive kind.",
        p_kind
    );
    Ref<NetwPredictFold> out;
    out.instantiate();
    out->decided.label = p_label;
    out->decided.fresh = p_fresh;
    out->decided.kind = DriveKind(p_kind);
    return out;
}

void NetwPredictFold::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictFold",
        D_METHOD("of", "label", "fresh", "kind"),
        &NetwPredictFold::of
    );
    ClassDB::bind_method(D_METHOD("label"), &NetwPredictFold::label);
    ClassDB::bind_method(D_METHOD("fresh"), &NetwPredictFold::fresh);
    ClassDB::bind_method(D_METHOD("kind"), &NetwPredictFold::kind);
}

int NetwPredictionCore::consume_action(int depth, int buffer) {
    if (depth > buffer) {
        return int(ConsumeAction::REPLAY);
    }
    return int(depth > 0 ? ConsumeAction::HOLD : ConsumeAction::STARVED);
}

Variant NetwPredictionCore::pose_delta(
    const Variant &target,
    const Variant &current,
    bool is_angle
) {
    return netw::pose_delta(target, current, is_angle);
}

bool NetwPredictionCore::teleport_reached(
    const Dictionary &pose_errors,
    const Dictionary &thresholds,
    double default_threshold
) {
    return netw::teleport_reached(pose_errors, thresholds, default_threshold);
}

Dictionary NetwPredictionCore::compared_state(
    const Dictionary &payload,
    const Dictionary &causal
) {
    if (causal.is_empty()) {
        return payload;
    }
    Dictionary out;
    const Array fields = payload.keys();
    for (int index = 0; index < fields.size(); ++index) {
        const Variant field = fields[index];
        if (causal.has(field)) {
            out[field] = payload[field];
        }
    }
    return out;
}

Dictionary NetwPredictionCore::project_payload(
    const Dictionary &payload,
    const Dictionary &projection,
    double age
) {
    if (age <= 0.0) {
        return payload;
    }
    Dictionary out = payload.duplicate();
    const Array fields = projection.keys();
    for (int index = 0; index < fields.size(); ++index) {
        const Variant field = fields[index];
        const Variant velocity_key = projection[field];
        if (!payload.has(field) || !payload.has(velocity_key)) {
            continue;
        }
        const Variant value = payload[field];
        if (!NetwProject::supports(int(value.get_type()))) {
            continue;
        }
        out[field] = NetwProject::project(value, payload[velocity_key], age);
    }
    return out;
}

Dictionary NetwPredictionCore::converge_toward(
    const Dictionary &restore,
    const Dictionary &current,
    const Dictionary &rules,
    const Dictionary &angles
) {
    if (rules.is_empty()) {
        return restore;
    }
    Dictionary out = restore.duplicate();
    const Array fields = rules.keys();
    for (int index = 0; index < fields.size(); ++index) {
        const Variant field = fields[index];
        if (!restore.has(field) || !current.has(field)) {
            continue;
        }
        const double rate = std::clamp(double(rules[field]), 0.0, 1.0);
        // A rate at either end is not a convergence: zero holds the live value
        // and one is the restore already staged.
        if (rate <= 0.0 || rate >= 1.0) {
            continue;
        }
        const Variant delta
            = pose_delta(restore[field], current[field], angles.has(field));
        if (delta.get_type() == Variant::NIL) {
            continue;
        }
        out[field] = pose_sum(current[field], pose_scale(delta, rate));
    }
    return out;
}

Ref<NetwPredictRecovery> NetwPredictRecovery::of(
    const Dictionary &p_restore,
    const Dictionary &p_write,
    bool p_teleport,
    bool p_skip
) {
    Ref<NetwPredictRecovery> out;
    out.instantiate();
    out->restored = p_restore;
    out->written = p_write;
    out->teleported = p_teleport;
    out->skipped = p_skip;
    return out;
}

void NetwPredictRecovery::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictRecovery",
        D_METHOD("of", "restore", "write", "teleport", "skip"),
        &NetwPredictRecovery::of
    );
    ClassDB::bind_method(D_METHOD("restore"), &NetwPredictRecovery::restore);
    ClassDB::bind_method(D_METHOD("write"), &NetwPredictRecovery::write);
    ClassDB::bind_method(D_METHOD("teleport"), &NetwPredictRecovery::teleport);
    ClassDB::bind_method(D_METHOD("skip"), &NetwPredictRecovery::skip);
}

Ref<NetwPredictRecovery> NetwPredictionCore::recover(
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
) {
    NETW_ZONE_NC("NetwPredict recover", colors::PREDICTION);
    if (policy == int(RecoveryPolicy::OBSERVE)) {
        return NetwPredictRecovery::of(Dictionary(), Dictionary(), false, true);
    }
    if (correction == int(CorrectionMode::REPLAY)) {
        return NetwPredictRecovery::of(payload, Dictionary(), false, false);
    }

    Dictionary restore = payload;
    if (snap_restore == int(RestoreMode::EXTRAPOLATED)
        && !projection.is_empty()) {
        const int64_t ack_age = verdict.get(StringName("ack_age_ticks"), 0);
        const int64_t ceiling = wiring.get(StringName("max_restore_ticks"), 0);
        const int64_t span = std::clamp(ack_age, int64_t(0), ceiling);
        restore
            = project_payload(payload, projection, double(span) * tick_delta);
    }

    if (bool(verdict.get(StringName("pose_unmeasured"), false))
        || teleport_reached(
            pose_errors,
            wiring.get(StringName("teleport_thresholds"), Dictionary()),
            wiring.get(StringName("teleport_threshold"), 0.0)
        )) {
        return NetwPredictRecovery::of(restore, restore, true, false);
    }

    // Partiality is an in-domain refinement. A divergence outside the declared
    // domain, or one nobody could charge while a contact was still disturbing
    // the bodies, gives no ground to decide which fields are safe to leave
    // predicted, so the recovery re-bases the whole closure.
    const int domain
        = verdict.get(StringName("domain"), int(Domain::IN_DOMAIN));
    const int attribution
        = verdict.get(StringName("attribution"), int(Attribution::UNKNOWN));
    if (domain == int(Domain::OUT_OF_DOMAIN)
        || (attribution == int(Attribution::UNKNOWN)
            && bool(verdict.get(StringName("contact_window"), false)))) {
        return NetwPredictRecovery::of(restore, restore, false, false);
    }

    if (bool(verdict.get(StringName("suppressed"), false))) {
        return NetwPredictRecovery::of(Dictionary(), Dictionary(), false, true);
    }

    const Dictionary withheld
        = wiring.get(StringName("withheld"), Dictionary());
    if (!withheld.is_empty()) {
        restore = restore.duplicate();
        const Array fields = withheld.keys();
        for (int index = 0; index < fields.size(); ++index) {
            restore.erase(fields[index]);
        }
    }
    restore = converge_toward(
        restore,
        current,
        wiring.get(StringName("converge_rules"), Dictionary()),
        wiring.get(StringName("angle_fields"), Dictionary())
    );
    return NetwPredictRecovery::of(restore, restore, false, false);
}

int NetwPredictionCore::domain_of(
    bool declared,
    bool approximate,
    int64_t label,
    int64_t window_until
) {
    if (!declared || approximate
        || (window_until >= 0 && label < window_until)) {
        return int(Domain::OUT_OF_DOMAIN);
    }
    return int(Domain::IN_DOMAIN);
}

Dictionary NetwPredictionCore::escalation_after(
    int streak,
    int last_sign,
    double last_divergence,
    double divergence,
    int sign
) {
    Dictionary result;
    if (last_divergence >= 0.0 && divergence < last_divergence) {
        result[StringName("streak")] = 0;
        result[StringName("sign")] = 0;
        result[StringName("escalate")] = false;
        return result;
    }

    const int grown = streak + 1;
    const bool flipped = sign != 0 && last_sign != 0 && sign != last_sign;
    const bool escalate = grown >= ESCALATE_NONSHRINK || flipped;

    result[StringName("streak")] = escalate ? 0 : grown;
    result[StringName("sign")] = sign;
    result[StringName("escalate")] = escalate;
    return result;
}

int NetwPredictionCore::measure(
    const Dictionary &field_sink,
    const Dictionary &tolerances
) {
    NETW_ZONE_NC("NetwPredict measure", colors::PREDICTION);
    int meter = 0;
    const Array fields = tolerances.keys();
    for (int i = 0; i < fields.size(); i++) {
        const Variant field = fields[i];
        const double error = field_sink.has(field)
            ? double(field_sink[field])
            : std::numeric_limits<double>::infinity();
        if (!std::isfinite(error)) {
            return MEASURE_SATURATED;
        }
        const double tolerance = std::max(0.0, double(tolerances[field]));
        if (tolerance <= 0.0) {
            meter = std::max(meter, error > 0.0 ? 1 : 0);
            continue;
        }
        const double over = std::max(error - tolerance, 0.0);
        meter = std::max(meter, int(std::ceil(over / tolerance)));
    }
    return meter;
}

int NetwPredictionCore::attribute(
    bool pre_equal,
    bool command_equal,
    bool environment_equal,
    bool topology_equal,
    bool raw_equal,
    bool witness_equal,
    int local_evidence,
    int peer_evidence,
    bool evidence_complete
) {
    NETW_ZONE_NC("NetwPredict attribute", colors::PREDICTION);
    return int(predict::attribute(
        pre_equal,
        command_equal,
        environment_equal,
        topology_equal,
        raw_equal,
        witness_equal,
        local_evidence,
        peer_evidence,
        evidence_complete
    ));
}

int64_t NetwPredictionCore::raw_state_fingerprint(const Dictionary &payload) {
    return fingerprint_of(payload);
}

int64_t NetwPredictionCore::topology_fingerprint(
    const Dictionary &facts,
    int quantum
) {
    return int64_t(predict::topology_fingerprint(facts, quantum));
}

int64_t NetwPredictionCore::fact_fingerprint(const Dictionary &facts) {
    return fingerprint_of(facts);
}

int NetwPredictionCore::contact_count_bucket(int count) {
    return std::clamp(count, 0, 4);
}

int NetwPredictionCore::differing_family(
    const PackedInt32Array &local,
    const PackedInt32Array &peer
) {
    if (local.size() < FAMILY_COUNT || peer.size() < FAMILY_COUNT) {
        return int(StateFamily::NONE);
    }
    for (int i = 0; i < FAMILY_COUNT; i++) {
        if (local[i] != peer[i]) {
            return i + 1;
        }
    }
    return int(StateFamily::NONE);
}

int64_t NetwPredictionCore::window_after(
    int64_t label,
    int64_t cooldown,
    int64_t window_until
) {
    return std::max(window_until, label + std::max<int64_t>(0, cooldown) + 1);
}

int64_t NetwPredictionCore::environment_digest(
    int64_t epoch,
    const Dictionary &samples
) {
    NETW_ZONE_NC("NetwPredict environment_digest", colors::PREDICTION);
    PackedByteArray bytes = gd::var_to_bytes(epoch);
    append_sorted(samples, bytes);
    return fnv1a(bytes);
}

Dictionary NetwPredictionCore::delta_direction(
    const StringName &field,
    const Variant &delta
) {
    int axis = -1;
    double component = 0.0;
    switch (delta.get_type()) {
        case Variant::FLOAT:
            axis = 0;
            component = double(delta);
            break;
        case Variant::VECTOR2: {
            const Vector2 v2 = delta;
            axis = std::abs(v2.x) >= std::abs(v2.y) ? 0 : 1;
            component = v2[axis];
        } break;
        case Variant::VECTOR3: {
            const Vector3 v3 = delta;
            axis = 0;
            if (std::abs(v3.y) > std::abs(v3[axis])) {
                axis = 1;
            }
            if (std::abs(v3.z) > std::abs(v3[axis])) {
                axis = 2;
            }
            component = v3[axis];
        } break;
        default:
            break;
    }

    Dictionary result;
    if (axis < 0) {
        result[StringName("key")] = StringName();
        result[StringName("sign")] = 0;
        return result;
    }
    result[StringName("key")]
        = StringName(String(field) + ":" + String::num_int64(axis));
    result[StringName("sign")]
        = component > 0.0 ? 1 : (component < 0.0 ? -1 : 0);
    return result;
}

Dictionary NetwPredictionCore::guard_projection(
    const Dictionary &projection,
    const Dictionary &field_divergence,
    double epsilon,
    const Dictionary &epsilon_overrides,
    int max_restore_ticks,
    int ack_age_ticks,
    double tick_delta
) {
    NETW_ZONE_NC("NetwPredict guard_projection", colors::PREDICTION);
    if (projection.is_empty() || field_divergence.is_empty()) {
        return projection;
    }
    const double span
        = double(std::clamp(ack_age_ticks, 0, max_restore_ticks)) * tick_delta;

    Dictionary out;
    const Array fields = projection.keys();
    for (int i = 0; i < fields.size(); i++) {
        const Variant field = fields[i];
        const Variant channel = projection[field];
        const double limit = double(epsilon_overrides.get(field, epsilon));
        const double projected_error
            = double(field_divergence.get(channel, 0.0)) * span;
        if (projected_error < limit) {
            out[field] = channel;
        }
    }
    return out;
}

Dictionary NetwPredictionCore::transport(
    const Dictionary &predicted,
    const Dictionary &authority,
    const Dictionary &current,
    const Dictionary &pose_fields,
    const Dictionary &angles
) {
    NETW_ZONE_NC("NetwPredict transport", colors::PREDICTION);
    Dictionary restore;
    Dictionary deltas;
    Dictionary invalid;
    invalid[StringName("restore")] = Dictionary();
    invalid[StringName("delta")] = Dictionary();
    invalid[StringName("valid")] = false;

    const Array fields = pose_fields.keys();
    for (int i = 0; i < fields.size(); i++) {
        const Variant field = fields[i];
        if (!predicted.has(field) || !authority.has(field)
            || !current.has(field)) {
            return invalid;
        }
        const Variant delta
            = pose_delta(authority[field], predicted[field], angles.has(field));
        if (delta.get_type() == Variant::NIL) {
            return invalid;
        }
        deltas[field] = delta;
        restore[field] = pose_sum(current[field], delta);
    }

    Dictionary result;
    result[StringName("restore")] = restore;
    result[StringName("delta")] = deltas;
    result[StringName("valid")] = !restore.is_empty();
    return result;
}

PredictionVerdict NetwPredictionCore::evaluate_struct_verdict(
    int domain,
    int verdict,
    const Dictionary &predicted,
    const Dictionary &payload,
    const Dictionary &wiring
) {
    Dictionary sink;
    const Ref<NetwPredictJudgement> judged
        = evaluate(domain, verdict, predicted, payload, wiring, sink);
    PredictionVerdict pv;
    pv.domain = domain;
    pv.verdict = verdict;
    pv.corrected = judged->corrected();
    pv.max_error = judged->divergence();
    return pv;
}

Dictionary NetwPredictionCore::calculate_joint_floor(
    const Dictionary &bases,
    const Dictionary &relay_floors,
    int64_t epoch_floor,
    int64_t history_floor,
    int64_t present
) {
    int64_t floor_val = present;
    Array base_keys = bases.keys();
    for (int i = 0; i < base_keys.size(); ++i) {
        int64_t b = bases[base_keys[i]];
        if (b >= 0 && b < floor_val) {
            floor_val = b;
        }
    }
    Array relay_keys = relay_floors.keys();
    for (int i = 0; i < relay_keys.size(); ++i) {
        int64_t rf = relay_floors[relay_keys[i]];
        if (rf >= 0 && rf < floor_val) {
            floor_val = rf;
        }
    }
    if (epoch_floor >= 0 && epoch_floor < floor_val) {
        floor_val = epoch_floor;
    }
    bool heal = false;
    if (floor_val < history_floor) {
        heal = true;
        floor_val = present;
    }
    Dictionary res;
    res["floor"] = floor_val;
    res["heal"] = heal;
    return res;
}

int NetwPredictionCore::calculate_joint_cell(
    bool authored,
    bool relayed,
    bool predictor_valid
) {
    if (authored) {
        return 3;
    }
    if (relayed) {
        return 2;
    }
    if (predictor_valid) {
        return 1;
    }
    return 0;
}

int NetwPredictionCore::admit_frame(
    int channel,
    int sender,
    int controller,
    bool receiver_is_server,
    bool payload_empty,
    int route_verdict
) {
    static const wire::WireRegistry registry
        = wire::WireRegistry::create_default();
    predict::FrameOrigin origin;
    origin.sender = sender;
    origin.controller = controller;
    origin.receiver_is_server = receiver_is_server;
    return int(predict::admit_frame(
        registry,
        uint8_t(channel),
        origin,
        payload_empty,
        Error(route_verdict)
    ));
}

} // namespace netw

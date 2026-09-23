#include "netw/prediction_core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/log.hpp"
#include "netw/predict/axes.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/frames.hpp"
#include "netw/predict/recovery.hpp"
#include "netw/predict/sensors.hpp"
#include "netw/profile.hpp"
#include "netw/project.hpp"

using namespace godot;

namespace netw {

constexpr int CAUSAL_FAMILY_COUNT = 3;

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

static predict::Wiring recovery_wiring(
    const Dictionary &payload,
    const Dictionary &current,
    const Dictionary &pose_errors,
    const Dictionary &projection,
    const Dictionary &wiring
) {
    predict::Wiring out;
    const Dictionary *const sources[] = {&payload, &current, &pose_errors};
    for (const Dictionary *source : sources) {
        const Array keys = source->keys();
        for (int index = 0; index < keys.size(); ++index) {
            out.fields.append(StringName(keys[index]));
        }
    }
    const Array projected = projection.keys();
    for (int index = 0; index < projected.size(); ++index) {
        out.fields.append(StringName(projected[index]));
        out.fields.append(StringName(projection[projected[index]]));
    }
    out.resize(out.fields.count());

    const Dictionary withheld
        = wiring.get(StringName("withheld"), Dictionary());
    const Dictionary converge
        = wiring.get(StringName("converge_rules"), Dictionary());
    const Dictionary angles
        = wiring.get(StringName("angle_fields"), Dictionary());
    const Dictionary epsilons
        = wiring.get(StringName("epsilon_overrides"), Dictionary());
    const Dictionary teleports
        = wiring.get(StringName("teleport_thresholds"), Dictionary());
    for (int at = 0; at < out.count(); ++at) {
        const StringName &key = out.fields.name_at(at);
        const uint32_t slot = uint32_t(at);
        out.causal[slot] = 1;
        out.withheld[slot] = withheld.has(key) ? 1 : 0;
        out.angle[slot] = angles.has(key) ? 1 : 0;
        out.pose[slot] = pose_errors.has(key) ? 1 : 0;
        out.converge_rate[slot] = double(converge.get(key, -1.0));
        out.epsilon[slot] = double(epsilons.get(key, -1.0));
        out.teleport[slot] = double(teleports.get(key, -1.0));
    }
    for (int index = 0; index < projected.size(); ++index) {
        const int field = out.fields.index_of(StringName(projected[index]));
        out.projection[uint32_t(field)]
            = out.fields.index_of(StringName(projection[projected[index]]));
    }
    return out;
}

static predict::StateRow recovery_row(
    const predict::Wiring &wiring,
    const Dictionary &values
) {
    predict::StateRow out;
    out.resize(wiring.count());
    for (int at = 0; at < wiring.count(); ++at) {
        const StringName &key = wiring.fields.name_at(at);
        if (values.has(key)) {
            out.set(at, values[key]);
        }
    }
    return out;
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

static void append_in_key_text_order(
    const Dictionary &source,
    PackedByteArray &bytes
) {
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
    append_in_key_text_order(source, bytes);
    return fnv1a(bytes);
}

#if defined(NETW_TESTS)
namespace {
int64_t minted_records = 0;
} // namespace

int64_t prediction_core::records_minted() {
    return minted_records;
}

void prediction_core::note_record_mint() {
    ++minted_records;
}
#endif

Judgement prediction_core::judge(
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
        Judgement out;
        out.divergence = std::numeric_limits<double>::infinity();
        out.corrected = true;
        return out;
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
    Judgement out;
    out.divergence = divergence;
    out.corrected = corrected;
    return out;
}

Ref<NetwPredictJudgement> prediction_core::evaluate(
    int domain,
    int verdict,
    const Dictionary &predicted,
    const Dictionary &payload,
    const Dictionary &wiring,
    Dictionary field_sink
) {
    const Judgement judged
        = judge(domain, verdict, predicted, payload, wiring, field_sink);
    return NetwPredictJudgement::of(judged.divergence, judged.corrected);
}

Ref<NetwPredictJudgement> NetwPredictJudgement::of(
    double p_divergence,
    bool p_corrected
) {
    NETW_NOTE_RECORD_MINT();
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

Fold prediction_core::fold(
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

Ref<NetwPredictFold> prediction_core::predict_fold(
    int64_t latest_input_tick,
    int64_t last_driven_input_tick,
    int64_t frame_tick
) {
    const Fold decided
        = fold(latest_input_tick, last_driven_input_tick, frame_tick);
    return NetwPredictFold::of(
        decided.label,
        decided.fresh,
        static_cast<NetwPredict::DriveKind>(int(decided.kind))
    );
}

Ref<NetwPredictFold> NetwPredictFold::of(
    int64_t p_label,
    bool p_fresh,
    NetwPredict::DriveKind p_kind
) {
    const int kind = int(p_kind);
    NETW_ERR_COND_V(
        kind < int(DriveKind::NONE) || kind > int(DriveKind::SUBSTITUTED),
        Ref<NetwPredictFold>(),
        sys::PREDICTION,
        "NetwPredictFold.of: kind %d names no drive kind.",
        kind
    );
    NETW_NOTE_RECORD_MINT();
    Ref<NetwPredictFold> out;
    out.instantiate();
    out->decided.label = p_label;
    out->decided.fresh = p_fresh;
    out->decided.kind = DriveKind(kind);
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

int prediction_core::consume_action(int depth, int buffer) {
    if (depth > buffer) {
        return int(ConsumeAction::REPLAY);
    }
    return int(depth > 0 ? ConsumeAction::HOLD : ConsumeAction::STARVED);
}

Variant prediction_core::pose_delta(
    const Variant &target,
    const Variant &current,
    bool is_angle
) {
    return netw::pose_delta(target, current, is_angle);
}

Variant prediction_core::pose_advance(
    const Variant &current,
    const Variant &delta
) {
    switch (current.get_type()) {
        case Variant::FLOAT:
            return double(current) + double(delta);
        case Variant::VECTOR2:
            return Vector2(current) + Vector2(delta);
        case Variant::VECTOR3:
            return Vector3(current) + Vector3(delta);
        default:
            return current;
    }
}

Dictionary prediction_core::compared_state(
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

Dictionary prediction_core::project_payload(
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
        if (!project::supports(int(value.get_type()))) {
            continue;
        }
        out[field] = project::forward(value, payload[velocity_key], age);
    }
    return out;
}

Ref<NetwPredictRecovery> NetwPredictRecovery::of(
    const Dictionary &p_restore,
    const Dictionary &p_write,
    bool p_teleport,
    bool p_skip
) {
    NETW_NOTE_RECORD_MINT();
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

Ref<NetwPredictRecovery> prediction_core::recover(
    const Dictionary &payload,
    int policy,
    int snap_restore,
    const Dictionary &projection,
    const Dictionary &current,
    const Dictionary &pose_errors,
    const Dictionary &wiring,
    const Dictionary &verdict,
    double tick_delta
) {
    NETW_ZONE_NC("NetwPredict recover", colors::PREDICTION);
    const predict::Wiring fields
        = recovery_wiring(payload, current, pose_errors, projection, wiring);

    predict::Config config;
    config.correction = predict::correction_for_recovery_policy(policy);
    config.restore = snap_restore;

    predict::RecoveryRequest request;
    request.authority = recovery_row(fields, payload);
    request.current = recovery_row(fields, current);
    request.predicted.resize(fields.count());
    request.field_errors.resize(uint32_t(fields.count()));
    for (int at = 0; at < fields.count(); ++at) {
        request.field_errors[uint32_t(at)]
            = double(pose_errors.get(fields.fields.name_at(at), -1.0));
    }
    request.policy = policy;
    request.fallback_epsilon = wiring.get(StringName("epsilon"), 0.0);
    request.fallback_teleport
        = wiring.get(StringName("teleport_threshold"), 0.0);
    request.max_restore_ticks = wiring.get(StringName("max_restore_ticks"), 0);
    request.ack_age_ticks = verdict.get(StringName("ack_age_ticks"), 0);
    request.tick_delta = tick_delta;
    request.domain = predict::Domain(
        int(verdict.get(StringName("domain"), int(Domain::IN_DOMAIN)))
    );
    request.attribution = predict::Attribution(
        int(verdict.get(StringName("attribution"), int(Attribution::UNKNOWN)))
    );
    request.contact_window = verdict.get(StringName("contact_window"), false);
    request.suppressed = verdict.get(StringName("suppressed"), false);
    request.pose_unmeasured = verdict.get(StringName("pose_unmeasured"), false);

    predict::RecoveryState fresh;
    const predict::WritePlan plan
        = predict::stage_recovery(fields, config, request, fresh);
    Dictionary restore;
    Dictionary write;
    for (int at = 0; at < fields.count(); ++at) {
        const StringName &key = fields.fields.name_at(at);
        if (plan.restore.has(at)) {
            restore[key] = plan.restore.values[uint32_t(at)];
        }
        if (plan.write.has(at)) {
            write[key] = plan.write.values[uint32_t(at)];
        }
    }
    return NetwPredictRecovery::of(restore, write, plan.teleport, plan.skip);
}

int prediction_core::domain_of(
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

int prediction_core::measure(
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

int prediction_core::attribute(
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

int64_t prediction_core::raw_state_fingerprint(const Dictionary &payload) {
    return fingerprint_of(payload);
}

int64_t prediction_core::topology_fingerprint(
    const Dictionary &facts,
    int quantum
) {
    return int64_t(predict::topology_fingerprint(facts, quantum));
}

int64_t prediction_core::fact_fingerprint(const Dictionary &facts) {
    return fingerprint_of(facts);
}

int prediction_core::contact_count_bucket(int count) {
    return std::clamp(count, 0, 4);
}

int prediction_core::differing_family(
    const PackedInt32Array &local,
    const PackedInt32Array &peer
) {
    if (local.size() < CAUSAL_FAMILY_COUNT
        || peer.size() < CAUSAL_FAMILY_COUNT) {
        return int(StateFamily::NONE);
    }
    for (int i = 0; i < CAUSAL_FAMILY_COUNT; i++) {
        if (local[i] != peer[i]) {
            return i + 1;
        }
    }
    return int(StateFamily::NONE);
}

int64_t prediction_core::window_after(
    int64_t label,
    int64_t cooldown,
    int64_t window_until
) {
    return std::max(window_until, label + std::max<int64_t>(0, cooldown) + 1);
}

int64_t prediction_core::environment_digest(
    int64_t epoch,
    const Dictionary &samples
) {
    NETW_ZONE_NC("NetwPredict environment_digest", colors::PREDICTION);
    PackedByteArray bytes = gd::var_to_bytes(epoch);
    append_in_key_text_order(samples, bytes);
    return fnv1a(bytes);
}

Dictionary prediction_core::delta_direction(
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

Dictionary prediction_core::guard_projection(
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

PredictionVerdict prediction_core::evaluate_struct_verdict(
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

Dictionary prediction_core::calculate_joint_floor(
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

int prediction_core::calculate_joint_cell(
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

int prediction_core::admit_frame(
    int channel,
    int sender,
    int controller,
    int64_t authority,
    bool receiver_is_authority,
    bool payload_empty,
    int route_verdict
) {
    static const wire::WireRegistry registry
        = wire::WireRegistry::create_default();
    predict::FrameOrigin origin;
    origin.sender = sender;
    origin.controller = controller;
    origin.authority = authority;
    origin.receiver_is_authority = receiver_is_authority;
    return int(predict::admit_frame(
        registry,
        uint8_t(channel),
        origin,
        payload_empty,
        Error(route_verdict)
    ));
}

} // namespace netw

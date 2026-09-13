#include "netw/predict/engine.hpp"

#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_field_recovery.hpp"
#include "netw/api/predict_island.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/timeline.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/script/model.hpp"
#include "netw/wire/registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/predict/axes.hpp"
#include "netw/predict/frames.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr int ACK_WINDOW_MAX = 64;

predict::StateRow state_row(const Array &p_values, int p_width) {
    predict::StateRow out;
    out.resize(p_width);
    const int count = p_values.size() < p_width ? p_values.size() : p_width;
    for (int at = 0; at < count; ++at) {
        out.set(at, p_values[at]);
    }
    return out;
}

LocalVector<double> tolerance_row(
    const PackedFloat64Array &p_values,
    int p_width
) {
    LocalVector<double> out;
    out.resize(uint32_t(p_width));
    for (int at = 0; at < p_width; ++at) {
        out[uint32_t(at)] = at < p_values.size() ? p_values[at] : -1.0;
    }
    return out;
}

} // namespace

predict::RecoveryRequest recovery_request(
    int p_width,
    const Array &p_predicted,
    const Array &p_authority,
    const Array &p_current,
    const PackedFloat64Array &p_field_errors,
    int64_t p_basis,
    int64_t p_current_label,
    int p_policy,
    double p_fallback_epsilon,
    double p_fallback_teleport,
    int p_max_restore_ticks,
    int p_ack_age_ticks,
    int p_collision_cooldown_ticks,
    double p_tick_delta,
    int p_domain,
    int p_attribution,
    bool p_contact_window,
    bool p_suppressed,
    bool p_pose_unmeasured
) {
    predict::RecoveryRequest out;
    NETW_ERR_COND_V(
        p_policy < int(RecoveryPolicy::REBASE_REPLAY)
            || p_policy > int(RecoveryPolicy::OBSERVE),
        out,
        sys::PREDICTION,
        "Recovery policy %d is outside the declared range.",
        p_policy
    );
    NETW_ERR_COND_V(
        p_domain < int(predict::Domain::IN_DOMAIN)
            || p_domain > int(predict::Domain::OUT_OF_DOMAIN),
        out,
        sys::PREDICTION,
        "Recovery domain %d is outside the declared range.",
        p_domain
    );
    NETW_ERR_COND_V(
        p_attribution < int(predict::Attribution::UNKNOWN)
            || p_attribution > int(predict::Attribution::CLOSURE),
        out,
        sys::PREDICTION,
        "Recovery attribution %d is outside the declared range.",
        p_attribution
    );
    NETW_ERR_COND_V(
        p_tick_delta <= 0.0,
        out,
        sys::PREDICTION,
        "Recovery tick delta must be positive."
    );
    out.predicted = state_row(p_predicted, p_width);
    out.authority = state_row(p_authority, p_width);
    out.current = state_row(p_current, p_width);
    out.field_errors = tolerance_row(p_field_errors, p_width);
    out.basis = p_basis;
    out.current_label = p_current_label;
    out.policy = p_policy;
    out.fallback_epsilon = p_fallback_epsilon;
    out.fallback_teleport = p_fallback_teleport;
    out.max_restore_ticks = p_max_restore_ticks;
    out.ack_age_ticks = p_ack_age_ticks;
    out.collision_cooldown_ticks = p_collision_cooldown_ticks;
    out.tick_delta = p_tick_delta;
    out.domain = predict::Domain(p_domain);
    out.attribution = predict::Attribution(p_attribution);
    out.contact_window = p_contact_window;
    out.suppressed = p_suppressed;
    out.pose_unmeasured = p_pose_unmeasured;
    return out;
}

predict::FieldDecl field_decl(
    const StringName &p_key,
    int p_property_class,
    const StringName &p_carry_channel,
    double p_converge_stiffness,
    bool p_teleport_only,
    bool p_reconcile_only,
    double p_epsilon_override,
    double p_teleport_at,
    bool p_angle,
    const Ref<NetwQuantize> &p_quantizer,
    int p_type
) {
    predict::FieldDecl decl;
    decl.key = p_key;
    decl.property_class = p_property_class;
    decl.carry_channel = p_carry_channel;
    decl.converge_stiffness = p_converge_stiffness;
    decl.teleport_only = p_teleport_only;
    decl.reconcile_only = p_reconcile_only;
    decl.epsilon_override = p_epsilon_override;
    decl.teleport_at = p_teleport_at;
    decl.angle = p_angle;
    decl.quantizer = p_quantizer;
    decl.type = p_type;
    return decl;
}

const predict::Slot *NetwPredictionEngine::row_of(int64_t p_slot) const {
    const HashMap<int64_t, predict::Slot>::ConstIterator found
        = rows.find(p_slot);
    return found != rows.end() && found->value.open ? &found->value : nullptr;
}

predict::Slot *NetwPredictionEngine::mutable_row_of(int64_t p_slot) {
    HashMap<int64_t, predict::Slot>::Iterator found = rows.find(p_slot);
    return found != rows.end() && found->value.open ? &found->value : nullptr;
}

int64_t NetwPredictionEngine::open(
    const LocalVector<predict::FieldDecl> &p_declaration
) {
    if (!roster_open("open")) {
        return -1;
    }
    const int64_t slot = next_slot;
    next_slot += 1;
    predict::Slot row;
    row.open = true;
    row.reset_entry_history();
    if (!p_declaration.is_empty()) {
        row.rewire(predict::compile(p_declaration));
    }
    rows.insert(slot, row);
    return slot;
}

void NetwPredictionEngine::rewire(
    int64_t p_slot,
    const LocalVector<predict::FieldDecl> &p_declaration,
    const LocalVector<predict::FieldDecl> &p_input
) {
    if (!roster_open("rewire")) {
        return;
    }
    HashMap<int64_t, predict::Slot>::Iterator found = rows.find(p_slot);
    if (found == rows.end() || !found->value.open) {
        return;
    }
    found->value.rewire(predict::compile(p_declaration));
    found->value.input_codec.clear();
    for (uint32_t at = 0; at < p_input.size(); ++at) {
        found->value.input_codec
            .append(p_input[at].key, p_input[at].quantizer, p_input[at].type);
    }
    if (found->value.port.bound()) {
        resolve_reach(found->value, resolve_owner(found->value));
    }
}

void NetwPredictionEngine::resolve_reach(
    predict::Slot &p_row,
    const Object *p_owner
) {
    p_row.owner_has_state.clear();
    p_row.owner_has_input.clear();
    for (int at = 0; at < p_row.wiring.codec.count(); ++at) {
        p_row.owner_has_state.push_back(
            gd::has_property(p_owner, p_row.wiring.codec.keys[at]) ? 1 : 0
        );
    }
    for (int at = 0; at < p_row.input_codec.count(); ++at) {
        p_row.owner_has_input.push_back(
            gd::has_property(p_owner, p_row.input_codec.keys[at]) ? 1 : 0
        );
    }
}

bool NetwPredictionEngine::bind_owner(int64_t p_slot, Object *p_owner) {
    if (!roster_open("bind_owner")) {
        return false;
    }
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || p_owner == nullptr) {
        return false;
    }
    row->port.bind(p_owner);
    row->owner_solves
        = p_owner->is_class("RigidBody2D") || p_owner->is_class("RigidBody3D");
    resolve_reach(*row, p_owner);
    NETW_TRACE(sys::PREDICTION, "slot %d bound an owner", p_slot);
    return row->port.bound();
}

void NetwPredictionEngine::unbind_owner(int64_t p_slot) {
    if (!roster_open("unbind_owner")) {
        return;
    }
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->port.unbind();
        row->owner_solves = false;
        row->owner_has_state.clear();
        row->owner_has_input.clear();
    }
}

bool NetwPredictionEngine::owner_bound(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->port.bound();
}

bool NetwPredictionEngine::owner_solves(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->port.bound() && row->owner_solves;
}

Object *NetwPredictionEngine::resolve_owner(predict::Slot &p_row) {
    return p_row.port.resolve(sys::PREDICTION);
}

Dictionary NetwPredictionEngine::capture_through(
    predict::Slot &p_row,
    const predict::FieldCodec &p_codec,
    const LocalVector<uint8_t> &p_reach
) {
    NETW_ZONE_NC("predict capture owner", colors::PREDICTION);
    Dictionary out;
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return out;
    }
    for (int at = 0; at < p_codec.count(); ++at) {
        if (at < int(p_reach.size()) && p_reach[uint32_t(at)] != 0) {
            out[p_codec.keys[at]] = owner->get(p_codec.keys[at]);
        }
    }
    return out;
}

bool NetwPredictionEngine::apply_through(
    predict::Slot &p_row,
    const predict::FieldCodec &p_codec,
    const LocalVector<uint8_t> &p_reach,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict apply owner", colors::PREDICTION);
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    for (int at = 0; at < p_codec.count(); ++at) {
        const StringName &key = p_codec.keys[at];
        if (at < int(p_reach.size()) && p_reach[uint32_t(at)] != 0
            && p_payload.has(key)) {
            owner->set(key, p_payload[key]);
        }
    }
    return true;
}

namespace {

bool apply_input_via_binding(
    predict::Slot &p_row,
    const Dictionary &p_payload
) {
    if (p_row.input_binding.is_null()
        || p_row.input_binding->node() == nullptr) {
        return false;
    }
    p_row.input_binding->apply_payload(p_payload);
    return true;
}

} // namespace

Dictionary NetwPredictionEngine::capture_state(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        ? capture_through(*row, row->wiring.codec, row->owner_has_state)
        : Dictionary();
}

Dictionary NetwPredictionEngine::capture_input(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    if (row->input_binding.is_valid()
        && row->input_binding->node() != nullptr) {
        return row->input_binding->snapshot_payload();
    }
    return capture_through(*row, row->input_codec, row->owner_has_input);
}

Dictionary NetwPredictionEngine::author_input(int64_t p_slot, int64_t p_tick) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    const Dictionary input = canonicalize_input(p_slot, capture_input(p_slot));
    if (row->timeline.is_valid()) {
        row->timeline->record_input(p_tick, input);
    }
    row->cursor.latest_input_tick = p_tick;
    if (row->input_binding.is_valid()) {
        row->input_binding->set_authored_tick(p_tick);
    }
    record_input(
        p_slot,
        p_tick,
        NetwPredictJournal::fnv1a(canonical_input_bytes(p_slot, input))
    );
    return input;
}

bool NetwPredictionEngine::apply_state(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        && apply_through(
               *row,
               row->wiring.codec,
               row->owner_has_state,
               p_payload
        );
}

bool NetwPredictionEngine::apply_input(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    return apply_input_via_binding(*row, p_payload)
        || apply_through(
               *row,
               row->input_codec,
               row->owner_has_input,
               p_payload
        );
}

bool NetwPredictionEngine::roster_open(const char *p_verb) {
    if (depth == 0) {
        return true;
    }
    mutations_refused += 1;
    NETW_WARN_ONCE(
        sys::PREDICTION,
        "%s was refused during a simulation pass; a simulate step must not "
        "register or unregister prediction, and a speculative act goes "
        "through an effect instead",
        p_verb
    );
    return false;
}

int NetwPredictionEngine::pass_depth() const {
    return depth;
}

int64_t NetwPredictionEngine::mutations_refused_count() const {
    return mutations_refused;
}

void NetwPredictionEngine::set_simulate(
    int64_t p_slot,
    const Callable &p_callable
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->simulate = p_callable;
    }
}

void NetwPredictionEngine::set_witness(
    int64_t p_slot,
    const Callable &p_callable
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->witness = p_callable;
    }
}

void NetwPredictionEngine::set_corridor(
    int64_t p_slot,
    const Callable &p_callable
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->corridor = p_callable;
    }
}

void NetwPredictionEngine::set_sensor(
    int64_t p_slot,
    const StringName &p_name,
    const Callable &p_callable
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    if (p_callable.is_valid()) {
        row->sensors[p_name] = p_callable;
    } else {
        row->sensors.erase(p_name);
    }
}

bool NetwPredictionEngine::is_angle_field(
    int64_t p_slot,
    const StringName &p_key
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    const int field = row->wiring.fields.index_of(p_key);
    return field >= 0 && row->wiring.angle[uint32_t(field)] != 0;
}

bool NetwPredictionEngine::is_causal_field(
    int64_t p_slot,
    const StringName &p_key
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    const int field = row->wiring.fields.index_of(p_key);
    return field >= 0 && row->wiring.causal[uint32_t(field)] != 0;
}

bool NetwPredictionEngine::is_trigger_excluded_field(
    int64_t p_slot,
    const StringName &p_key
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    const int field = row->wiring.fields.index_of(p_key);
    return field >= 0 && row->wiring.trigger_exclude[uint32_t(field)] != 0;
}

double NetwPredictionEngine::epsilon_for_field(
    int64_t p_slot,
    const StringName &p_key,
    double p_fallback
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return p_fallback;
    }
    const int field = row->wiring.fields.index_of(p_key);
    if (field < 0 || row->wiring.epsilon[uint32_t(field)] < 0.0) {
        return p_fallback;
    }
    return row->wiring.epsilon[uint32_t(field)];
}

Dictionary NetwPredictionEngine::compared_state_of(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return p_payload;
    }
    bool any_causal = false;
    for (int at = 0; at < row->wiring.count(); ++at) {
        any_causal = any_causal || row->wiring.causal[uint32_t(at)] != 0;
    }
    if (!any_causal) {
        return p_payload;
    }
    Dictionary out;
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (row->wiring.causal[uint32_t(at)] == 0) {
            continue;
        }
        const StringName &key = row->wiring.fields.name_at(at);
        if (p_payload.has(key)) {
            out[key] = p_payload[key];
        }
    }
    return out;
}

Dictionary NetwPredictionEngine::projection_map(int64_t p_slot) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (int at = 0; at < row->wiring.count(); ++at) {
        const int channel = row->wiring.projection[uint32_t(at)];
        if (channel >= 0) {
            out[row->wiring.fields.name_at(at)]
                = row->wiring.fields.name_at(channel);
        }
    }
    return out;
}

Dictionary NetwPredictionEngine::angle_fields_of(int64_t p_slot) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    for (int at = 0; row != nullptr && at < row->wiring.count(); ++at) {
        if (row->wiring.angle[uint32_t(at)] != 0) {
            out[row->wiring.fields.name_at(at)] = true;
        }
    }
    return out;
}

Dictionary NetwPredictionEngine::wiring_snapshot(int64_t p_slot) const {
    Dictionary epsilon_overrides;
    Dictionary vote_excludes;
    Dictionary withheld;
    Dictionary converge_rules;
    Dictionary teleport_thresholds;

    const predict::Slot *row = row_of(p_slot);
    for (int at = 0; row != nullptr && at < row->wiring.count(); ++at) {
        const StringName &key = row->wiring.fields.name_at(at);
        if (row->wiring.epsilon[uint32_t(at)] >= 0.0) {
            epsilon_overrides[key] = row->wiring.epsilon[uint32_t(at)];
        }
        if (row->wiring.vote_exclude[uint32_t(at)] != 0) {
            vote_excludes[key] = true;
        }
        if (row->wiring.withheld[uint32_t(at)] != 0) {
            withheld[key] = true;
        }
        if (row->wiring.converge_rate[uint32_t(at)] > 0.0) {
            converge_rules[key] = row->wiring.converge_rate[uint32_t(at)];
        }
        if (row->wiring.teleport[uint32_t(at)] >= 0.0) {
            teleport_thresholds[key] = row->wiring.teleport[uint32_t(at)];
        }
    }

    Dictionary out;
    out[StringName("epsilon_overrides")] = epsilon_overrides;
    out[StringName("vote_excludes")] = vote_excludes;
    out[StringName("angle_fields")] = angle_fields_of(p_slot);
    out[StringName("withheld")] = withheld;
    out[StringName("converge_rules")] = converge_rules;
    out[StringName("teleport_thresholds")] = teleport_thresholds;
    out[StringName("epsilon")] = row == nullptr ? 0.0 : row->config.epsilon;
    out[StringName("teleport_threshold")]
        = row == nullptr ? 0.0 : row->config.teleport_threshold;
    out[StringName("max_restore_ticks")]
        = row == nullptr ? 0 : row->config.max_restore_ticks;
    return out;
}

Dictionary NetwPredictionEngine::teleport_distances(int64_t p_slot) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (row->wiring.teleport[uint32_t(at)] >= 0.0) {
            out[row->wiring.fields.name_at(at)]
                = row->wiring.teleport[uint32_t(at)];
        }
    }
    return out;
}

bool NetwPredictionEngine::teleport_reached_of(
    int64_t p_slot,
    const Dictionary &p_pose_errors,
    double p_fallback
) const {
    const predict::Slot *row = row_of(p_slot);
    const Array keys = p_pose_errors.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const Variant key = keys[at];
        double threshold = p_fallback;
        if (row != nullptr) {
            const int field = row->wiring.fields.index_of(StringName(key));
            if (field >= 0 && row->wiring.teleport[uint32_t(field)] >= 0.0) {
                threshold = row->wiring.teleport[uint32_t(field)];
            }
        }
        if (double(p_pose_errors[key]) >= threshold) {
            return true;
        }
    }
    return false;
}

Dictionary NetwPredictionEngine::transport_of(
    int64_t p_slot,
    const Dictionary &p_predicted,
    const Dictionary &p_authority,
    const Dictionary &p_current
) const {
    Dictionary invalid;
    invalid[StringName("restore")] = Dictionary();
    invalid[StringName("delta")] = Dictionary();
    invalid[StringName("valid")] = false;

    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return invalid;
    }

    Dictionary restore;
    Dictionary deltas;
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (row->wiring.pose[uint32_t(at)] == 0) {
            continue;
        }
        const StringName &key = row->wiring.fields.name_at(at);
        if (!p_predicted.has(key) || !p_authority.has(key)
            || !p_current.has(key)) {
            return invalid;
        }
        const Variant delta = prediction_core::pose_delta(
            p_authority[key],
            p_predicted[key],
            row->wiring.angle[uint32_t(at)] != 0
        );
        if (delta.get_type() == Variant::NIL) {
            return invalid;
        }
        deltas[key] = delta;
        restore[key] = prediction_core::pose_advance(p_current[key], delta);
    }

    Dictionary out;
    out[StringName("restore")] = restore;
    out[StringName("delta")] = deltas;
    out[StringName("valid")] = !restore.is_empty();
    return out;
}

namespace {

void fill_readings(
    predict::Slot &p_row,
    predict::FieldReadings &p_into,
    const Dictionary &p_by_field
) {
    p_into.resize(p_row.wiring.count());
    const Array keys = p_by_field.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key(keys[at]);
        p_into.note(p_row.wiring.fields.index_of(key), double(p_by_field[key]));
    }
}

Dictionary read_readings(
    const predict::Slot *p_row,
    const predict::FieldReadings &p_from
) {
    Dictionary out;
    if (p_row == nullptr) {
        return out;
    }
    for (int at = 0; at < p_row->wiring.count(); ++at) {
        if (p_from.has(at)) {
            out[p_row->wiring.fields.name_at(at)] = p_from.at(at, 0.0);
        }
    }
    return out;
}

} // namespace

Dictionary NetwPredictionEngine::frame_input_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Dictionary() : row->rows.frame_input;
}

void NetwPredictionEngine::set_frame_input(
    int64_t p_slot,
    const Dictionary &p_row
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->rows.frame_input = p_row;
    }
}

Dictionary NetwPredictionEngine::stall_input_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Dictionary() : row->rows.stall_input;
}

void NetwPredictionEngine::set_stall_input(
    int64_t p_slot,
    const Dictionary &p_row
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->rows.stall_input = p_row;
    }
}

Dictionary NetwPredictionEngine::last_input_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Dictionary() : row->rows.last_input;
}

void NetwPredictionEngine::set_last_input(
    int64_t p_slot,
    const Dictionary &p_row
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->rows.last_input = p_row;
    }
}

Dictionary NetwPredictionEngine::open_topology_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Dictionary() : row->rows.open_topology;
}

void NetwPredictionEngine::set_open_topology(
    int64_t p_slot,
    const Dictionary &p_row
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->rows.open_topology = p_row;
    }
}

namespace {

bool roster_index_valid(int p_roster) {
    return p_roster >= 0 && p_roster < 4;
}

} // namespace

TypedArray<NetwEntity> NetwPredictionEngine::roster_list(
    int64_t p_slot,
    int p_roster
) {
    TypedArray<NetwEntity> out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !roster_index_valid(p_roster)) {
        return out;
    }
    predict::EntityRoster &held = row->rosters[p_roster];
    godot::LocalVector<godot::ObjectID> live;
    for (uint32_t at = 0; at < held.members.size(); ++at) {
        NetwEntity *found
            = Object::cast_to<NetwEntity>(gd::object_of(held.members[at]));
        if (found == nullptr) {
            continue;
        }
        live.push_back(held.members[at]);
        out.push_back(Ref<NetwEntity>(found));
    }
    held.members = live;
    return out;
}

bool NetwPredictionEngine::roster_has(
    int64_t p_slot,
    int p_roster,
    const Ref<NetwEntity> &p_entity
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !roster_index_valid(p_roster) || p_entity.is_null()) {
        return false;
    }
    return row->rosters[p_roster].index_of(
               ObjectID(p_entity->get_instance_id())
           )
        >= 0;
}

bool NetwPredictionEngine::roster_add(
    int64_t p_slot,
    int p_roster,
    const Ref<NetwEntity> &p_entity
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !roster_index_valid(p_roster) || p_entity.is_null()) {
        return false;
    }
    return row->rosters[p_roster].add(ObjectID(p_entity->get_instance_id()));
}

bool NetwPredictionEngine::roster_erase(
    int64_t p_slot,
    int p_roster,
    const Ref<NetwEntity> &p_entity
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !roster_index_valid(p_roster) || p_entity.is_null()) {
        return false;
    }
    return row->rosters[p_roster].erase(ObjectID(p_entity->get_instance_id()));
}

void NetwPredictionEngine::roster_clear(int64_t p_slot, int p_roster) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr && roster_index_valid(p_roster)) {
        row->rosters[p_roster].clear();
    }
}

int NetwPredictionEngine::roster_count(int64_t p_slot, int p_roster) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !roster_index_valid(p_roster)) {
        return 0;
    }
    return row->rosters[p_roster].count();
}

void NetwPredictionEngine::roster_assign(
    int64_t p_slot,
    int p_roster,
    const TypedArray<NetwEntity> &p_members
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !roster_index_valid(p_roster)) {
        return;
    }
    predict::EntityRoster &held = row->rosters[p_roster];
    held.clear();
    for (int at = 0; at < p_members.size(); ++at) {
        const Ref<NetwEntity> member = p_members[at];
        if (member.is_valid()) {
            held.add(ObjectID(member->get_instance_id()));
        }
    }
}

bool NetwPredictionEngine::registered_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.registered;
}

void NetwPredictionEngine::set_registered(int64_t p_slot, bool p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.registered = p_value;
    }
}

bool NetwPredictionEngine::last_correction_teleported_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.last_correction_teleported;
}

void NetwPredictionEngine::set_last_correction_teleported(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.last_correction_teleported = p_value;
    }
}

int64_t NetwPredictionEngine::validated_class_hash_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->latches.validated_class_hash;
}

void NetwPredictionEngine::set_validated_class_hash(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.validated_class_hash = p_value;
    }
}

bool NetwPredictionEngine::stream_reconstructed_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.stream_reconstructed;
}

void NetwPredictionEngine::set_stream_reconstructed(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.stream_reconstructed = p_value;
    }
}

bool NetwPredictionEngine::fallback_latched_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.fallback_latched;
}

void NetwPredictionEngine::set_fallback_latched(int64_t p_slot, bool p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.fallback_latched = p_value;
    }
}

bool NetwPredictionEngine::previous_witness_sleeping_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.previous_witness_sleeping;
}

void NetwPredictionEngine::set_previous_witness_sleeping(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.previous_witness_sleeping = p_value;
    }
}

bool NetwPredictionEngine::has_previous_witness_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.has_previous_witness;
}

void NetwPredictionEngine::set_has_previous_witness(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.has_previous_witness = p_value;
    }
}

bool NetwPredictionEngine::invalid_witness_reported_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.invalid_witness_reported;
}

void NetwPredictionEngine::set_invalid_witness_reported(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.invalid_witness_reported = p_value;
    }
}

Dictionary NetwPredictionEngine::witness_sample(
    int64_t p_slot,
    const Ref<NetwPredictionHandle> &p_handle
) {
    NETW_ZONE_NC("predict witness sample", colors::PREDICTION);
    if (p_handle.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "slot %d has no handle to sample a witness through",
            p_slot
        );
        return Dictionary();
    }
    const Callable sampler = p_handle->get_witness_contacts();
    if (!sampler.is_valid()) {
        NETW_TRACE(sys::PREDICTION, "slot %d declares no witness", p_slot);
        return Dictionary();
    }
    return normalize_witness_sample(p_slot, sampler.call());
}

Dictionary NetwPredictionEngine::normalize_witness_sample(
    int64_t p_slot,
    const Variant &p_value
) {
    if (p_value.get_type() == Variant::DICTIONARY) {
        const Dictionary sample = p_value;
        const Variant colliders
            = sample.get(StringName("colliders"), Variant());
        const Variant sleeping = sample.get(StringName("sleeping"), Variant());
        if (colliders.get_type() == Variant::ARRAY
            && sleeping.get_type() == Variant::BOOL) {
            const Array rows = colliders;
            bool live = true;
            for (int64_t index = 0; index < rows.size(); ++index) {
                if (netw::gd::live_object(rows[index]) == nullptr) {
                    live = false;
                    break;
                }
            }
            const Variant continuous
                = sample.get(StringName("continuous"), Dictionary());
            if (live && continuous.get_type() == Variant::DICTIONARY) {
                return sample;
            }
        }
    }
    if (!invalid_witness_reported_of(p_slot)) {
        set_invalid_witness_reported(p_slot, true);
        NETW_ERROR(
            sys::PREDICTION,
            "Prediction witness must return {colliders: Array, sleeping: "
            "bool}; continuous, when present, must be a Dictionary."
        );
    }
    NETW_TRACE(sys::PREDICTION, "slot %d declared an unusable witness", p_slot);
    return Dictionary();
}

Dictionary NetwPredictionEngine::predicted_command(
    int64_t p_slot,
    const Ref<NetwEntity> &p_entity,
    int64_t p_tick
) {
    NETW_ZONE_NC("predict predicted command", colors::PREDICTION);
    Dictionary command;
    if (core() != nullptr) {
        command = coast_command(p_slot);
    } else {
        NETW_TRACE(
            sys::PREDICTION,
            "slot %d has no session, so it coasts on an empty baseline",
            p_slot
        );
    }
    const Callable predictor = first_simulation_predictor(p_slot);
    if (!predictor.is_valid()) {
        return canonical_input(p_slot, command);
    }
    const Variant predicted = predictor.call(p_entity, p_tick);
    if (predicted.get_type() == Variant::DICTIONARY) {
        const Dictionary overlay = predicted;
        const Array keys = overlay.keys();
        for (int64_t index = 0; index < keys.size(); ++index) {
            command[keys[index]] = overlay[keys[index]];
        }
        return canonical_input(p_slot, command);
    }
    if (!invalid_command_predictor_reported_of(p_slot)) {
        set_invalid_command_predictor_reported(p_slot, true);
        NETW_ERROR(
            sys::PREDICTION,
            "Prediction island command predictor for %s must return a "
            "Dictionary. COAST was used instead.",
            p_entity.is_valid() ? p_entity->get_entity_id() : StringName()
        );
    }
    NETW_TRACE(
        sys::PREDICTION,
        "slot %d declared an unusable command predictor",
        p_slot
    );
    return canonical_input(p_slot, command);
}

bool NetwPredictionEngine::invalid_command_predictor_reported_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false
                          : row->latches.invalid_command_predictor_reported;
}

void NetwPredictionEngine::set_invalid_command_predictor_reported(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.invalid_command_predictor_reported = p_value;
    }
}

bool NetwPredictionEngine::joint_refusal_reported_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.joint_refusal_reported;
}

void NetwPredictionEngine::set_joint_refusal_reported(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.joint_refusal_reported = p_value;
    }
}

bool NetwPredictionEngine::stepper_absence_reported_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.stepper_absence_reported;
}

void NetwPredictionEngine::set_stepper_absence_reported(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.stepper_absence_reported = p_value;
    }
}

bool NetwPredictionEngine::island_gap_reported_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.island_gap_reported;
}

void NetwPredictionEngine::set_island_gap_reported(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.island_gap_reported = p_value;
    }
}

bool NetwPredictionEngine::island_roster_seeded_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->latches.island_roster_seeded;
}

void NetwPredictionEngine::set_island_roster_seeded(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.island_roster_seeded = p_value;
    }
}

int64_t NetwPredictionEngine::joint_basis_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->latches.joint_basis;
}

void NetwPredictionEngine::set_joint_basis(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.joint_basis = p_value;
    }
}

int64_t NetwPredictionEngine::joint_relay_floor_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->latches.joint_relay_floor;
}

void NetwPredictionEngine::set_joint_relay_floor(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.joint_relay_floor = p_value;
    }
}

int64_t NetwPredictionEngine::joint_epoch_floor_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->latches.joint_epoch_floor;
}

void NetwPredictionEngine::set_joint_epoch_floor(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.joint_epoch_floor = p_value;
    }
}

int64_t NetwPredictionEngine::tenure_begin_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->latches.tenure_begin;
}

void NetwPredictionEngine::set_tenure_begin(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.tenure_begin = p_value;
    }
}

int64_t NetwPredictionEngine::tenure_end_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->latches.tenure_end;
}

void NetwPredictionEngine::set_tenure_end(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latches.tenure_end = p_value;
    }
}

int64_t NetwPredictionEngine::tape_epoch_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->lane_cursor.tape_epoch;
}

void NetwPredictionEngine::set_tape_epoch(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.tape_epoch = p_value;
    }
}

int64_t NetwPredictionEngine::next_tape_entry_index_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->lane_cursor.next_tape_entry_index;
}

void NetwPredictionEngine::set_next_tape_entry_index(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.next_tape_entry_index = p_value;
    }
}

int64_t NetwPredictionEngine::last_driven_entry_index_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.last_driven_entry_index;
}

void NetwPredictionEngine::set_last_driven_entry_index(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.last_driven_entry_index = p_value;
    }
}

int64_t NetwPredictionEngine::last_recorded_entry_index_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.last_recorded_entry_index;
}

void NetwPredictionEngine::set_last_recorded_entry_index(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.last_recorded_entry_index = p_value;
    }
}

int64_t NetwPredictionEngine::replay_cursor_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.replay_cursor;
}

void NetwPredictionEngine::set_replay_cursor(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.replay_cursor = p_value;
    }
}

int64_t NetwPredictionEngine::last_replayed_label_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.last_replayed_label;
}

void NetwPredictionEngine::set_last_replayed_label(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.last_replayed_label = p_value;
    }
}

bool NetwPredictionEngine::last_replayed_fresh_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->lane_cursor.last_replayed_fresh;
}

void NetwPredictionEngine::set_last_replayed_fresh(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.last_replayed_fresh = p_value;
    }
}

int64_t NetwPredictionEngine::next_input_tick_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.next_input_tick;
}

void NetwPredictionEngine::set_next_input_tick(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.next_input_tick = p_value;
    }
}

int64_t NetwPredictionEngine::ack_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.ack;
}

void NetwPredictionEngine::set_ack(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.ack = p_value;
    }
}

bool NetwPredictionEngine::ack_advanced_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->lane_cursor.ack_advanced;
}

void NetwPredictionEngine::set_ack_advanced(int64_t p_slot, bool p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.ack_advanced = p_value;
    }
}

int64_t NetwPredictionEngine::ack_of_acks_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.ack_of_acks;
}

void NetwPredictionEngine::set_ack_of_acks(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.ack_of_acks = p_value;
    }
}

bool NetwPredictionEngine::ack_domain_confirmed_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? true : row->lane_cursor.ack_domain_confirmed;
}

void NetwPredictionEngine::set_ack_domain_confirmed(
    int64_t p_slot,
    bool p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.ack_domain_confirmed = p_value;
    }
}

int64_t NetwPredictionEngine::owner_ack_floor_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.owner_ack_floor;
}

void NetwPredictionEngine::set_owner_ack_floor(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.owner_ack_floor = p_value;
    }
}

int64_t NetwPredictionEngine::command_epoch_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.command_epoch;
}

void NetwPredictionEngine::set_command_epoch(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.command_epoch = p_value;
    }
}

int64_t NetwPredictionEngine::relayed_epoch_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.relayed_epoch;
}

void NetwPredictionEngine::set_relayed_epoch(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.relayed_epoch = p_value;
    }
}

int64_t NetwPredictionEngine::newest_matrix_transition_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.newest_matrix_transition;
}

void NetwPredictionEngine::set_newest_matrix_transition(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.newest_matrix_transition = p_value;
    }
}

int NetwPredictionEngine::arrivals_this_frame_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->lane_cursor.arrivals_this_frame;
}

void NetwPredictionEngine::set_arrivals_this_frame(
    int64_t p_slot,
    int p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.arrivals_this_frame = p_value;
    }
}

int64_t NetwPredictionEngine::cooldown_until_tick_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->lane_cursor.cooldown_until_tick;
}

void NetwPredictionEngine::set_cooldown_until_tick(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->lane_cursor.cooldown_until_tick = p_value;
    }
}

double NetwPredictionEngine::tick_delta_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 1.0 / 60.0 : row->cursor.tick_delta;
}

void NetwPredictionEngine::set_tick_delta(int64_t p_slot, double p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.tick_delta = p_value;
    }
}

int64_t NetwPredictionEngine::frame_index_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->cursor.frame_index;
}

void NetwPredictionEngine::set_frame_index(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.frame_index = p_value;
    }
}

int NetwPredictionEngine::declared_quantum_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 1 : row->cursor.declared_quantum;
}

void NetwPredictionEngine::set_declared_quantum(int64_t p_slot, int p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.declared_quantum = p_value;
    }
}

int64_t NetwPredictionEngine::latest_input_tick_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->cursor.latest_input_tick;
}

void NetwPredictionEngine::set_latest_input_tick(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.latest_input_tick = p_value;
    }
}

int64_t NetwPredictionEngine::last_driven_input_tick_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->cursor.last_driven_input_tick;
}

void NetwPredictionEngine::set_last_driven_input_tick(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.last_driven_input_tick = p_value;
    }
}

int64_t NetwPredictionEngine::last_frame_transition_tick_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->cursor.last_frame_transition_tick;
}

void NetwPredictionEngine::set_last_frame_transition_tick(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.last_frame_transition_tick = p_value;
    }
}

int64_t NetwPredictionEngine::last_recorded_input_tick_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->cursor.last_recorded_input_tick;
}

void NetwPredictionEngine::set_last_recorded_input_tick(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.last_recorded_input_tick = p_value;
    }
}

bool NetwPredictionEngine::raw_fingerprints_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? false : row->cursor.raw_fingerprints;
}

void NetwPredictionEngine::set_raw_fingerprints(int64_t p_slot, bool p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->cursor.raw_fingerprints = p_value;
    }
}

void NetwPredictionEngine::seed_recovery_ledger(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || row->state_binding.is_null()
        || row->state_binding->node() == nullptr
        || row->state_binding->get_set().is_null()) {
        return;
    }
    for (uint32_t at = 0; at < row->wiring.causal.size(); ++at) {
        if (row->wiring.causal[at] != 0) {
            row->recovery_ledger.seed(int(at));
        }
    }
}

void NetwPredictionEngine::ledger_seed(
    int64_t p_slot,
    const StringName &p_key
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->recovery_ledger.seed(row->wiring.fields.index_of(p_key));
    }
}

void NetwPredictionEngine::ledger_bump_triggered(
    int64_t p_slot,
    const StringName &p_key
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->recovery_ledger.bump_triggered(row->wiring.fields.index_of(p_key));
    }
}

void NetwPredictionEngine::ledger_bump_repaired(
    int64_t p_slot,
    const StringName &p_key
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->recovery_ledger.bump_repaired(row->wiring.fields.index_of(p_key));
    }
}

void NetwPredictionEngine::ledger_bump_contracted(
    int64_t p_slot,
    const StringName &p_key
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->recovery_ledger.bump_contracted(
            row->wiring.fields.index_of(p_key)
        );
    }
}

PackedInt64Array NetwPredictionEngine::ledger_counts(
    int64_t p_slot,
    const StringName &p_key
) const {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(6);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const int field = row->wiring.fields.index_of(p_key);
    if (field < 0) {
        return out;
    }
    int64_t *values = out.ptrw();
    values[0] = row->recovery_ledger.triggered[uint32_t(field)];
    values[1] = row->recovery_ledger.repaired[uint32_t(field)];
    values[2] = row->recovery_ledger.contracted[uint32_t(field)];
    const predict::CarryFieldStats *carried = row->carry.field(field);
    if (carried != nullptr) {
        values[3] = carried->carried;
        values[4] = carried->declined;
        values[5] = carried->infidelity;
    }
    return out;
}

Array NetwPredictionEngine::ledger_fields(int64_t p_slot) const {
    Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (row->recovery_ledger.has(at)) {
            out.push_back(row->wiring.fields.name_at(at));
        }
    }
    return out;
}

Dictionary NetwPredictionEngine::field_recovery(int64_t p_slot) {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (!row->recovery_ledger.has(at)) {
            continue;
        }
        const StringName &key = row->wiring.fields.name_at(at);
        out[key] = NetwPredictFieldRecovery::of(this, p_slot, key);
    }
    return out;
}

bool NetwPredictionEngine::note_simulated_by(
    int64_t p_slot,
    int64_t p_subject,
    const Callable &p_predictor
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && row->simulated_by.note(p_subject, p_predictor);
}

bool NetwPredictionEngine::clear_simulated_by(
    int64_t p_slot,
    int64_t p_subject
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && row->simulated_by.erase(p_subject);
}

void NetwPredictionEngine::clear_simulation_subjects(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->simulated_by.clear();
    }
}

int NetwPredictionEngine::simulation_subject_count(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->simulated_by.count();
}

Callable NetwPredictionEngine::first_simulation_predictor(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Callable() : row->simulated_by.first_valid();
}

void NetwPredictionEngine::note_breach_source(
    int64_t p_slot,
    const StringName &p_source
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->breach_source = p_source;
    }
}

StringName NetwPredictionEngine::breach_source_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->breach_source == StringName()) {
        return StringName("default");
    }
    return row->breach_source;
}

void NetwPredictionEngine::note_divergence(
    int64_t p_slot,
    const Dictionary &p_by_field
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        fill_readings(*row, row->report.divergence, p_by_field);
    }
}

void NetwPredictionEngine::clear_divergence(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->report.divergence.clear();
    }
}

Dictionary NetwPredictionEngine::divergence_report(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return read_readings(
        row,
        row == nullptr ? predict::FieldReadings() : row->report.divergence
    );
}

double NetwPredictionEngine::divergence_of(
    int64_t p_slot,
    const StringName &p_key,
    double p_absent
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return p_absent;
    }
    return row->report.divergence.at(
        row->wiring.fields.index_of(p_key),
        p_absent
    );
}

bool NetwPredictionEngine::has_divergence(
    int64_t p_slot,
    const StringName &p_key
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        && row->report.divergence.has(row->wiring.fields.index_of(p_key));
}

void NetwPredictionEngine::note_tier_errors(
    int64_t p_slot,
    const Dictionary &p_by_field
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        fill_readings(*row, row->report.tier_error, p_by_field);
    }
}

void NetwPredictionEngine::clear_tier_errors(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->report.tier_error.clear();
    }
}

Dictionary NetwPredictionEngine::tier_error_report(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return read_readings(
        row,
        row == nullptr ? predict::FieldReadings() : row->report.tier_error
    );
}

void NetwPredictionEngine::note_verdict_reason(int64_t p_slot, int p_reason) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->report.verdict_reason = p_reason;
    }
}

int NetwPredictionEngine::verdict_reason_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->report.verdict_reason;
}

void NetwPredictionEngine::note_attribution(
    int64_t p_slot,
    int p_attribution,
    int64_t p_transition
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->report.attribution = p_attribution;
        row->report.attributed_transition = p_transition;
    }
}

int NetwPredictionEngine::attribution_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->report.attribution;
}

int64_t NetwPredictionEngine::attributed_transition_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->report.attributed_transition;
}

void NetwPredictionEngine::note_compare_staleness(int64_t p_slot, int p_ticks) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->report.compare_staleness = p_ticks;
    }
}

int NetwPredictionEngine::compare_staleness_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->report.compare_staleness;
}

void NetwPredictionEngine::note_reconciling(int64_t p_slot, bool p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->report.reconciling = p_value;
    }
}

bool NetwPredictionEngine::reconciling(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->report.reconciling;
}

double NetwPredictionEngine::epsilon_of_slot(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0.0 : row->config.epsilon;
}

double NetwPredictionEngine::teleport_threshold_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0.0 : row->config.teleport_threshold;
}

int NetwPredictionEngine::collision_cooldown_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->config.collision_cooldown_ticks;
}

int NetwPredictionEngine::carry_rule_count(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : int(row->carry_rules.size());
}

bool NetwPredictionEngine::has_carry_rule(
    int64_t p_slot,
    const StringName &p_field
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->carry_rules.has(p_field);
}

Array NetwPredictionEngine::carry_rule_fields(int64_t p_slot) const {
    Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (int at = 0; at < row->wiring.fields.count(); ++at) {
        const StringName &key = row->wiring.fields.name_at(at);
        if (row->carry_rules.has(key)) {
            out.push_back(key);
        }
    }
    return out;
}

void NetwPredictionEngine::set_carry(
    int64_t p_slot,
    const StringName &p_field,
    const Callable &p_callable
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    if (p_callable.is_valid()) {
        row->carry_rules[p_field] = p_callable;
    } else {
        row->carry_rules.erase(p_field);
    }
}

void NetwPredictionEngine::set_order_key(int64_t p_slot, int64_t p_order_key) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->order_key = p_order_key;
    }
}

int64_t NetwPredictionEngine::order_key_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->order_key : -1;
}

namespace {

bool authors_pass(int p_role) {
    return p_role == int(Role::PREDICT) || p_role == int(Role::CONSUME)
        || p_role == int(Role::HOST_LOCAL);
}

bool steps_in_phase(const predict::Slot &p_row, int p_phase) {
    const int role = p_row.config.role;
    const int schedule = p_row.config.schedule;
    const bool tick = schedule == int(Schedule::TICK);
    const bool frame = schedule == int(Schedule::FRAME);
    const bool stepped = schedule == int(Schedule::STEPPED);
    const bool authoring_fallback
        = role == int(Role::REMOTE) && p_row.quarantine.latched;
    switch (p_phase) {
        case NetwPredictionEngine::PASS_ISLAND_TICK:
            return island_pass_admits(int(Schedule::TICK), schedule)
                && authors_pass(role);
        case NetwPredictionEngine::PASS_ISLAND_FRAME:
            return island_pass_admits(int(Schedule::FRAME), schedule)
                && authors_pass(role);
        case NetwPredictionEngine::PASS_JOINT:
            return p_row.config.island == NetwPredictionEngine::ISLAND_JOINT
                && authors_pass(role);
        case NetwPredictionEngine::PASS_TICK:
            return tick || role == int(Role::PREDICT)
                || role == int(Role::HOST_LOCAL) || authoring_fallback;
        case NetwPredictionEngine::PASS_FRAME:
            return frame
                && (authors_pass(role) || authoring_fallback
                    || role == int(Role::SIMULATE));
        case NetwPredictionEngine::PASS_FINALIZE_FRAME:
            return frame && role == int(Role::PREDICT);
        case NetwPredictionEngine::PASS_STEPPED:
            return stepped
                && (authors_pass(role) || authoring_fallback
                    || role == int(Role::SIMULATE));
        default:
            return false;
    }
}

} // namespace

PackedInt64Array NetwPredictionEngine::pass_slots(int p_phase) const {
    NETW_ZONE_NC("predict pass slots", colors::PREDICTION);
    const PackedInt64Array ordered = ordered_slots();
    PackedInt64Array out;
    for (int at = 0; at < ordered.size(); ++at) {
        const predict::Slot *row = row_of(ordered[at]);
        if (row != nullptr && steps_in_phase(*row, p_phase)) {
            out.push_back(ordered[at]);
        }
    }
    return out;
}

PackedInt64Array NetwPredictionEngine::ordered_slots() const {
    NETW_ZONE_NC("predict ordered slots", colors::PREDICTION);
    LocalVector<int64_t> keys;
    LocalVector<int64_t> slots;
    for (const KeyValue<int64_t, predict::Slot> &row : rows) {
        keys.push_back(
            row.value.order_key < 0 ? INT64_MAX : row.value.order_key
        );
        slots.push_back(row.key);
    }
    for (uint32_t at = 1; at < slots.size(); ++at) {
        const int64_t key = keys[at];
        const int64_t slot = slots[at];
        uint32_t place = at;
        while (place > 0
               && (keys[place - 1] > key
                   || (keys[place - 1] == key && slots[place - 1] > slot))) {
            keys[place] = keys[place - 1];
            slots[place] = slots[place - 1];
            place -= 1;
        }
        keys[place] = key;
        slots[place] = slot;
    }
    PackedInt64Array out;
    out.resize(int(slots.size()));
    for (uint32_t at = 0; at < slots.size(); ++at) {
        out.set(int(at), slots[at]);
    }
    return out;
}

bool NetwPredictionEngine::has_simulate(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->simulate.is_valid();
}

Dictionary NetwPredictionEngine::run_step(
    int64_t p_slot,
    const Dictionary &p_input,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    NETW_ZONE_NC("predict run step", colors::PREDICTION);
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    if (!apply_input_via_binding(*row, p_input)) {
        apply_through(*row, row->input_codec, row->owner_has_input, p_input);
    }
    const Callable step = row->simulate;
    depth += 1;
    if (step.is_valid()) {
        NETW_ZONE_NC("predict game simulate step", colors::PREDICTION);
        Array arguments;
        arguments.push_back(p_delta);
        arguments.push_back(p_tick);
        arguments.push_back(p_fresh);
        step.callv(arguments);
    }
    depth -= 1;
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    return capture_through(*row, row->wiring.codec, row->owner_has_state);
}

Dictionary NetwPredictionEngine::canonicalize_state(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? predict::canonicalize(row->wiring.codec, p_payload)
                          : p_payload.duplicate();
}

Dictionary NetwPredictionEngine::canonicalize_input(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? predict::canonicalize(row->input_codec, p_payload)
                          : p_payload.duplicate();
}

Dictionary NetwPredictionEngine::coast_command(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? predict::zero_row(row->input_codec) : Dictionary();
}

int64_t NetwPredictionEngine::sample_environment(
    int64_t p_slot,
    int64_t p_epoch
) {
    NETW_ZONE_NC("predict sample environment", colors::PREDICTION);
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    if (p_epoch == -1 && row->sensors.is_empty()) {
        row->last_samples = Dictionary();
        return 0;
    }
    Dictionary samples;
    depth += 1;
    for (const KeyValue<StringName, Callable> &sensor : row->sensors) {
        if (sensor.value.is_valid()) {
            samples[sensor.key] = sensor.value.call();
        }
    }
    depth -= 1;
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    row->last_samples = samples;
    return prediction_core::environment_digest(p_epoch, samples);
}

Dictionary NetwPredictionEngine::sensor_samples(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->last_samples : Dictionary();
}

PackedByteArray NetwPredictionEngine::canonical_state_bytes(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        ? predict::canonical_bytes(row->wiring.codec, p_payload)
        : PackedByteArray();
}

PackedByteArray NetwPredictionEngine::canonical_input_bytes(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        ? predict::canonical_bytes(row->input_codec, p_payload)
        : PackedByteArray();
}

void NetwPredictionEngine::close(int64_t p_slot) {
    if (roster_open("close")) {
        rows.erase(p_slot);
        HashMap<int64_t, Ref<RefCounted>>::ConstIterator seated
            = entity_by_slot.find(p_slot);
        if (seated != entity_by_slot.end()) {
            if (seated->value.is_valid()) {
                slot_by_entity.erase(seated->value->get_instance_id());
            }
            entity_by_slot.erase(p_slot);
        }
    }
}

int64_t NetwPredictionEngine::slot_register(const Ref<RefCounted> &p_entity) {
    if (p_entity.is_null()) {
        NETW_ERR_V(-1, sys::PREDICTION, "slot_register refused a null entity");
    }
    const int64_t standing = slot_of(p_entity);
    if (standing >= 0) {
        return standing;
    }
    const int64_t slot = open();
    if (slot < 0) {
        return slot;
    }
    slot_by_entity.insert(p_entity->get_instance_id(), slot);
    entity_by_slot.insert(slot, p_entity);
    NETW_TRACE(
        sys::PREDICTION,
        "slot %d seated an entity, %d registered",
        slot,
        int64_t(slot_by_entity.size())
    );
    return slot;
}

int64_t NetwPredictionEngine::slot_of(const Ref<RefCounted> &p_entity) const {
    if (p_entity.is_null()) {
        return -1;
    }
    HashMap<uint64_t, int64_t>::ConstIterator seated
        = slot_by_entity.find(p_entity->get_instance_id());
    return seated != slot_by_entity.end() ? seated->value : -1;
}

void NetwPredictionEngine::slot_unregister(const Ref<RefCounted> &p_entity) {
    const int64_t slot = slot_of(p_entity);
    if (slot >= 0) {
        close(slot);
    }
}

int64_t NetwPredictionEngine::slot_registered() const {
    return int64_t(slot_by_entity.size());
}

bool NetwPredictionEngine::slot_bind_owner(
    const Ref<RefCounted> &p_entity,
    Object *p_owner
) {
    const int64_t slot = slot_of(p_entity);
    return slot >= 0 && bind_owner(slot, p_owner);
}

void NetwPredictionEngine::slot_unbind_owner(const Ref<RefCounted> &p_entity) {
    unbind_owner(slot_of(p_entity));
}

bool NetwPredictionEngine::is_open(int64_t p_slot) const {
    return row_of(p_slot) != nullptr;
}

int NetwPredictionEngine::open_count() const {
    int count = 0;
    for (const KeyValue<int64_t, predict::Slot> &row : rows) {
        count += row.value.open ? 1 : 0;
    }
    return count;
}

bool NetwPredictionEngine::supports(
    int p_schedule,
    int p_role,
    int p_correction,
    int p_restore,
    int p_island
) const {
    const bool scheduled = p_schedule == int(Schedule::TICK)
        || p_schedule == int(Schedule::FRAME)
        || p_schedule == int(Schedule::STEPPED);
    const bool island = p_island >= 0 && p_island <= 2;
    const bool declared
        = p_role >= int(Role::PREDICT) && p_role <= int(Role::SIMULATE);
    const bool role
        = declared && (p_role != int(Role::SIMULATE) || p_island > 0);
    const bool joint = p_island != 2 || rerunnable(p_schedule);
    const bool corrected = p_correction == int(CorrectionMode::SNAP)
        || p_correction == int(CorrectionMode::REPLAY);
    const bool restored = p_restore == int(RestoreMode::EXACT)
        || p_restore == int(RestoreMode::EXTRAPOLATED);
    return scheduled && island && role && joint && corrected && restored;
}

Dictionary NetwPredictionEngine::archetype_axes(int p_archetype) {
    const predict::ArchetypeAxes axes = predict::archetype_axes(p_archetype);
    Dictionary out;
    out[StringName("declared")] = axes.declared;
    out[StringName("schedule")] = axes.schedule;
    out[StringName("missing_policy")] = axes.missing_policy;
    out[StringName("recovery_policy")] = axes.recovery_policy;
    out[StringName("snap_restore")] = axes.snap_restore;
    out[StringName("declares_snap_restore")] = axes.declares_snap_restore;
    out[StringName("teleport_threshold")] = axes.teleport_threshold;
    out[StringName("declares_teleport_threshold")]
        = axes.declares_teleport_threshold;
    return out;
}

int NetwPredictionEngine::role_for_axes(int p_input_source, int p_sim_mode) {
    return predict::role_for_axes(p_input_source, p_sim_mode);
}

int NetwPredictionEngine::correction_for_recovery_policy(int p_policy) {
    return predict::correction_for_recovery_policy(p_policy);
}

bool NetwPredictionEngine::reconfigure_from(
    int64_t p_slot,
    const Ref<NetwPredictionHandle> &p_handle
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_handle.is_null()) {
        return false;
    }
    const Ref<NetwPredictIsland> island = p_handle->get_island();
    const bool declared = island.is_valid() && island->get_declared();
    int seat = ISLAND_NONE;
    if (p_handle->get_reconcile_mode() == int(NetwPredict::RECONCILE_JOINT)) {
        seat = ISLAND_JOINT;
    } else if (declared || simulation_subject_count(p_slot) > 0) {
        seat = ISLAND_DECLARED;
    }
    return configure(
        p_slot,
        resolved_schedule(p_slot, p_handle->get_schedule()),
        row->config.role,
        row->config.correction,
        p_handle->get_snap_restore(),
        p_handle->get_max_restore_ticks(),
        seat,
        p_handle->get_witness_contacts().is_valid(),
        declared,
        island.is_valid() && island->get_approximate(),
        p_handle->get_divergence_epsilon(),
        p_handle->get_teleport_threshold(),
        p_handle->get_collision_cooldown_ticks()
    );
}

bool NetwPredictionEngine::configure(
    int64_t p_slot,
    int p_schedule,
    int p_role,
    int p_correction,
    int p_restore,
    int p_max_restore_ticks,
    int p_island,
    bool p_witness,
    bool p_island_declared,
    bool p_island_approximate,
    double p_epsilon,
    double p_teleport_threshold,
    int p_collision_cooldown_ticks
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr
        || !supports(p_schedule, p_role, p_correction, p_restore, p_island)) {
        return false;
    }
    row->config.schedule = p_schedule;
    row->config.role = p_role;
    row->config.correction = p_correction;
    row->config.restore = p_restore;
    row->config.max_restore_ticks = std::max(0, p_max_restore_ticks);
    row->config.island = p_island;
    row->config.witness = p_witness;
    row->config.island_declared = p_island_declared;
    row->config.island_approximate = p_island_approximate;
    row->config.epsilon = std::max(0.0, p_epsilon);
    row->config.teleport_threshold = std::max(0.0, p_teleport_threshold);
    row->config.collision_cooldown_ticks
        = std::max(0, p_collision_cooldown_ticks);
    return true;
}

bool NetwPredictionEngine::island_declared(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->config.island_declared;
}

bool NetwPredictionEngine::island_approximate(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->config.island_approximate;
}

int64_t NetwPredictionEngine::out_of_domain_until(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->out_of_domain_until : -1;
}

bool NetwPredictionEngine::out_of_domain_at(
    int64_t p_slot,
    int64_t p_label
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->out_of_domain_until >= 0
        && p_label < row->out_of_domain_until;
}

void NetwPredictionEngine::open_out_of_domain_window(
    int64_t p_slot,
    int64_t p_label,
    int p_cooldown
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->out_of_domain_until = prediction_core::window_after(
            p_label,
            p_cooldown,
            row->out_of_domain_until
        );
    }
}

void NetwPredictionEngine::clear_out_of_domain_window(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->out_of_domain_until = -1;
    }
}

bool NetwPredictionEngine::adopt_environment_epoch(
    int64_t p_slot,
    int64_t p_epoch,
    int64_t p_label,
    int p_cooldown
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || row->environment_epoch == p_epoch) {
        return false;
    }
    if (row->environment_epoch != -1) {
        row->out_of_domain_until = prediction_core::window_after(
            p_label,
            p_cooldown,
            row->out_of_domain_until
        );
    }
    row->environment_epoch = p_epoch;
    return true;
}

void NetwPredictionEngine::clear_environment_epoch(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->environment_epoch = -1;
    }
}

void NetwPredictionEngine::record_owner_claims(
    int64_t p_slot,
    const predict::CommandFrameRecord &p_frame
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const predict::CommandFrame &frame = p_frame.wire();
    const uint32_t claimed_count
        = MIN(frame.evidence.size(), frame.transitions.size());
    for (uint32_t at = 0; at < claimed_count; ++at) {
        const int64_t claimed = frame.transitions[at].index;
        if (claimed < 0) {
            continue;
        }
        row->owner_claims.record(claimed, frame.evidence[at]);
    }
    row->owner_claims.trim(predict::TAPE_HISTORY_LIMIT);
}

void NetwPredictionEngine::clear_owner_claims(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->owner_claims.clear();
    }
}

int NetwPredictionEngine::owner_claim_count(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->owner_claims.size() : 0;
}

PackedInt64Array NetwPredictionEngine::judge_owner_claim(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_fingerprint
) {
    PackedInt64Array verdict;
    verdict.push_back(0);
    verdict.push_back(0);
    verdict.push_back(-1);

    predict::Slot *row = mutable_row_of(p_slot);
    predict::CommandEvidenceWire claim;
    if (row == nullptr || !row->owner_claims.take(p_transition, claim)) {
        return verdict;
    }
    verdict.set(0, 1);
    if (int64_t(claim.post_fp) == p_fingerprint) {
        verdict.set(1, 1);
        return verdict;
    }

    const int at = row->journal.index_of(p_transition);
    if (at < 0) {
        return verdict;
    }

    const uint8_t local_evidence = row->journal.evidence_mask_at(at);
    const bool substituted
        = (row->journal.flags_at(at) & predict::ROW_SUBSTITUTED) != 0;
    const bool both_witness = (local_evidence & predict::EVIDENCE_WITNESS) != 0
        && (claim.evidence_mask & predict::EVIDENCE_WITNESS) != 0;
    const int32_t witness_fp = row->journal.witness_fp_at(at);
    row->journal.mark_witness_match(
        p_transition,
        both_witness && claim.witness_fp == witness_fp
    );

    const int attribution = prediction_core::attribute(
        claim.pre_fp == row->journal.pre_fp_at(at),
        !substituted,
        claim.e_digest == row->journal.e_digest_at(at),
        claim.topo_fp == row->journal.topo_fp_at(at),
        claim.raw_fp == row->journal.raw_fp_at(at),
        claim.witness_fp == witness_fp,
        local_evidence,
        claim.evidence_mask,
        true
    );
    row->journal.mark_attribution(
        p_transition,
        predict::Attribution(attribution)
    );

    PackedInt32Array local_family;
    PackedInt32Array peer_family;
    if (attribution == int(predict::Attribution::PRE_STATE)) {
        const predict::FamilyFingerprints families
            = row->journal.pre_families_at(at);
        local_family.push_back(families.pose);
        local_family.push_back(families.momentum);
        local_family.push_back(families.controller);
        peer_family.push_back(claim.pre_pose_fp);
        peer_family.push_back(claim.pre_momentum_fp);
        peer_family.push_back(claim.pre_controller_fp);
    } else if (attribution == int(predict::Attribution::CLOSURE)) {
        const predict::FamilyFingerprints families
            = row->journal.post_families_at(at);
        local_family.push_back(families.pose);
        local_family.push_back(families.momentum);
        local_family.push_back(families.controller);
        peer_family.push_back(claim.post_pose_fp);
        peer_family.push_back(claim.post_momentum_fp);
        peer_family.push_back(claim.post_controller_fp);
    }
    row->journal.mark_differing_family(
        p_transition,
        predict::DifferingFamily(
            prediction_core::differing_family(local_family, peer_family)
        )
    );

    verdict.set(2, attribution);
    return verdict;
}

void NetwPredictionEngine::record_authority_witness_class(
    int64_t p_slot,
    int64_t p_transition,
    int p_witness_class
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->authority_witness_classes
            .record(p_transition, p_witness_class, predict::TAPE_HISTORY_LIMIT);
    }
}

int NetwPredictionEngine::authority_witness_class(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->authority_witness_classes.at(p_transition)
                          : -1;
}

void NetwPredictionEngine::clear_authority_witness_classes(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->authority_witness_classes.clear();
    }
}

void NetwPredictionEngine::ledger_arm(
    int64_t p_slot,
    int p_field,
    double p_error,
    int64_t p_basis
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr && p_field >= 0) {
        row->contraction_ledger.arm(p_field, p_error, p_basis);
    }
}

TypedArray<Dictionary> NetwPredictionEngine::tape_transitions(
    int64_t p_slot
) const {
    TypedArray<Dictionary> out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const StringName index_key("index");
    const StringName label_key("label");
    const StringName fresh_key("fresh");
    if (row->config.role == int(Role::PREDICT)) {
        const PackedInt64Array span = tape_span(p_slot);
        for (int64_t at = span[0]; at <= span[1]; ++at) {
            Dictionary entry;
            entry[index_key] = at;
            entry[label_key] = tape_label_of(p_slot, at);
            entry[fresh_key] = tape_is_fresh(p_slot, at);
            out.push_back(entry);
        }
        return out;
    }
    const PackedInt64Array held = command_transitions(p_slot);
    for (int at = 0; at < held.size(); ++at) {
        Dictionary entry;
        entry[index_key] = held[at];
        entry[label_key] = command_label_of(p_slot, held[at]);
        entry[fresh_key] = command_is_fresh(p_slot, held[at]);
        out.push_back(entry);
    }
    return out;
}

Dictionary NetwPredictionEngine::command_cell_at(
    int64_t p_slot,
    int64_t p_transition
) const {
    Dictionary out;
    if (row_of(p_slot) == nullptr) {
        return out;
    }
    const int provenance = joint_provenance_at(p_slot, p_transition);
    if (provenance != int(predict::CellProvenance::RELAYED)
        && provenance != int(predict::CellProvenance::SUBSTITUTED)) {
        return out;
    }
    out[StringName("command")] = joint_command_at(p_slot, p_transition);
    out[StringName("origin")]
        = provenance == int(predict::CellProvenance::RELAYED)
        ? int(NetwPredict::COMMAND_ORIGIN_RELAYED)
        : int(NetwPredict::COMMAND_ORIGIN_PREDICTED);
    return out;
}

PackedFloat64Array NetwPredictionEngine::divergence_columns(
    int64_t p_slot
) const {
    PackedFloat64Array row_out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->state_binding.is_null()
        || row->state_binding->get_set().is_null()) {
        return row_out;
    }
    const int count = row->wiring.count();
    row_out.resize(count);
    double *values = row_out.ptrw();
    for (int at = 0; at < count; ++at) {
        values[at] = row->report.divergence.at(
            at,
            std::numeric_limits<double>::infinity()
        );
    }
    return row_out;
}

void NetwPredictionEngine::ledger_note_comparison(
    int64_t p_slot,
    bool p_corrected,
    int64_t p_ack,
    double p_divergence_epsilon
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    if (row->contraction_ledger.armed()) {
        const PackedInt32Array settled
            = ledger_settle(p_slot, p_ack, divergence_columns(p_slot));
        for (int at = 0; at < settled.size(); ++at) {
            row->recovery_ledger.bump_contracted(settled[at]);
        }
    }
    if (!p_corrected) {
        return;
    }
    const double absent = std::numeric_limits<double>::infinity();
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (!row->report.divergence.has(at)
            || row->wiring.trigger_exclude[uint32_t(at)] != 0
            || row->wiring.causal[uint32_t(at)] == 0) {
            continue;
        }
        const double declared = row->wiring.epsilon[uint32_t(at)];
        const double epsilon = declared < 0.0 ? p_divergence_epsilon : declared;
        if (!NetwPredictionHandle::triggers(
                row->report.divergence.at(at, absent),
                epsilon
            )) {
            continue;
        }
        row->recovery_ledger.bump_triggered(at);
    }
}

bool NetwPredictionEngine::ledger_armed(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->contraction_ledger.armed();
}

PackedInt32Array NetwPredictionEngine::ledger_settle(
    int64_t p_slot,
    int64_t p_ack,
    const PackedFloat64Array &p_divergence
) {
    PackedInt32Array contracted;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return contracted;
    }
    const LocalVector<int> settled
        = row->contraction_ledger.settle(p_ack, p_divergence);
    for (uint32_t at = 0; at < settled.size(); ++at) {
        contracted.push_back(settled[at]);
    }
    return contracted;
}

void NetwPredictionEngine::ledger_clear(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->contraction_ledger.clear();
    }
}

Dictionary NetwPredictionEngine::pending_provenance(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->pending_provenance : Dictionary();
}

void NetwPredictionEngine::set_pending_provenance(
    int64_t p_slot,
    const Dictionary &p_provenance
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->pending_provenance = p_provenance;
    }
}

void NetwPredictionEngine::stamp_pending_provenance(
    int64_t p_slot,
    int64_t p_transition
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const Dictionary &staged = row->pending_provenance;
    const int write_id = int(int64_t(staged.get(StringName("write_id"), 0)));
    if (write_id <= 0) {
        return;
    }
    mark_provenance(
        p_slot,
        p_transition,
        int(int64_t(staged.get(StringName("episode"), 0))),
        write_id,
        int(int64_t(staged.get(StringName("operator"), 0))),
        int64_t(staged.get(StringName("basis"), -1))
    );
}

int NetwPredictionEngine::schedule_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->config.schedule : -1;
}

int NetwPredictionEngine::role_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->config.role : -1;
}

int NetwPredictionEngine::correction_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? int(CorrectionMode::REPLAY)
                          : row->config.correction;
}

void NetwPredictionEngine::set_role(int64_t p_slot, int p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->config.role = p_value;
    }
}

void NetwPredictionEngine::set_correction(int64_t p_slot, int p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->config.correction = p_value;
    }
}

int NetwPredictionEngine::island_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->config.island : ISLAND_NONE;
}

predict::ConsumeInputPlan NetwPredictionEngine::plan_consume_input(
    int p_schedule,
    bool p_has_input,
    bool p_has_later_input,
    bool p_has_last_input,
    int p_missing_policy
) const {
    NETW_ERR_COND_V(
        p_schedule != int(Schedule::TICK) && p_schedule != int(Schedule::FRAME),
        predict::ConsumeInputPlan(),
        sys::PREDICTION,
        "A consume input plan requires a TICK or FRAME schedule."
    );
    NETW_ERR_COND_V(
        p_missing_policy < int(MissingInput::STALL)
            || p_missing_policy > int(MissingInput::REPEAT_LAST),
        predict::ConsumeInputPlan(),
        sys::PREDICTION,
        "The missing input policy is outside the declared enum."
    );
    return predict::plan_consume_input(
        Schedule(p_schedule),
        p_has_input,
        p_has_later_input,
        p_has_last_input,
        MissingInput(p_missing_policy)
    );
}

int NetwPredictionEngine::field_count(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->wiring.count() : 0;
}

int NetwPredictionEngine::field_slot(
    int64_t p_slot,
    const StringName &p_key
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->wiring.fields.index_of(p_key) : -1;
}

StringName NetwPredictionEngine::field_name(int64_t p_slot, int p_field) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->wiring.fields.name_at(p_field) : StringName();
}

#define NETW_PREDICT_TABLE_READ(m_name, m_column, m_type, m_absent) \
    m_type NetwPredictionEngine::m_name(int64_t p_slot, int p_field) const { \
        const predict::Slot *row = row_of(p_slot); \
        if (row == nullptr || p_field < 0 || p_field >= row->wiring.count()) { \
            return m_absent; \
        } \
        return m_type(row->wiring.m_column[uint32_t(p_field)]); \
    }

NETW_PREDICT_TABLE_READ(projection_of, projection, int, -1)
NETW_PREDICT_TABLE_READ(state_family_of, state_family, int, 0)
NETW_PREDICT_TABLE_READ(converge_rate_of, converge_rate, double, -1.0)
NETW_PREDICT_TABLE_READ(epsilon_of, epsilon, double, -1.0)
NETW_PREDICT_TABLE_READ(teleport_of, teleport, double, -1.0)
NETW_PREDICT_TABLE_READ(is_pose, pose, bool, false)
NETW_PREDICT_TABLE_READ(is_withheld, withheld, bool, false)
NETW_PREDICT_TABLE_READ(is_trigger_excluded, trigger_exclude, bool, false)
NETW_PREDICT_TABLE_READ(is_vote_excluded, vote_exclude, bool, false)
NETW_PREDICT_TABLE_READ(is_angle, angle, bool, false)
NETW_PREDICT_TABLE_READ(is_causal, causal, bool, false)

#undef NETW_PREDICT_TABLE_READ

void NetwPredictCarryContext::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_state"),
        &NetwPredictCarryContext::get_state
    );
    ClassDB::bind_method(
        D_METHOD("get_input"),
        &NetwPredictCarryContext::get_input
    );
    ClassDB::bind_method(
        D_METHOD("get_delta"),
        &NetwPredictCarryContext::get_delta
    );
    ClassDB::bind_method(
        D_METHOD("get_label"),
        &NetwPredictCarryContext::get_label
    );
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "state"), "", "get_state");
    ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "input"), "", "get_input");
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "delta"), "", "get_delta");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "label"), "", "get_label");
}

NetwPredictTiming NetwPredictTiming::of(
    int64_t p_tick,
    double p_delta,
    double p_ticktime,
    int64_t p_frame,
    int p_quantum,
    bool p_simulating
) {
    NetwPredictTiming out;
    out.carried.tick = p_tick;
    out.carried.delta = p_delta;
    out.carried.ticktime = p_ticktime;
    out.carried.frame = p_frame;
    out.carried.quantum = p_quantum;
    out.carried.simulating = p_simulating;
    return out;
}

predict::EvidenceRow evidence_row(
    int64_t p_pre_fp,
    int64_t p_c_hash,
    int64_t p_e_digest,
    int64_t p_post_fp,
    int64_t p_pre_pose_fp,
    int64_t p_pre_momentum_fp,
    int64_t p_pre_controller_fp,
    int64_t p_post_pose_fp,
    int64_t p_post_momentum_fp,
    int64_t p_post_controller_fp,
    int64_t p_topo_fp,
    int64_t p_witness_fp,
    int64_t p_raw_fp,
    int p_evidence_mask,
    bool p_complete
) {
    predict::EvidenceRow row;
    row.pre_fp = int32_t(p_pre_fp);
    row.c_hash = int32_t(p_c_hash);
    row.e_digest = int32_t(p_e_digest);
    row.post_fp = int32_t(p_post_fp);
    row.pre_families.pose = int32_t(p_pre_pose_fp);
    row.pre_families.momentum = int32_t(p_pre_momentum_fp);
    row.pre_families.controller = int32_t(p_pre_controller_fp);
    row.post_families.pose = int32_t(p_post_pose_fp);
    row.post_families.momentum = int32_t(p_post_momentum_fp);
    row.post_families.controller = int32_t(p_post_controller_fp);
    row.topo_fp = int32_t(p_topo_fp);
    row.witness_fp = int32_t(p_witness_fp);
    row.raw_fp = int32_t(p_raw_fp);
    row.evidence_mask = uint8_t(p_evidence_mask);
    row.complete = p_complete;
    return row;
}

void NetwPredictionEngine::record_input(
    int64_t p_slot,
    int64_t p_tick,
    int64_t p_c_hash
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->record_input(p_tick, int32_t(p_c_hash));
    }
}

predict::DriveRecord NetwPredictionEngine::open_drive(
    int64_t p_slot,
    const Dictionary &p_topology,
    int64_t p_tick,
    int64_t p_frame,
    double p_ticktime,
    int p_quantum,
    bool p_simulating,
    int64_t p_pre_fp,
    int64_t p_pre_pose_fp,
    int64_t p_pre_momentum_fp,
    int64_t p_pre_controller_fp,
    int64_t p_raw_fp,
    int p_evidence_mask
) {
    predict::DriveRecord out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    predict::Timing timing;
    timing.tick = p_tick;
    timing.frame = p_frame;
    timing.ticktime = p_ticktime;
    timing.quantum = p_quantum;
    timing.simulating = p_simulating;

    predict::StateStamp pre;
    pre.fp = int32_t(p_pre_fp);
    pre.families.pose = int32_t(p_pre_pose_fp);
    pre.families.momentum = int32_t(p_pre_momentum_fp);
    pre.families.controller = int32_t(p_pre_controller_fp);
    pre.raw_fp = int32_t(p_raw_fp);
    pre.evidence_mask = uint8_t(p_evidence_mask);

    return row->open_drive(timing, p_topology, pre);
}

predict::DriveRecord NetwPredictionEngine::replay_drive(
    int64_t p_slot,
    const Dictionary &p_topology,
    int64_t p_transition,
    int64_t p_label,
    int p_kind,
    int64_t p_tick,
    int64_t p_frame,
    double p_ticktime,
    int p_quantum,
    int64_t p_pre_fp,
    int64_t p_pre_pose_fp,
    int64_t p_pre_momentum_fp,
    int64_t p_pre_controller_fp,
    int64_t p_raw_fp,
    int p_evidence_mask,
    bool p_authoring
) {
    predict::DriveRecord out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    predict::Timing timing;
    timing.tick = p_tick;
    timing.frame = p_frame;
    timing.ticktime = p_ticktime;
    timing.quantum = p_quantum;

    predict::StateStamp pre;
    pre.fp = int32_t(p_pre_fp);
    pre.families.pose = int32_t(p_pre_pose_fp);
    pre.families.momentum = int32_t(p_pre_momentum_fp);
    pre.families.controller = int32_t(p_pre_controller_fp);
    pre.raw_fp = int32_t(p_raw_fp);
    pre.evidence_mask = uint8_t(p_evidence_mask);

    return row->replay_drive(
        timing,
        p_topology,
        p_transition,
        p_label,
        DriveKind(p_kind),
        pre,
        p_authoring
    );
}

bool NetwPredictionEngine::record_evidence(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_environment_epoch,
    const Dictionary &p_environment,
    const Dictionary &p_topology,
    const PackedStringArray &p_contact_ids,
    const PackedInt32Array &p_witness_classes,
    const PackedInt32Array &p_realizations,
    const PackedByteArray &p_outside_boundary,
    bool p_sleeping
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Solve evidence names closed slot %d.",
        p_slot
    );
    const int count = p_contact_ids.size();
    NETW_ERR_COND_V(
        p_witness_classes.size() != count || p_realizations.size() != count
            || p_outside_boundary.size() != count,
        false,
        sys::PREDICTION,
        "Witness columns have different lengths."
    );
    LocalVector<predict::WitnessContact> contacts;
    for (int at = 0; at < count; ++at) {
        predict::WitnessContact contact;
        contact.identity = p_contact_ids[at];
        contact.witness_class = p_witness_classes[at];
        contact.realization = p_realizations[at];
        contact.outside_boundary = p_outside_boundary[at] != 0;
        contacts.push_back(contact);
    }
    return row->record_evidence(
        p_transition,
        p_environment_epoch,
        p_environment,
        p_topology,
        contacts,
        p_sleeping
    );
}

int NetwPredictionEngine::resolve_axes(
    int64_t p_slot,
    const Ref<NetwPredictionHandle> &p_handle
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_handle.is_null()) {
        return int(NetwPredict::ROLE_REMOTE);
    }
    const bool is_server = row->axes.authority;
    const bool delay_closed = row->latches.fallback_latched
        || p_handle->get_recovery_policy()
            == int(NetwPredict::RECOVERY_POLICY_DELAY_CLOSED);
    if (row->axes.inputless) {
        const bool dragged = simulation_subject_count(p_slot) > 0;
        p_handle->set_input_source(int(NetwPredict::INPUT_SOURCE_NONE));
        p_handle->set_sim_mode(
            is_server ? int(NetwPredict::SIM_MODE_AUTHORITATIVE)
                : dragged && !delay_closed
                ? int(NetwPredict::SIM_MODE_SPECULATIVE)
                : int(NetwPredict::SIM_MODE_DISPLAY)
        );
    } else if (row->axes.controlled_locally) {
        p_handle->set_input_source(int(NetwPredict::INPUT_SOURCE_LOCAL));
        p_handle->set_sim_mode(
            is_server          ? int(NetwPredict::SIM_MODE_AUTHORITATIVE)
                : delay_closed ? int(NetwPredict::SIM_MODE_DISPLAY)
                               : int(NetwPredict::SIM_MODE_SPECULATIVE)
        );
    } else if (is_server) {
        p_handle->set_input_source(int(NetwPredict::INPUT_SOURCE_RECEIVED));
        p_handle->set_sim_mode(int(NetwPredict::SIM_MODE_AUTHORITATIVE));
    } else if (simulation_subject_count(p_slot) > 0) {
        p_handle->set_input_source(int(NetwPredict::INPUT_SOURCE_PREDICTED));
        p_handle->set_sim_mode(int(NetwPredict::SIM_MODE_SPECULATIVE));
    } else {
        p_handle->set_input_source(int(NetwPredict::INPUT_SOURCE_NONE));
        p_handle->set_sim_mode(int(NetwPredict::SIM_MODE_DISPLAY));
    }
    return NetwPredictionHandle::role_for_axes(
        static_cast<NetwPredict::InputSource>(p_handle->get_input_source()),
        static_cast<NetwPredict::SimMode>(p_handle->get_sim_mode())
    );
}

int NetwPredictionEngine::resolve_correction(
    int64_t p_slot,
    int p_declared
) const {
    if (p_declared != int(CorrectionMode::AUTO)) {
        return p_declared;
    }
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->owner_solves ? int(CorrectionMode::SNAP)
                                               : int(CorrectionMode::REPLAY);
}

void NetwPredictionEngine::record_episode_decision(
    int64_t p_slot,
    int p_operator,
    int64_t p_basis,
    bool p_eligible,
    bool p_applied,
    const Dictionary &p_eligibility
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    predict::EpisodeDecision decision;
    decision.op = predict::Operator(p_operator);
    decision.basis = p_basis;
    decision.eligible = p_eligible;
    decision.applied = p_applied;
    decision.eligibility = p_eligibility;
    row->episode.record_decision(decision);
}

bool NetwPredictionEngine::open_episode(
    int64_t p_slot,
    int64_t p_transition,
    int p_attribution
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    predict::Episode &episode = row->episode;
    if (episode.active && episode.state == predict::EpisodeState::OPEN) {
        return false;
    }
    episode
        .open(row->journal, p_transition, predict::Attribution(p_attribution));
    return true;
}

void NetwPredictionEngine::record_episode_divergence(
    int64_t p_slot,
    int64_t p_transition
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->episode.record_divergence(row->journal, p_transition);
    }
}

int NetwPredictionEngine::escalation_field_of(
    int64_t p_slot,
    const Array &p_predicted,
    const Array &p_authority,
    const PackedFloat64Array &p_field_errors,
    double p_fallback_epsilon
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return -1;
    }
    const int width = row->wiring.count();
    predict::RecoveryRequest request;
    request.predicted = state_row(p_predicted, width);
    request.authority = state_row(p_authority, width);
    request.field_errors = tolerance_row(p_field_errors, width);
    request.fallback_epsilon = p_fallback_epsilon;
    return predict::escalation_field(row->wiring, request);
}

int NetwPredictionEngine::trigger_shape_of(
    int64_t p_slot,
    const PackedFloat64Array &p_field_errors,
    double p_fallback_epsilon
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return int(predict::TriggerShape::NONE);
    }
    LocalVector<double> errors;
    errors.resize(uint32_t(p_field_errors.size()));
    for (uint32_t at = 0; at < errors.size(); ++at) {
        errors[at] = p_field_errors[int(at)];
    }
    return int(predict::trigger_shape(row->wiring, errors, p_fallback_epsilon));
}

void NetwPredictionEngine::record_episode_escalation(
    int64_t p_slot,
    int p_trigger_shape
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->episode.record_escalation(predict::TriggerShape(p_trigger_shape));
    }
}

void NetwPredictionEngine::stamp_episode_write_delta(
    int64_t p_slot,
    int p_delta_fp
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->episode.stamp_write_delta(int32_t(p_delta_fp));
    }
}

int NetwPredictionEngine::record_episode_write(
    int64_t p_slot,
    int p_operator,
    int64_t p_basis,
    int p_delta_fp,
    const StringName &p_target,
    int p_ack_age,
    int p_trigger_shape,
    bool p_evidence_free,
    bool p_null_operator
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    return row->episode.record_write(
        predict::Operator(p_operator),
        p_basis,
        int32_t(p_delta_fp),
        p_target,
        p_ack_age,
        predict::TriggerShape(p_trigger_shape),
        p_evidence_free,
        p_null_operator
    );
}

bool NetwPredictionEngine::episode_active(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->episode.active;
}

int NetwPredictionEngine::episode_state(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->episode.active ? int(row->episode.state) : -1;
}

PackedInt64Array NetwPredictionEngine::episode_stats(int64_t p_slot) const {
    PackedInt64Array out;
    out.resize(STAT_EPISODE_COUNT);
    const predict::Slot *row = row_of(p_slot);
    const predict::Episode absent;
    const predict::Episode &episode = row != nullptr ? row->episode : absent;
    int64_t *values = out.ptrw();
    values[STAT_EPISODE_ID] = episode.id;
    values[STAT_EPISODE_STATE] = episode.active ? int(episode.state) : -1;
    values[STAT_EPISODE_OPENED] = episode.opened_transition;
    values[STAT_EPISODE_ATTRIBUTION] = int(episode.attribution);
    values[STAT_EPISODE_NON_CONTRACTION] = episode.non_contraction_used;
    values[STAT_EPISODE_WITHHELD_NC] = episode.withheld_non_contractions;
    values[STAT_EPISODE_EVIDENCE_FREE_NC]
        = episode.evidence_free_non_contractions;
    values[STAT_EPISODE_NO_TRIGGER_NC] = episode.nc_no_trigger;
    values[STAT_EPISODE_MIXED_TRIGGER_NC] = episode.nc_mixed_trigger;
    values[STAT_EPISODE_CLOSURE_USED] = episode.closure_used;
    values[STAT_EPISODE_AGREEMENT_RUN] = episode.agreement_run;
    values[STAT_EPISODE_LAST_COMPARISON] = episode.last_comparison_transition;
    values[STAT_EPISODE_AGREEMENT_WRITE_ID] = episode.agreement_write_id;
    values[STAT_EPISODE_LAST_WRITE_ID] = episode.last_write_id;
    values[STAT_EPISODE_EVIDENCE_DROPPED] = episode.evidence_dropped;
    values[STAT_EPISODE_WRITE_COUNT] = int64_t(episode.writes.size());
    values[STAT_EPISODE_COMPARISON_COUNT] = int64_t(episode.comparisons.size());
    values[STAT_EPISODE_ACTIVE] = episode.active ? 1 : 0;
    values[STAT_EPISODE_TRANSPORT_DECIDED] = episode.transport_decided ? 1 : 0;
    values[STAT_EPISODE_DISSIPATE_DECIDED] = episode.dissipate_decided ? 1 : 0;
    return out;
}

bool NetwPredictionEngine::episode_budget_exhausted(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->episode.budget_exhausted();
}

bool NetwPredictionEngine::episode_operator_pending(
    int64_t p_slot,
    int p_operator
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        && row->episode.operator_pending(predict::Operator(p_operator));
}

bool NetwPredictionEngine::demote_for_breach(
    int64_t p_slot,
    int64_t p_transition,
    bool p_breached,
    int p_witness_fingerprint,
    const Callable &p_send_command,
    const Callable &p_enter_fallback
) {
    NETW_ZONE_NC("predict demote for breach", colors::PREDICTION);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (!p_breached || handle == nullptr
        || role_of(p_slot) != NetwPredict::ROLE_PREDICT
        || fallback_latched_of(p_slot)
        || handle->get_breach_response() != NetwPredict::BREACH_RESPONSE_DEMOTE
        || !handle->get_witness_contacts().is_valid()) {
        return false;
    }
    if (journal_has(p_slot, p_transition)) {
        mark_domain(p_slot, p_transition, int(predict::Domain::OUT_OF_DOMAIN));
    }
    const bool opened
        = episode_state(p_slot) != NetwPredict::EPISODE_STATE_OPEN;
    record_breach(p_slot, p_transition);
    if (opened) {
        sync_episode(p_slot);
        announce_episode(p_slot, EventPlane::EPISODE_OPEN, Dictionary());
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    record_episode_write(
        p_slot,
        int(predict::Operator::DEMOTE),
        p_transition,
        p_witness_fingerprint,
        seated.is_valid() ? seated->get_entity_id() : StringName(),
        handle->get_ack_age_ticks(),
        trigger_shape(p_slot, handle->get_divergence_epsilon()),
        true,
        false
    );
    sync_episode(p_slot);
    p_send_command.call();
    p_enter_fallback
        .call(p_transition, int(predict::Attribution::CONTACT), true);
    return true;
}

void NetwPredictionEngine::record_breach(int64_t p_slot, int64_t p_transition) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    predict::Episode &episode = row->episode;
    if (!episode.active || episode.state != predict::EpisodeState::OPEN) {
        episode.open(row->journal, p_transition, predict::Attribution::CONTACT);
    } else {
        episode.repin_generator(
            row->journal,
            p_transition,
            predict::Attribution::CONTACT
        );
    }
    episode.record_breach(p_transition);
}

bool NetwPredictionEngine::witness_row_clean(
    int64_t p_slot,
    int64_t p_transition,
    bool p_require_peer
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    const int index = row->journal.index_of(p_transition);
    if (index < 0) {
        return false;
    }
    const uint8_t evidence = row->journal.evidence_mask_at(index);
    if ((evidence & predict::EVIDENCE_WITNESS) == 0) {
        return false;
    }
    const uint8_t flags = row->journal.flags_at(index);
    if (p_require_peer && (flags & predict::ROW_WITNESS_MATCHED) == 0) {
        return false;
    }
    const uint8_t state = row->journal.witness_state_at(index);
    if ((state & (predict::WITNESS_SLEEPING | predict::WITNESS_WOKE)) != 0) {
        return false;
    }
    const uint8_t realized = row->journal.witness_realization_bits_at(index);
    if (realized == 0) {
        return false;
    }
    constexpr uint8_t REPRODUCIBLE
        = uint8_t(1u << int(predict::ContactClass::NONE))
        | uint8_t(1u << int(predict::ContactClass::DECLARED_SUPPORT))
        | uint8_t(1u << int(predict::ContactClass::OTHER_STATIC));
    return (realized & uint8_t(~REPRODUCIBLE)) == 0;
}

bool NetwPredictionEngine::transport_admissible(
    int64_t p_slot,
    bool p_candidate,
    bool p_basis_witness_clean,
    bool p_recent_witness_clean,
    bool p_non_pose_agrees,
    bool p_below_teleport,
    bool p_escalated,
    bool p_observing
) const {
    const predict::Slot *row = row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Transport evidence names closed slot %d.",
        p_slot
    );
    predict::TransportEvidence evidence;
    evidence.candidate = p_candidate;
    evidence.basis_witness_clean = p_basis_witness_clean;
    evidence.recent_witness_clean = p_recent_witness_clean;
    evidence.non_pose_agrees = p_non_pose_agrees;
    evidence.below_teleport = p_below_teleport;
    evidence.escalated = p_escalated;
    evidence.snap_correction
        = row->config.correction == int(CorrectionMode::SNAP);
    evidence.observing = p_observing;
    return predict::transport_admissible(evidence);
}

bool NetwPredictionEngine::dissipate_admissible(
    int64_t p_slot,
    bool p_momentum_active,
    bool p_other_active,
    bool p_basis_witness_clean,
    bool p_recent_witness_clean,
    bool p_escalated,
    bool p_observing,
    int p_meter
) const {
    const predict::Slot *row = row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Dissipate evidence names closed slot %d.",
        p_slot
    );
    predict::DissipateEvidence evidence;
    evidence.momentum_active = p_momentum_active;
    evidence.other_active = p_other_active;
    evidence.basis_witness_clean = p_basis_witness_clean;
    evidence.recent_witness_clean = p_recent_witness_clean;
    evidence.escalated = p_escalated;
    evidence.snap_correction
        = row->config.correction == int(CorrectionMode::SNAP);
    evidence.observing = p_observing;
    evidence.meter = p_meter;
    return predict::dissipate_admissible(evidence);
}

int NetwPredictionEngine::try_dissipate(
    int64_t p_slot,
    int64_t p_basis,
    int p_meter,
    int p_domain,
    bool p_escalated,
    double p_divergence_epsilon,
    bool p_observing,
    const StringName &p_target,
    int p_ack_age_ticks
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return DISSIPATE_UNDECIDED;
    }
    const PackedInt64Array stats = episode_stats(p_slot);
    if (stats[STAT_EPISODE_ACTIVE] == 0
        || stats[STAT_EPISODE_DISSIPATE_DECIDED] != 0
        || !dissipate_declared(p_slot)) {
        return DISSIPATE_UNDECIDED;
    }
    const Dictionary judged = dissipate_fields(
        p_slot,
        divergence_report(p_slot),
        p_domain,
        p_divergence_epsilon
    );
    const bool momentum_active = judged[StringName("momentum_active")];
    const bool other_active = judged[StringName("other_active")];
    const bool basis_clean = witness_row_clean(p_slot, p_basis, true);
    const bool recent_clean = recent_witness_clean(p_slot, p_basis);
    const bool eligible = dissipate_admissible(
        p_slot,
        momentum_active,
        other_active,
        basis_clean,
        recent_clean,
        p_escalated,
        p_observing,
        p_meter
    );
    Dictionary eligibility;
    eligibility[StringName("eligible")] = eligible;
    eligibility[StringName("momentum_active")] = momentum_active;
    eligibility[StringName("other_active")] = other_active;
    eligibility[StringName("basis_witness_clean")] = basis_clean;
    eligibility[StringName("recent_witness_clean")] = recent_clean;
    eligibility[StringName("mechanism")]
        = !p_observing && row->config.correction == int(CorrectionMode::SNAP);
    eligibility[StringName("fields")] = judged[StringName("fields")];
    record_episode_decision(
        p_slot,
        int(predict::Operator::DISSIPATE),
        p_basis,
        eligible,
        eligible,
        eligibility
    );
    if (eligible) {
        record_episode_write(
            p_slot,
            int(predict::Operator::DISSIPATE),
            p_basis,
            0,
            p_target,
            p_ack_age_ticks,
            trigger_shape(p_slot, p_divergence_epsilon),
            false,
            true
        );
    }
    return eligible ? DISSIPATE_APPLIED : DISSIPATE_DECLINED;
}

bool NetwPredictionEngine::dissipate_declared(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    for (uint32_t at = 0; at < row->wiring.withheld.size(); ++at) {
        if (row->wiring.withheld[at] != 0) {
            return true;
        }
    }
    return false;
}

bool NetwPredictionEngine::static_geometry(Object *p_collider) const {
    return predict::static_geometry(p_collider);
}

int NetwPredictionEngine::witness_class(
    Object *p_collider,
    bool p_declared_support
) const {
    return predict::witness_class(p_collider, p_declared_support);
}

Ref<NetwEntity> NetwPredictionEngine::contact_entity(Object *p_collider) const {
    Node *node = Object::cast_to<Node>(p_collider);
    if (node == nullptr) {
        return Ref<NetwEntity>();
    }
    const Ref<NetwEntity> entity = NetwEntity::of(node);
    if (entity.is_valid() && entity->get_declares_scene()) {
        return Ref<NetwEntity>();
    }
    return entity;
}

String NetwPredictionEngine::collider_identity(Object *p_collider) const {
    Node *node = Object::cast_to<Node>(p_collider);
    if (static_geometry(p_collider)) {
        return String("path:") + String(node->get_path());
    }
    const Ref<NetwEntity> entity = contact_entity(p_collider);
    if (entity.is_valid()) {
        return String("entity:") + String(entity->get_entity_id());
    }
    if (node != nullptr) {
        return String("path:") + String(node->get_path());
    }
    return String("object:")
        + (p_collider != nullptr ? p_collider->get_class() : String());
}

int NetwPredictionEngine::classify_collider(
    Object *p_collider,
    const String &p_support
) const {
    if (p_collider == nullptr) {
        return int(NetwPredict::CONTACT_CLASS_UNPREDICTED_DYNAMIC);
    }
    if (!p_support.is_empty() && collider_identity(p_collider) == p_support) {
        return int(NetwPredict::CONTACT_CLASS_DECLARED_SUPPORT);
    }
    if (p_collider->is_class("AnimatableBody2D")
        || p_collider->is_class("AnimatableBody3D")
        || p_collider->is_class("CharacterBody2D")
        || p_collider->is_class("CharacterBody3D")) {
        return int(NetwPredict::CONTACT_CLASS_KINEMATIC_PROXY);
    }
    if (static_geometry(p_collider)) {
        return int(NetwPredict::CONTACT_CLASS_OTHER_STATIC);
    }
    const bool rigid = p_collider->is_class("RigidBody2D")
        || p_collider->is_class("RigidBody3D");
    if (rigid && bool(p_collider->get(StringName("freeze")))) {
        return int(NetwPredict::CONTACT_CLASS_KINEMATIC_PROXY);
    }
    const Ref<NetwEntity> entity = contact_entity(p_collider);
    if (entity.is_valid() && slot_of(entity) >= 0) {
        return int(NetwPredict::CONTACT_CLASS_PREDICTED_DYNAMIC);
    }
    return int(NetwPredict::CONTACT_CLASS_UNPREDICTED_DYNAMIC);
}

Dictionary NetwPredictionEngine::open_simulated_state(
    int64_t p_slot,
    const Dictionary &p_payload,
    int64_t p_recv_tick,
    int64_t p_current_tick
) {
    Dictionary opened;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        opened[StringName("target")] = p_payload;
        opened[StringName("divergence")] = 0.0;
        return opened;
    }
    const int64_t age = std::clamp(
        std::max(int64_t(0), p_current_tick - p_recv_tick),
        int64_t(0),
        int64_t(row->config.max_restore_ticks)
    );
    Dictionary target = p_payload;
    if (row->config.restore == int(RestoreMode::EXTRAPOLATED)) {
        target = project_state(
            p_slot,
            p_payload,
            double(age) * row->cursor.tick_delta
        );
    }
    Dictionary by_field;
    const double divergence = NetwPredictionHandle::divergence_by_field(
        canonicalize_state(p_slot, capture_state(p_slot)),
        target,
        by_field,
        angle_fields_of(p_slot)
    );
    note_divergence(p_slot, by_field);
    note_reconciling(p_slot, true);
    opened[StringName("target")] = target;
    opened[StringName("divergence")] = divergence;
    return opened;
}

Array NetwPredictionEngine::input_keys(int64_t p_slot) const {
    Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->input_binding.is_null()) {
        return out;
    }
    const Ref<NetwPropertySet> declared = row->input_binding->get_set();
    if (declared.is_null()) {
        return out;
    }
    for (int at = 0; at < declared->columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> field = declared->columns[at];
        if (field.is_valid()
            && field->get_lane() == int(NetwPropertySet::VOLATILE)) {
            out.push_back(field->get_key());
        }
    }
    return out;
}

const SchemaRecord *NetwPredictionEngine::input_schema(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->input_binding.is_null()
        || row->input_binding->get_set().is_null()) {
        return nullptr;
    }
    return &row->input_binding->get_set()->get_volatile_schema();
}

bool NetwPredictionEngine::decode_command_frame(
    int64_t p_slot,
    const PackedByteArray &p_payload,
    predict::CommandFrameRecord &r_frame
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->input_binding.is_null()
        || row->input_binding->get_set().is_null()) {
        return false;
    }
    return predict::CommandFrameRecord::decode(
        row->input_binding->get_set()->get_volatile_schema(),
        p_payload,
        r_frame
    );
}

PackedInt64Array NetwPredictionEngine::admit_relayed_frame(
    int64_t p_slot,
    const predict::CommandFrameRecord &p_frame,
    const Array &p_keys,
    bool p_reconcile_joint
) {
    PackedInt64Array counts = gd::zeroed<PackedInt64Array>(RELAY_COLUMN_COUNT);
    if (row_of(p_slot) == nullptr) {
        return counts;
    }
    int64_t *tally = counts.ptrw();
    const int64_t epoch = p_frame.epoch();
    bool bumped = false;
    if (epoch != relayed_epoch_of(p_slot)) {
        bumped = relayed_epoch_of(p_slot) >= 0;
        set_relayed_epoch(p_slot, epoch);
        joint_clear(p_slot);
        set_newest_matrix_transition(p_slot, -1);
    }
    int fresh_index = 0;
    for (int at = 0; at < p_frame.transition_count(); ++at) {
        const predict::TransitionWire &transition = p_frame.transition_at(at);
        const int64_t index = transition.index;
        if (!transition.fresh) {
            continue;
        }
        const Array values = p_frame.payload_at(fresh_index);
        fresh_index += 1;
        const int64_t floor = newest_matrix_transition_of(p_slot)
            - predict::TAPE_HISTORY_LIMIT + 1;
        if (index < floor) {
            tally[RELAY_DROPPED_LATE] += 1;
            continue;
        }
        const int standing = joint_provenance_at(p_slot, index);
        if (standing == int(NetwPredict::CELL_PROVENANCE_RELAYED)) {
            continue;
        }
        Dictionary command;
        const int carried = std::min(p_keys.size(), values.size());
        for (int position = 0; position < carried; ++position) {
            command[p_keys[position]] = values[position];
        }
        const bool displaced
            = standing == int(NetwPredict::CELL_PROVENANCE_SUBSTITUTED);
        set_newest_matrix_transition(
            p_slot,
            std::max(newest_matrix_transition_of(p_slot), index)
        );
        if (index >= 0) {
            joint_record(p_slot, index, Array(), command, false, true, false);
        }
        tally[RELAY_RECORDED] += 1;
        if (!p_reconcile_joint) {
            continue;
        }
        if (bumped) {
            bumped = false;
            set_joint_epoch_floor(
                p_slot,
                std::max(joint_epoch_floor_of(p_slot), index)
            );
            joint_note_basis(p_slot, index, JOINT_FLOOR_EPOCH);
        }
        if (displaced) {
            const int64_t floored = index - 1;
            const int64_t held = joint_relay_floor_of(p_slot);
            set_joint_relay_floor(
                p_slot,
                held < 0 ? floored : std::min(held, floored)
            );
            joint_note_basis(p_slot, floored, JOINT_FLOOR_RELAY);
        }
    }
    return counts;
}

PackedByteArray NetwPredictionEngine::build_command_frame_for(int64_t p_slot) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return PackedByteArray();
    }
    const bool authors_commands = row->config.role == int(Role::PREDICT)
        || (row->config.role == int(Role::REMOTE)
            && row->latches.fallback_latched);
    if (!authors_commands || row->input_binding.is_null()
        || row->input_binding->get_set().is_null()) {
        return PackedByteArray();
    }
    return build_command_frame(
        p_slot,
        row->input_binding->get_set()->get_volatile_schema(),
        input_keys(p_slot),
        int(MAX(int64_t(1), row->input_binding->get_set()->get_window() + 2))
    );
}

PackedInt64Array NetwPredictionEngine::ack_frontier(
    int64_t p_slot,
    int64_t p_ack
) const {
    PackedInt64Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->config.role != int(Role::CONSUME) || p_ack < 0) {
        return out;
    }
    const int64_t closed = journal_last_closed(p_slot);
    out.push_back(closed);
    out.push_back(MIN(p_ack, closed));
    return out;
}

int64_t NetwPredictionEngine::lane_route(int64_t p_slot) const {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (host == nullptr || seated.is_null()) {
        return -1;
    }
    return host->liveness_route_of(seated.ptr());
}

void NetwPredictionEngine::send_lane(
    int64_t p_peer,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_bytes
) {
    NetwMultiplayer *host = core();
    if (host == nullptr || p_bytes.is_empty()) {
        return;
    }
    host->send_to(
        p_peer,
        p_route,
        p_channel,
        p_bytes,
        false,
        0,
        String(),
        true
    );
}

Ref<NetwPredictStats> NetwPredictionEngine::stats_of(int64_t p_slot) const {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return Ref<NetwPredictStats>();
    }
    return handle->get_stats();
}

int64_t NetwPredictionEngine::held_fact_of(int64_t p_slot, int p_fact) const {
    const Ref<NetwPredictStats> counters = stats_of(p_slot);
    return counters.is_valid() ? counters->get_int_fact(p_fact) : 0;
}

void NetwPredictionEngine::set_held_fact(
    int64_t p_slot,
    int p_fact,
    int64_t p_value
) {
    const Ref<NetwPredictStats> counters = stats_of(p_slot);
    if (counters.is_valid()) {
        counters->set_int_fact(p_fact, p_value);
    }
}

void NetwPredictionEngine::add_held_fact(
    int64_t p_slot,
    int p_fact,
    int64_t p_delta
) {
    const Ref<NetwPredictStats> counters = stats_of(p_slot);
    if (counters.is_valid()) {
        counters->set_int_fact(
            p_fact,
            counters->get_int_fact(p_fact) + p_delta
        );
    }
}

PackedByteArray NetwPredictionEngine::publish_ack_frame(int64_t p_slot) {
    if (core() == nullptr) {
        return PackedByteArray();
    }
    const PackedInt64Array frontier = ack_frontier(p_slot, ack_of(p_slot));
    if (frontier.is_empty()) {
        return PackedByteArray();
    }
    set_held_fact(p_slot, NetwPredictStats::FACT_JOURNAL_CLOSED, frontier[0]);
    set_held_fact(p_slot, NetwPredictStats::FACT_ACK_FRONTIER, frontier[1]);
    if (frontier[1] < 0) {
        return PackedByteArray();
    }
    return build_ack_frame(
        p_slot,
        int(command_epoch_of(p_slot)),
        ack_of(p_slot),
        owner_ack_floor_of(p_slot)
    );
}

void NetwPredictionEngine::send_command_frame(int64_t p_slot) {
    const int64_t route = lane_route(p_slot);
    if (route < 0) {
        return;
    }
    const PackedByteArray bytes = build_command_frame_for(p_slot);
    if (!bytes.is_empty()) {
        add_held_fact(p_slot, NetwPredictStats::FACT_COMMAND_FRAMES_SENT, 1);
    }
    send_lane(1, route, wire::builtin_channel("PREDICT_COMMAND"), bytes);
}

void NetwPredictionEngine::send_ack_frame(int64_t p_slot) {
    const int64_t route = lane_route(p_slot);
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (route < 0 || seated.is_null()) {
        return;
    }
    send_lane(
        seated->get_controller(),
        route,
        wire::builtin_channel("PREDICT_ACK"),
        publish_ack_frame(p_slot)
    );
}

void NetwPredictionEngine::admit_relayed_payload(
    int64_t p_slot,
    const PackedByteArray &p_payload
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    const PackedInt64Array counts = receive_relayed_command_frame(
        p_slot,
        p_payload,
        handle != nullptr
            && handle->get_reconcile_mode() == NetwPredict::RECONCILE_JOINT
    );
    if (counts.is_empty()) {
        add_held_fact(p_slot, NetwPredictStats::FACT_FRAMES_DROPPED_INVALID, 1);
        return;
    }
    add_held_fact(
        p_slot,
        NetwPredictStats::FACT_RELAYED_DROPPED_LATE,
        counts[RELAY_DROPPED_LATE]
    );
    add_held_fact(
        p_slot,
        NetwPredictStats::FACT_RELAYED_RECORDED,
        counts[RELAY_RECORDED]
    );
}

void NetwPredictionEngine::declare_skipped_run(
    int64_t p_slot,
    int64_t p_from,
    int64_t p_until
) {
    if (core() == nullptr) {
        return;
    }
    for (int64_t transition = p_from; transition < p_until; ++transition) {
        declare_skipped(
            p_slot,
            transition,
            command_label_of(p_slot, transition)
        );
    }
}

int64_t NetwPredictionEngine::queued_span(int64_t p_slot) const {
    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    if (lane.is_null()) {
        return 0;
    }
    return lane->newest_input_tick() - next_input_tick_of(p_slot) + 1;
}

void NetwPredictionEngine::resync_input_if_stranded(
    int64_t p_slot,
    int64_t p_ceiling,
    int64_t p_buffer
) {
    if (p_ceiling <= 0 || queued_span(p_slot) <= p_ceiling) {
        return;
    }
    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    if (lane.is_null()) {
        return;
    }
    const int64_t standing = next_input_tick_of(p_slot);
    const int64_t target = lane->newest_input_tick() - p_buffer;
    if (target <= standing) {
        return;
    }
    if (core() != nullptr) {
        for (int64_t transition = MAX(standing, target - ACK_WINDOW_MAX);
             transition < target;
             ++transition) {
            declare_skipped(p_slot, transition, transition);
        }
    }
    add_held_fact(
        p_slot,
        NetwPredictStats::FACT_SKIPPED,
        MAX(int64_t(0), target - standing)
    );
    add_held_fact(p_slot, NetwPredictStats::FACT_RESYNC, 1);
    set_next_input_tick(p_slot, target);
}

void NetwPredictionEngine::resync_tape_if_stranded(
    int64_t p_slot,
    int64_t p_depth,
    int64_t p_buffer,
    int64_t p_ceiling
) {
    if (p_ceiling <= 0 || p_depth <= p_buffer + p_ceiling) {
        return;
    }
    const int64_t standing = replay_cursor_of(p_slot);
    const int64_t target = standing + p_depth - p_buffer - 1;
    if (target <= standing) {
        return;
    }
    declare_skipped_run(p_slot, standing, target);
    add_held_fact(p_slot, NetwPredictStats::FACT_SKIPPED, target - standing);
    add_held_fact(p_slot, NetwPredictStats::FACT_RESYNC, 1);
    set_replay_cursor(p_slot, target);
}

void NetwPredictionEngine::report_owner_claim(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_fingerprint
) {
    const PackedInt64Array verdict
        = judge_owner_claim(p_slot, p_transition, p_fingerprint);
    if (verdict[CLAIM_JUDGED] == 0) {
        return;
    }
    add_held_fact(p_slot, NetwPredictStats::FACT_CLIENT_FP_VERIFIED, 1);
    if (verdict[CLAIM_MATCHED] != 0) {
        return;
    }
    add_held_fact(p_slot, NetwPredictStats::FACT_CLIENT_MISMATCHES, 1);
    const int64_t attribution = verdict[CLAIM_ATTRIBUTION];
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (attribution < 0 || host == nullptr || seated.is_null()) {
        return;
    }
    host->emit_signal(
        StringName("predict_owner_divergence"),
        seated->get_controller(),
        p_transition,
        attribution
    );
}

Dictionary NetwPredictionEngine::capture_state_raw(int64_t p_slot) {
    if (core() != nullptr && owner_bound(p_slot)) {
        return capture_state(p_slot);
    }
    const Ref<NetwPropertySetBinding> binding = state_binding_of(p_slot);
    return binding.is_valid() ? binding->snapshot_payload() : Dictionary();
}

Dictionary NetwPredictionEngine::capture_input_raw(int64_t p_slot) {
    if (core() != nullptr && owner_bound(p_slot)) {
        return capture_input(p_slot);
    }
    const Ref<NetwPropertySetBinding> binding = input_binding_of(p_slot);
    return binding.is_valid() ? binding->snapshot_payload() : Dictionary();
}

void NetwPredictionEngine::apply_input_raw(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    if (core() != nullptr && owner_bound(p_slot)) {
        apply_input(p_slot, p_payload);
        return;
    }
    const Ref<NetwPropertySetBinding> binding = input_binding_of(p_slot);
    if (binding.is_valid()) {
        binding->apply_payload(p_payload);
    }
}

Dictionary NetwPredictionEngine::canonical_state(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    if (core() != nullptr) {
        return p_slot >= 0 ? canonicalize_state(p_slot, p_payload)
                           : p_payload.duplicate();
    }
    const Ref<NetwPropertySetBinding> binding = state_binding_of(p_slot);
    return binding.is_valid() ? binding->canonicalize_payload(p_payload)
                              : Dictionary();
}

Dictionary NetwPredictionEngine::canonical_input(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    if (core() != nullptr) {
        return p_slot >= 0 ? canonicalize_input(p_slot, p_payload)
                           : p_payload.duplicate();
    }
    const Ref<NetwPropertySetBinding> binding = input_binding_of(p_slot);
    return binding.is_valid() ? binding->canonicalize_payload(p_payload)
                              : Dictionary();
}

PackedByteArray NetwPredictionEngine::input_bytes(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    if (core() != nullptr) {
        return p_slot >= 0 ? canonical_input_bytes(p_slot, p_payload)
                           : PackedByteArray();
    }
    const Ref<NetwPropertySetBinding> binding = input_binding_of(p_slot);
    return binding.is_valid() ? binding->canonical_bytes(p_payload)
                              : PackedByteArray();
}

Dictionary NetwPredictionEngine::capture_canonical_state(int64_t p_slot) {
    NETW_ZONE_NC("predict capture canonical state", colors::PREDICTION);
    return canonical_state(p_slot, capture_state_raw(p_slot));
}

void NetwPredictionEngine::record_input_bytes(
    int64_t p_slot,
    int64_t p_tick,
    const Dictionary &p_input
) {
    if (core() == nullptr) {
        return;
    }
    record_input(
        p_slot,
        p_tick,
        NetwPredictJournal::fnv1a(input_bytes(p_slot, p_input))
    );
}

void NetwPredictionEngine::run_replay_step(
    int64_t p_slot,
    const Dictionary &p_input,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    if (core() != nullptr && owner_bound(p_slot)) {
        run_step(p_slot, p_input, p_delta, p_tick, p_fresh);
        return;
    }
    apply_input_raw(p_slot, p_input);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    const Callable step = handle->get_simulate();
    if (step.is_valid()) {
        step.call(p_delta, p_tick, p_fresh);
    }
}

bool NetwPredictionEngine::replay_tape_entry(
    int64_t p_slot,
    double p_delta,
    int64_t p_tick
) {
    const int64_t cursor = replay_cursor_of(p_slot);
    if (core() == nullptr || !command_has(p_slot, cursor)) {
        return false;
    }
    const int64_t label = command_label_of(p_slot, cursor);
    const bool fresh = command_is_fresh(p_slot, cursor);
    Dictionary input = last_input_of(p_slot);
    int kind = int(DriveKind::REPEAT);
    bool applied_fresh = false;
    if (fresh) {
        const Dictionary command = command_for(p_slot, cursor, label);
        if (!command.is_empty()) {
            input = command;
            set_last_input(p_slot, input);
            kind = int(DriveKind::FRESH);
            applied_fresh = true;
        } else {
            add_held_fact(p_slot, NetwPredictStats::FACT_MISSING, 1);
            kind = int(DriveKind::MISSING);
        }
    }
    if (input.is_empty()) {
        input = stall_input_of(p_slot);
    }
    record_drive(p_slot, cursor, label, kind, input, p_tick, true);
    run_replay_step(p_slot, input, p_delta, label, applied_fresh);
    set_ack(p_slot, cursor);
    set_replay_cursor(p_slot, cursor + 1);
    set_last_replayed_label(p_slot, label);
    set_last_replayed_fresh(p_slot, fresh);
    add_held_fact(p_slot, NetwPredictStats::FACT_CONSUMED, 1);
    return true;
}

Dictionary NetwPredictionEngine::command_for(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label
) const {
    if (core() != nullptr && command_has(p_slot, p_transition)) {
        return command_payload_of(p_slot, p_transition);
    }
    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    if (lane.is_null() || !lane->has_input_at(p_label)) {
        return Dictionary();
    }
    return lane->input_at(p_label);
}

void NetwPredictionEngine::prepare_tick_tape(int64_t p_slot, int64_t p_tick) {
    if (core() == nullptr) {
        return;
    }
    const PackedInt64Array span = tape_span(p_slot);
    const int64_t newest = span.size() > 1 ? span[1] : -1;
    tape_prepare_tick(p_slot, p_tick);
    if (newest >= 0 && newest != p_tick - 1) {
        set_tape_epoch(p_slot, (tape_epoch_of(p_slot) + 1) & 0xFF);
        clear_witness_details(p_slot);
        drop_deferred_operator(p_slot);
        set_ack_of_acks(p_slot, -1);
        set_ack_domain_confirmed(p_slot, false);
    }
    set_next_tape_entry_index(p_slot, p_tick);
}

namespace {

String refusal_row(
    const Ref<NetwEntity> &p_member,
    const NetwPredictionHandle *p_handle,
    const char *p_suffix
) {
    const String tier = p_handle != nullptr
        ? NetwPredict::schedule_name(p_handle->get_schedule())
        : String("unknown");
    const StringName named
        = p_member.is_valid() ? p_member->get_entity_id() : StringName();
    return String(named) + String(p_suffix) + tier + String(" tier");
}

} // namespace

void NetwPredictionEngine::refresh_simulation_gate(
    int64_t p_slot,
    bool p_force_release
) {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (host == nullptr || seated.is_null()) {
        return;
    }
    const bool solver = handle != nullptr
        && handle->get_archetype() == int(NetwPredict::ARCHETYPE_SOLVER_BODY);
    host->simulation_gate_set(
        seated->get_rid_handle(),
        !p_force_release && solver
    );
}

void NetwPredictionEngine::host_local_step(
    int64_t p_slot,
    double p_delta,
    int64_t p_tick
) {
    const Dictionary input = capture_input_raw(p_slot);
    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    if (lane.is_valid()) {
        lane->record_input(p_tick, input);
    }
    record_drive(
        p_slot,
        p_tick,
        p_tick,
        int(DriveKind::FRESH),
        input,
        p_tick,
        false
    );
    run_replay_step(p_slot, input, p_delta, p_tick, true);
    const Ref<NetwPropertySetBinding> binding = state_binding_of(p_slot);
    if (binding.is_valid()) {
        binding->set_authored_tick(p_tick);
        binding->set_reconcile_ack(p_tick);
    }
}

int64_t NetwPredictionEngine::sibling_slot(const Ref<NetwEntity> &p_member) {
    NetwMultiplayer *host = core();
    if (host == nullptr || p_member.is_null()) {
        return -1;
    }
    return host->predict_engine_seated(p_member->get_rid_handle())
        ? slot_of(p_member)
        : -1;
}

int NetwPredictionEngine::admitted_reconcile_mode(int64_t p_slot) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    const Ref<NetwPredictIsland> island
        = handle != nullptr ? handle->get_island() : Ref<NetwPredictIsland>();
    const int requested = island.is_valid()
        ? island->get_reconcile()
        : int(NetwPredict::RECONCILE_INDEPENDENT);
    if (requested != int(NetwPredict::RECONCILE_JOINT)) {
        return requested;
    }

    PackedStringArray refusals;
    if (!slot_is_steppable(p_slot)) {
        refusals.push_back(
            refusal_row(owner_entity(p_slot), handle, " owner on a ")
        );
    }
    const TypedArray<NetwEntity> members
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < members.size(); ++at) {
        const Ref<NetwEntity> member = members[at];
        if (member.is_null()) {
            continue;
        }
        const int64_t seat = sibling_slot(member);
        if (seat < 0 || slot_is_steppable(seat)) {
            continue;
        }
        refusals.push_back(refusal_row(
            member,
            Object::cast_to<NetwPredictionHandle>(handle_of(seat)),
            " on a "
        ));
    }
    if (refusals.is_empty()) {
        return int(NetwPredict::RECONCILE_JOINT);
    }
    if (!joint_refusal_reported_of(p_slot)) {
        set_joint_refusal_reported(p_slot, true);
        NETW_ERROR(
            sys::PREDICTION,
            "NetwPredictIsland.reconcile: JOINT admitted for none of this "
            "island's members (%s), so it runs INDEPENDENT.",
            String(", ").join(refusals)
        );
    }
    return int(NetwPredict::RECONCILE_INDEPENDENT);
}

void NetwPredictionEngine::follow_relay_subscription(
    int64_t p_slot,
    int p_mode
) {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (host == nullptr || seated.is_null()) {
        return;
    }
    host->predict_relay_subscribe(
        seated->get_rid_handle(),
        p_mode == int(NetwPredict::RECONCILE_JOINT)
    );
}

void NetwPredictionEngine::admit_reconcile_mode(int64_t p_slot) {
    const int admitted = admitted_reconcile_mode(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle != nullptr) {
        handle->set_reconcile_mode(admitted);
    }
    const TypedArray<NetwEntity> members
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < members.size(); ++at) {
        const Ref<NetwEntity> member = members[at];
        if (member.is_null()) {
            continue;
        }
        const int64_t seat = sibling_slot(member);
        if (seat < 0) {
            continue;
        }
        NetwPredictionHandle *held
            = Object::cast_to<NetwPredictionHandle>(handle_of(seat));
        if (held == nullptr || held->get_reconcile_mode() == admitted) {
            continue;
        }
        held->set_reconcile_mode(admitted);
        follow_relay_subscription(seat, admitted);
    }
}

bool NetwPredictionEngine::slot_has_stepper(int64_t p_slot) const {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (host == nullptr || seated.is_null()) {
        return false;
    }
    return host->predict_get_stepper(host->entity_space_of(seated).space)
        .is_valid();
}

int NetwPredictionEngine::resolved_schedule(int64_t p_slot, int p_declared) {
    if (p_declared != int(NetwPredict::SCHEDULE_STEPPED)) {
        return p_declared;
    }
    if (slot_has_stepper(p_slot)) {
        set_stepper_absence_reported(p_slot, false);
        return p_declared;
    }
    if (!stepper_absence_reported_of(p_slot)) {
        set_stepper_absence_reported(p_slot, true);
        NetwMultiplayer *host = core();
        const Ref<NetwEntity> seated = owner_entity(p_slot);
        const RID space = host != nullptr && seated.is_valid()
            ? host->entity_space_of(seated).space
            : RID();
        NETW_ERROR(
            sys::PREDICTION,
            "NetwPredict.SCHEDULE_STEPPED: %s stands in space %d, which has "
            "no installed stepper, so it runs SCHEDULE_FRAME. Install one "
            "with NetwMultiplayer.predict_stepper_install, or declare a "
            "schedule a stock engine can drive.",
            seated.is_valid() ? String(seated->get_entity_id())
                              : String("an entity"),
            int64_t(space.get_id())
        );
    }
    return int(NetwPredict::SCHEDULE_FRAME);
}

bool NetwPredictionEngine::slot_is_steppable(int64_t p_slot) const {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr
        || handle->get_sim_mode() == NetwPredict::SIM_MODE_DISPLAY
        || !handle->get_simulate().is_valid()) {
        return false;
    }
    if (handle->get_schedule() == NetwPredict::SCHEDULE_TICK) {
        return true;
    }
    return handle->get_schedule() == NetwPredict::SCHEDULE_STEPPED
        && slot_has_stepper(p_slot);
}

void NetwPredictionEngine::refresh_tape_diagnostics(int64_t p_slot) {
    const bool live = core() != nullptr;
    const PackedInt64Array held
        = live ? command_transitions(p_slot) : PackedInt64Array();
    set_held_fact(
        p_slot,
        NetwPredictStats::FACT_TAPE_EPOCH,
        command_epoch_of(p_slot)
    );
    set_held_fact(
        p_slot,
        NetwPredictStats::FACT_TAPE_QUEUE_DEPTH,
        live ? command_depth_from(p_slot, replay_cursor_of(p_slot)) : 0
    );
    set_held_fact(
        p_slot,
        NetwPredictStats::FACT_TAPE_INDEX,
        held.is_empty() ? -1 : held[held.size() - 1]
    );
}

void NetwPredictionEngine::admit_command_payload(
    int64_t p_slot,
    const PackedByteArray &p_payload
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->config.role != int(Role::CONSUME)) {
        return;
    }
    add_held_fact(p_slot, NetwPredictStats::FACT_COMMAND_FRAMES_RECEIVED, 1);
    const int64_t held = receive_command_frame(p_slot, p_payload);
    if (held < 0) {
        add_held_fact(p_slot, NetwPredictStats::FACT_FRAMES_DROPPED_INVALID, 1);
        return;
    }
    set_held_fact(p_slot, NetwPredictStats::FACT_COMMAND_QUEUE_DEPTH, held);
    refresh_tape_diagnostics(p_slot);
}

int64_t NetwPredictionEngine::refresh_owner_ack_age(int64_t p_slot) {
    if (core() == nullptr || p_slot < 0) {
        return -1;
    }
    refresh_ack_age(p_slot);
    const PackedInt64Array columns = drive_stats(p_slot);
    const int64_t age = STAT_ACK_AGE_TICKS < columns.size()
        ? columns[STAT_ACK_AGE_TICKS]
        : -1;
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (age >= 0 && handle != nullptr) {
        handle->set_ack_age_ticks(int(age));
    }
    return age;
}

bool NetwPredictionEngine::speculation_horizon_full(int64_t p_slot) {
    refresh_owner_ack_age(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    return handle != nullptr
        && handle->get_ack_age_ticks() >= NetwPredict::ACK_AGE_MAX;
}

PackedByteArray NetwPredictionEngine::build_command_frame(
    int64_t p_slot,
    const SchemaRecord &p_schema,
    const Array &p_keys,
    int p_window
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return PackedByteArray();
    }
    const PackedInt64Array span = tape_span(p_slot);
    const int64_t newest = span[1];
    if (newest < 0) {
        return PackedByteArray();
    }
    predict::CommandFrameRecord frame;
    if (!predict::CommandFrameRecord::open(p_schema, frame)) {
        return PackedByteArray();
    }
    const int64_t floor = ack_of_acks_of(p_slot);
    frame.set_epoch(int(tape_epoch_of(p_slot)));
    frame.set_ack_of_acks(floor);

    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    const Dictionary stall = stall_input_of(p_slot);
    PackedInt64Array rows;
    const int64_t oldest = std::max(
        std::max(span[0], floor + 1),
        newest - std::max(1, p_window) + 1
    );
    for (int64_t index = oldest; index <= newest; ++index) {
        const int64_t label = tape_label_of(p_slot, index);
        const bool fresh = tape_is_fresh(p_slot, index);
        if (!frame.append_transition(index, label, fresh)) {
            break;
        }
        rows.append(index);
        if (!fresh) {
            continue;
        }
        const Dictionary input
            = lane.is_valid() ? lane->input_at(label) : Dictionary();
        Array values;
        for (int at = 0; at < p_keys.size(); ++at) {
            const StringName key = p_keys[at];
            values.append(input.get(key, stall.get(key, Variant())));
        }
        frame.append_payload(values);
    }
    if (rows.is_empty()) {
        return PackedByteArray();
    }
    const int64_t closed = journal_last_closed(p_slot);
    for (int at = 0; at < rows.size(); ++at) {
        const int64_t index = rows[at];
        if (index > closed) {
            break;
        }
        const int seat = journal_slot_of(p_slot, index);
        if (seat < 0) {
            break;
        }
        const predict::JournalRow held = journal_row(p_slot, index);
        if (!held.present) {
            break;
        }
        PackedInt32Array pre_family;
        pre_family.push_back(held.pre_families.pose);
        pre_family.push_back(held.pre_families.momentum);
        pre_family.push_back(held.pre_families.controller);
        PackedInt32Array post_family;
        post_family.push_back(held.post_families.pose);
        post_family.push_back(held.post_families.momentum);
        post_family.push_back(held.post_families.controller);
        frame.append_evidence(
            held.evidence_mask,
            held.pre_fp,
            held.post_fp,
            held.e_digest,
            held.topo_fp,
            held.witness_fp,
            pre_family,
            post_family,
            held.raw_fp
        );
    }
    return frame.to_bytes();
}

PackedInt64Array NetwPredictionEngine::admit_command_frame(
    int64_t p_slot,
    const predict::CommandFrameRecord &p_frame,
    const Array &p_keys
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return PackedInt64Array();
    }
    const int64_t epoch = p_frame.epoch();
    if (epoch != command_epoch_of(p_slot)) {
        set_command_epoch(p_slot, epoch);
        tape_reset(p_slot, epoch);
        clear_owner_claims(p_slot);
        clear_witness_details(p_slot);
        drop_deferred_operator(p_slot);
        set_owner_ack_floor(p_slot, -1);
        set_replay_cursor(p_slot, -1);
        set_last_replayed_label(p_slot, -1);
        set_last_replayed_fresh(p_slot, false);
        set_ack(p_slot, -1);
        set_last_input(p_slot, Dictionary());
    }
    set_owner_ack_floor(
        p_slot,
        std::max(owner_ack_floor_of(p_slot), p_frame.ack_of_acks())
    );

    record_owner_claims(p_slot, p_frame);

    const bool ticked = row->config.schedule == int(Schedule::TICK);
    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    int fresh_seen = 0;
    for (int at = 0; at < p_frame.transition_count(); ++at) {
        const predict::TransitionWire &transition = p_frame.transition_at(at);
        const int64_t index = transition.index;
        if (index < 0) {
            continue;
        }
        const bool fresh = transition.fresh;
        const int64_t label = transition.label;
        Dictionary command;
        if (fresh) {
            if (fresh_seen >= p_frame.payload_count()) {
                break;
            }
            const Array values = p_frame.payload_at(fresh_seen);
            fresh_seen += 1;
            const int carried = std::min(p_keys.size(), values.size());
            for (int i = 0; i < carried; ++i) {
                command[p_keys[i]] = values[i];
            }
        }
        if (!command_admit(p_slot, index, label, fresh, command)) {
            continue;
        }
        set_arrivals_this_frame(p_slot, arrivals_this_frame_of(p_slot) + 1);
        if (ticked && fresh && label >= 0 && lane.is_valid()) {
            lane->record_input(label, command);
            if (next_input_tick_of(p_slot) < 0) {
                set_next_input_tick(p_slot, label);
            }
        }
    }
    const PackedInt64Array held = command_transitions(p_slot);
    if (replay_cursor_of(p_slot) < 0 && !held.is_empty()) {
        set_replay_cursor(p_slot, held[0]);
    }
    return held;
}

int64_t NetwPredictionEngine::receive_command_frame(
    int64_t p_slot,
    const PackedByteArray &p_payload
) {
    predict::CommandFrameRecord frame;
    if (!decode_command_frame(p_slot, p_payload, frame)) {
        return -1;
    }
    return admit_command_frame(p_slot, frame, input_keys(p_slot)).size();
}

PackedInt64Array NetwPredictionEngine::receive_relayed_command_frame(
    int64_t p_slot,
    const PackedByteArray &p_payload,
    bool p_reconcile_joint
) {
    predict::CommandFrameRecord frame;
    if (!decode_command_frame(p_slot, p_payload, frame)) {
        return PackedInt64Array();
    }
    return admit_relayed_frame(
        p_slot,
        frame,
        input_keys(p_slot),
        p_reconcile_joint
    );
}

int64_t NetwPredictionEngine::drive_frontier(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return -1;
    }
    return row->config.schedule == int(Schedule::FRAME)
        ? row->lane_cursor.last_driven_entry_index
        : row->cursor.latest_input_tick;
}

namespace {

bool delta_negligible(const Variant &p_delta) {
    switch (p_delta.get_type()) {
        case Variant::FLOAT:
            return std::abs(double(p_delta)) < 0.000001;
        case Variant::VECTOR2:
            return Vector2(p_delta).length() < 0.000001;
        case Variant::VECTOR3:
            return Vector3(p_delta).length() < 0.000001;
        default:
            return true;
    }
}

} // namespace

void NetwPredictionEngine::note_joint_basis(
    int64_t p_slot,
    int64_t p_basis,
    const Dictionary &p_payload,
    int p_source
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    set_joint_basis(p_slot, MAX(joint_basis_of(p_slot), p_basis));
    if (handle->get_schedule() == NetwPredict::SCHEDULE_TICK) {
        const Ref<NetwTimeline> history = timeline_of(p_slot);
        if (history.is_valid()) {
            history->record_state(p_basis + 1, p_payload);
        }
        mark_carry_dirty(p_slot, p_basis + 1);
    }
    if (p_basis >= 0 && island_of(p_slot) != ISLAND_NONE) {
        joint_note_basis(p_slot, p_basis, p_source);
    }
}

void NetwPredictionEngine::admit_simulated_state(
    int64_t p_slot,
    const Dictionary &p_header
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    if (!stream_reconstructed_of(p_slot)) {
        set_stream_reconstructed(
            p_slot,
            bool(p_header.get(StringName("whole"), true))
        );
    }
    if (!stream_reconstructed_of(p_slot)) {
        return;
    }
    const Dictionary payload
        = p_header.get(StringName("payload"), Dictionary());
    if (payload.is_empty()) {
        return;
    }
    const int64_t recv_tick = p_header.get(StringName("tick"), -1);
    const int64_t now = current_tick();
    const Dictionary opened = open_simulated_state(
        p_slot,
        payload,
        recv_tick,
        now >= 0 ? now : recv_tick
    );
    const Ref<NetwPredictStats> counters = handle->get_stats();
    if (counters.is_valid()) {
        counters->set(
            StringName("corrections"),
            int64_t(counters->get(StringName("corrections"))) + 1
        );
    }
    if (handle->get_reconcile_mode() == NetwPredict::RECONCILE_JOINT) {
        note_joint_basis(p_slot, recv_tick - 1, payload, JOINT_FLOOR_STATE);
    } else {
        const Ref<NetwEntity> seated = owner_entity(p_slot);
        if (apply_restore(
                p_slot,
                opened.get(StringName("target"), Dictionary()),
                int(predict::Operator::NONE),
                -1,
                p_slot,
                seated.is_valid() ? seated->get_entity_id() : StringName(),
                handle->get_ack_age_ticks(),
                handle->get_divergence_epsilon(),
                false,
                false
            )) {
        }
    }
    note_reconciling(p_slot, false);
    note_verdict_reason(p_slot, NetwPredict::VERDICT_REASON_NONE);
    handle->emit_signal(
        StringName("state_evaluated"),
        recv_tick,
        int64_t(-1),
        double(opened.get(StringName("divergence"), 0.0)),
        true
    );
}

int NetwPredictionEngine::replay_reseed_horizon(
    int64_t p_slot,
    int64_t p_ack,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return 0;
    }
    if (handle->get_schedule() == NetwPredict::SCHEDULE_FRAME) {
        return replay_authored_entries(
            p_slot,
            p_ack,
            p_sample,
            p_participants,
            p_demote
        );
    }
    const predict::Slot *row = row_of(p_slot);
    const Ref<NetwTimeline> history = timeline_of(p_slot);
    if (row == nullptr || history.is_null()
        || input_binding_of(p_slot).is_null()) {
        return 0;
    }
    const Array window
        = history->inputs_in_range(p_ack + 1, latest_input_tick_of(p_slot));
    const Dictionary live = capture_input(p_slot);
    const double delta = row->cursor.tick_delta;
    for (int at = 0; at < window.size(); ++at) {
        const Dictionary entry = window[at];
        const int64_t tick = entry.get(StringName("tick"), -1);
        run_step(
            p_slot,
            entry.get(StringName("input"), Dictionary()),
            delta,
            tick,
            false
        );
        history->record_state(tick + 1, capture_current(p_slot));
    }
    apply_input(p_slot, live);
    return window.size();
}

void NetwPredictionEngine::finish_reseed_alignment(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_ack,
    const Dictionary &p_payload,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return;
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (apply_restore(
            p_slot,
            advanced_seed(
                p_slot,
                p_payload,
                p_ack,
                handle->get_snap_restore(),
                handle->get_max_restore_ticks(),
                handle->get_teleport_threshold(),
                handle->get_divergence_epsilon()
            ),
            int(predict::Operator::RESEED_ALIGN),
            p_ack,
            p_slot,
            seated.is_valid() ? seated->get_entity_id() : StringName(),
            handle->get_ack_age_ticks(),
            handle->get_divergence_epsilon(),
            true,
            false
        )) {
        sync_episode(p_slot);
    }
    const Ref<NetwTimeline> history = timeline_of(p_slot);
    if (history.is_valid()) {
        history->record_state(p_ack + 1, p_payload);
    }
    row = mutable_row_of(p_slot);
    if (row != nullptr
        && row->config.correction == int(CorrectionMode::REPLAY)) {
        note_replay_depth(
            p_slot,
            replay_reseed_horizon(
                p_slot,
                p_ack,
                p_sample,
                p_participants,
                p_demote
            )
        );
    }
    adopt_alignment(
        p_slot,
        p_ack,
        handle->get_schedule() == NetwPredict::SCHEDULE_FRAME
            ? last_driven_entry_index_of(p_slot)
            : latest_input_tick_of(p_slot)
    );
    sync_episode(p_slot);
    handle->emit_signal(
        StringName("state_evaluated"),
        p_recv_tick,
        p_ack,
        0.0,
        false
    );
}

void NetwPredictionEngine::run_input(
    int64_t p_slot,
    const Dictionary &p_input,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    if (owner_bound(p_slot)) {
        run_step(p_slot, p_input, p_delta, p_tick, p_fresh);
        return;
    }
    apply_input(p_slot, p_input);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle != nullptr && handle->get_simulate().is_valid()) {
        Array stepped;
        stepped.push_back(p_delta);
        stepped.push_back(p_tick);
        stepped.push_back(p_fresh);
        handle->get_simulate().callv(stepped);
    }
}

void NetwPredictionEngine::consume_one(int64_t p_slot, double p_delta) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    const Ref<NetwTimeline> history = timeline_of(p_slot);
    if (handle == nullptr || history.is_null()) {
        return;
    }
    const int64_t next = next_input_tick_of(p_slot);
    const bool has_input = history->has_input_at(next);
    const predict::ConsumeInputPlan plan = plan_consume_input(
        int(Schedule::TICK),
        has_input,
        history->newest_input_tick() > next,
        !last_input_of(p_slot).is_empty(),
        handle->get_missing_policy()
    );
    if (!plan.eligible) {
        return;
    }
    const Dictionary input = has_input ? history->input_at(next)
        : plan.use_last                ? last_input_of(p_slot)
                                       : stall_input_of(p_slot);
    record_drive(
        p_slot,
        next,
        next,
        int(plan.kind),
        input,
        next,
        false,
        false,
        false
    );
    if (plan.run) {
        run_input(p_slot, input, p_delta, next, true);
    }
    const Ref<NetwPredictStats> counters = handle->get_stats();
    if (plan.missing) {
        if (counters.is_valid()) {
            counters->set(
                StringName("missing"),
                int64_t(counters->get(StringName("missing"))) + 1
            );
        }
    } else {
        set_last_input(p_slot, input);
        if (counters.is_valid()) {
            counters->set(
                StringName("consumed"),
                int64_t(counters->get(StringName("consumed"))) + 1
            );
        }
    }
    set_ack(p_slot, next);
    set_next_input_tick(p_slot, next + 1);
}

int NetwPredictionEngine::replay_tick_window(
    int64_t p_slot,
    int64_t p_from,
    int64_t p_to,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote
) {
    const predict::Slot *row = row_of(p_slot);
    const Ref<NetwTimeline> history = timeline_of(p_slot);
    if (row == nullptr || history.is_null()) {
        return 0;
    }
    const Array window = history->inputs_in_range(p_from, p_to);
    note_replay_depth(p_slot, window.size());
    const Dictionary live = capture_input(p_slot);
    const double delta = row->cursor.tick_delta;
    for (int at = 0; at < window.size(); ++at) {
        const Dictionary entry = window[at];
        const int64_t tick = entry.get(StringName("tick"), -1);
        run_step(
            p_slot,
            entry.get(StringName("input"), Dictionary()),
            delta,
            tick,
            false
        );
        const Dictionary state = capture_current(p_slot);
        history->record_state(tick + 1, state);
        const Dictionary solve
            = seal_transition(p_slot, tick, state, p_sample, p_participants);
        if (!solve.is_empty() && p_demote.is_valid()) {
            Array charged;
            charged.push_back(tick);
            charged.push_back(solve);
            p_demote.callv(charged);
        }
    }
    apply_input(p_slot, live);
    return window.size();
}

bool NetwPredictionEngine::run_recovery_ladder(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_ack_label,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    int p_meter,
    int p_domain,
    int p_attribution,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote,
    const Callable &p_seam
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return false;
    }
    const int window = open_recovery(
        p_slot,
        p_ack,
        p_predicted,
        p_payload,
        p_meter,
        p_domain
    );
    const Dictionary before = recovery_before_of(p_slot);
    Dictionary correction_write = recovery_write_of(p_slot);
    if (window == RECOVERY_DISSIPATED) {
        return false;
    }
    RecoveryPlan plan;
    bool escalated = escalation_pending(p_slot);
    if (window == RECOVERY_TRANSPORTED) {
        plan.restore = correction_write;
        plan.write = correction_write;
        plan.skip = false;
    } else {
        const predict::WritePlan pool_plan = plan_recovery(
            p_slot,
            p_ack,
            p_ack_label,
            p_predicted,
            p_payload,
            escalated,
            p_domain,
            p_attribution
        );
        const bool pool_planned
            = row_of(p_slot) != nullptr && !p_seam.is_valid();
        if (pool_planned) {
            plan = recovery_of(p_slot, pool_plan);
            escalated = pool_plan.escalated;
            if (escalated) {
                set_cooldown_until_tick(
                    p_slot,
                    latest_input_tick_of(p_slot)
                        + handle->get_collision_cooldown_ticks()
                );
            }
        } else {
            Dictionary context;
            context[StringName("domain")] = p_domain;
            context[StringName("attribution")] = p_attribution;
            context[StringName("contact_window")]
                = out_of_domain_at(p_slot, p_ack_label);
            context[StringName("suppressed")] = handle->get_sleeping()
                || probation_pending(p_slot)
                || latest_input_tick_of(p_slot)
                    < cooldown_until_tick_of(p_slot);
            context[StringName("pose_unmeasured")]
                = escalated || !has_pose_fields(p_slot);
            context[StringName("ack_age_ticks")] = handle->get_ack_age_ticks();
            plan = recover_through(p_slot, context, before, p_seam);
            if (escalated) {
                record_episode_escalation(
                    p_slot,
                    trigger_shape(p_slot, handle->get_divergence_epsilon())
                );
                sync_episode(p_slot);
                set_cooldown_until_tick(
                    p_slot,
                    latest_input_tick_of(p_slot)
                        + handle->get_collision_cooldown_ticks()
                );
            }
        }
        apply_recovery_plan(p_slot, plan, p_ack, pool_planned);
        correction_write = recovery_write_of(p_slot);
    }
    close_state_recovery(
        p_slot,
        p_ack,
        p_payload,
        plan,
        p_attribution,
        before,
        correction_write,
        p_sample,
        p_participants,
        p_demote
    );
    return true;
}

RecoveryPlan NetwPredictionEngine::recover_through(
    int64_t p_slot,
    const Dictionary &p_context,
    const Dictionary &p_before,
    const Callable &p_seam
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return RecoveryPlan();
    }
    const Dictionary carried = recovery_carried_of(p_slot);
    const int policy = handle->resolved_recovery_policy();
    const int correction = correction_of(p_slot);
    const int snap_restore = handle->get_snap_restore();
    const Dictionary projection = recovery_projection_of(p_slot);
    const Dictionary tier_errors = recovery_tier_errors_of(p_slot);
    const double delta = tick_delta_of(p_slot);
    if (p_seam.is_valid()) {
        Array asked;
        asked.push_back(carried);
        asked.push_back(policy);
        asked.push_back(correction);
        asked.push_back(snap_restore);
        asked.push_back(projection);
        asked.push_back(p_before);
        asked.push_back(tier_errors);
        asked.push_back(p_context);
        asked.push_back(delta);
        RecoveryPlan answered;
        if (plan_answered(
                Ref<NetwPredictRecovery>(p_seam.callv(asked)),
                p_slot,
                answered
            )) {
            return answered;
        }
    }
    return prediction_core::recover_plan(
        carried,
        policy,
        correction,
        snap_restore,
        projection,
        p_before,
        tier_errors,
        wiring_snapshot(p_slot),
        p_context,
        delta
    );
}

bool NetwPredictionEngine::plan_answered(
    const Ref<NetwPredictRecovery> &p_answered,
    int64_t p_slot,
    RecoveryPlan &r_plan
) {
    if (p_answered.is_valid()) {
        r_plan.restore = p_answered->restore();
        r_plan.write = p_answered->write();
        r_plan.teleport = p_answered->teleport();
        r_plan.skip = p_answered->skip();
        return true;
    }
    refuse_seam(StringName("_predict_recover"), p_slot);
    return false;
}

bool NetwPredictionEngine::judgement_answered(
    const Ref<NetwPredictJudgement> &p_answered,
    int64_t p_slot,
    Judgement &r_judged
) {
    if (p_answered.is_valid()) {
        r_judged.divergence = p_answered->divergence();
        r_judged.corrected = p_answered->corrected();
        return true;
    }
    refuse_seam(StringName("_predict_evaluate"), p_slot);
    return false;
}

void NetwPredictionEngine::refuse_seam(
    const StringName &p_seam,
    int64_t p_slot
) {
    NetwMultiplayer *host = core();
    if (host == nullptr) {
        return;
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    host->seam_refused(
        p_seam,
        seated.is_valid() ? host->liveness_route_of(seated.ptr()) : 0,
        String("null")
    );
}

Dictionary NetwPredictionEngine::judge_state(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_domain,
    int64_t p_row_flags,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    const Callable &p_seam
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    const double epsilon
        = handle != nullptr ? handle->get_divergence_epsilon() : 0.0;
    const predict::StateVerdict pool_verdict = compare_state_for(
        p_slot,
        p_ack,
        p_predicted,
        p_payload,
        meter_tolerances(p_slot, int(p_domain), epsilon),
        epsilon
    );
    double divergence = 0.0;
    bool corrected = false;
    bool settled = false;
    int meter = 0;
    if (pool_verdict.transition >= 0 && !p_seam.is_valid()) {
        note_divergence(p_slot, field_divergence(p_slot, pool_verdict));
        divergence = pool_verdict.divergence;
        corrected = pool_verdict.corrected;
        settled = pool_verdict.settled;
        meter = pool_verdict.meter;
    } else {
        const int exact_verdict = (p_row_flags & predict::ROW_ACKED) == 0
            ? int(NetwPredict::EXACT_VERDICT_UNJUDGED)
            : ((p_row_flags & predict::ROW_MATCHED) != 0
                   ? int(NetwPredict::EXACT_VERDICT_EQUAL)
                   : int(NetwPredict::EXACT_VERDICT_UNEQUAL));
        Dictionary seam_divergence;
        Judgement judged;
        bool answered = false;
        if (p_seam.is_valid()) {
            Array asked;
            asked.push_back(p_domain);
            asked.push_back(exact_verdict);
            asked.push_back(p_predicted);
            asked.push_back(p_payload);
            asked.push_back(seam_divergence);
            answered = judgement_answered(
                Ref<NetwPredictJudgement>(p_seam.callv(asked)),
                p_slot,
                judged
            );
        }
        if (!answered) {
            judged = prediction_core::judge(
                int(p_domain),
                exact_verdict,
                p_predicted,
                p_payload,
                wiring_snapshot(p_slot),
                seam_divergence
            );
        }
        note_divergence(p_slot, seam_divergence);
        divergence = judged.divergence;
        corrected = judged.corrected;
        settled = p_domain == int64_t(predict::Domain::OUT_OF_DOMAIN)
            || exact_verdict != int(NetwPredict::EXACT_VERDICT_UNJUDGED);
        meter = meter_of(p_slot, seam_divergence, int(p_domain), epsilon);
    }
    Dictionary detail;
    detail[StringName("transition")] = p_ack;
    detail[StringName("divergence")] = divergence;
    detail[StringName("corrected")] = corrected;
    report_predict(p_slot, EventPlane::PREDICT_EVALUATE, detail, Dictionary());
    if (corrected && meter == 0) {
        meter = 1;
    } else if (!corrected) {
        meter = 0;
    }
    Dictionary out;
    out[StringName("divergence")] = divergence;
    out[StringName("corrected")] = corrected;
    out[StringName("settled")] = settled;
    out[StringName("meter")] = meter;
    return out;
}

void NetwPredictionEngine::close_state_recovery(
    int64_t p_slot,
    int64_t p_ack,
    const Dictionary &p_payload,
    const RecoveryPlan &p_plan,
    int64_t p_attribution,
    const Dictionary &p_before,
    const Dictionary &p_correction_write,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    const int schedule = handle->get_schedule();
    const bool frame_scheduled = schedule == NetwPredict::SCHEDULE_FRAME;
    if (schedule == NetwPredict::SCHEDULE_TICK) {
        const Ref<NetwTimeline> history = timeline_of(p_slot);
        if (history.is_valid()) {
            history->record_state(p_ack + 1, p_payload);
        }
        mark_carry_dirty(p_slot, p_ack + 1);
    }
    const bool replays = !p_plan.skip
        && correction_of(p_slot) == NetwPredict::CORRECTION_MODE_REPLAY;
    if (replays && frame_scheduled) {
        replay_authored_entries(
            p_slot,
            p_ack,
            p_sample,
            p_participants,
            p_demote
        );
    } else if (replays) {
        replay_tick_window(
            p_slot,
            p_ack + 1,
            latest_input_tick_of(p_slot),
            p_sample,
            p_participants,
            p_demote
        );
    }
    announce_recovered(
        p_slot,
        p_ack,
        p_attribution,
        p_before,
        p_correction_write
    );
    note_reconciling(p_slot, false);
}

void NetwPredictionEngine::note_replay_depth(int64_t p_slot, int p_depth) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    row->stats.max_replay_depth = MAX(row->stats.max_replay_depth, p_depth);
}

int NetwPredictionEngine::replay_authored_entries(
    int64_t p_slot,
    int64_t p_ack,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    const LocalVector<predict::ReplayEntry> entries
        = replay_entries(p_slot, p_ack);
    note_replay_depth(p_slot, int(entries.size()));
    const Dictionary live = capture_input(p_slot);
    const double delta = row->cursor.tick_delta;
    for (uint32_t at = 0; at < entries.size(); ++at) {
        const predict::ReplayEntry &step = entries[at];
        run_step(p_slot, step.input, delta, step.label, false);
        const Dictionary solve = close_replayed_entry(
            p_slot,
            step.index,
            p_sample,
            p_participants
        );
        if (!solve.is_empty() && p_demote.is_valid()) {
            Array charged;
            charged.push_back(step.index);
            charged.push_back(solve);
            p_demote.callv(charged);
        }
    }
    apply_input(p_slot, live);
    return int(entries.size());
}

Dictionary NetwPredictionEngine::close_replayed_entry(
    int64_t p_slot,
    int64_t p_index,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants
) {
    if (row_of(p_slot) == nullptr) {
        return Dictionary();
    }
    const Dictionary state = canonicalize_state(p_slot, capture_state(p_slot));
    const Ref<NetwTimeline> entries = entry_history(p_slot);
    if (entries.is_valid()) {
        entries->record_state(p_index + 1, state);
    }
    return seal_transition(p_slot, p_index, state, p_sample, p_participants);
}

Dictionary NetwPredictionEngine::seal_transition(
    int64_t p_slot,
    int64_t p_transition,
    const Dictionary &p_state,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants
) {
    NETW_ZONE_NC("predict seal transition", colors::PREDICTION);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    if (p_transition >= 0) {
        const int origin = joint_provenance_at(p_slot, p_transition);
        const Ref<NetwTimeline> lane = timeline_of(p_slot);
        Array columns;
        const int count = row->wiring.count();
        columns.resize(count);
        for (int at = 0; at < count; ++at) {
            columns[at] = p_state.get(field_name(p_slot, at), Variant());
        }
        const Dictionary written
            = lane.is_valid() ? lane->input_at(p_transition) : Dictionary();
        const bool authored = !written.is_empty();
        const bool relayed
            = origin == int(NetwPredict::CELL_PROVENANCE_RELAYED);
        const bool substituted
            = origin == int(NetwPredict::CELL_PROVENANCE_SUBSTITUTED);
        Variant command = coast_command(p_slot);
        switch (NetwPredict::joint_cell(authored, relayed, substituted)) {
            case int(NetwPredict::CELL_PROVENANCE_AUTHORED):
                command = written;
                break;
            case int(NetwPredict::CELL_PROVENANCE_RELAYED):
            case int(NetwPredict::CELL_PROVENANCE_SUBSTITUTED):
                command = joint_command_at(p_slot, p_transition);
                break;
            default:
                break;
        }
        joint_record(
            p_slot,
            p_transition,
            columns,
            command,
            authored,
            relayed,
            substituted
        );
    }
    const int seat = journal_slot_of(p_slot, p_transition);
    if (seat < 0
        || (journal_flags_at(p_slot, seat) & predict::ROW_CLOSED) != 0) {
        return Dictionary();
    }
    const Dictionary solve = solve_evidence(
        p_slot,
        open_topology_of(p_slot),
        p_transition,
        p_sample,
        p_participants
    );
    const int32_t post_fp = state_fingerprint(p_slot, p_state);
    const PackedInt32Array families
        = state_family_fingerprints(p_slot, p_state);
    if (families.size() >= 3) {
        const Array contacts = solve.get(StringName("contacts"), Array());
        PackedStringArray identities;
        PackedInt32Array witness_classes;
        PackedInt32Array realizations;
        PackedByteArray outside;
        for (int at = 0; at < contacts.size(); ++at) {
            const Dictionary contact = contacts[at];
            identities.append(contact[StringName("identity")]);
            witness_classes.append(contact[StringName("witness_class")]);
            realizations.append(contact[StringName("realization")]);
            outside.append(
                bool(contact[StringName("outside_boundary")]) ? 1 : 0
            );
        }
        record_evidence(
            p_slot,
            p_transition,
            declared_epoch_of(p_slot),
            sensor_samples(p_slot),
            solve.get(StringName("topology_facts"), Dictionary()),
            identities,
            witness_classes,
            realizations,
            outside,
            bool(solve.get(StringName("sleeping"), false))
        );
        close_drive(
            p_slot,
            p_transition,
            post_fp,
            families[0],
            families[1],
            families[2]
        );
    }
    record_witness_detail(
        p_slot,
        p_transition,
        solve.get(StringName("detail"), Dictionary())
    );
    return solve;
}

void NetwPredictionEngine::ledger_note_writes(
    int64_t p_slot,
    const Dictionary &p_deltas
) {
    if (row_of(p_slot) == nullptr) {
        return;
    }
    const int64_t frontier = drive_frontier(p_slot);
    const Array keys = p_deltas.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        if (!is_causal_field(p_slot, key)) {
            continue;
        }
        ledger_bump_repaired(p_slot, key);
        if (has_divergence(p_slot, key)) {
            ledger_arm(
                p_slot,
                field_slot(p_slot, key),
                divergence_of(p_slot, key, INFINITY),
                frontier
            );
        }
    }
}

Array NetwPredictionEngine::state_columns_of(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    Array columns;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return columns;
    }
    const int count = row->wiring.count();
    columns.resize(count);
    for (int at = 0; at < count; ++at) {
        columns[at] = p_payload.get(field_name(p_slot, at), Variant());
    }
    return columns;
}

PackedFloat64Array NetwPredictionEngine::tolerance_columns_of(
    int64_t p_slot,
    const Dictionary &p_tolerances
) const {
    PackedFloat64Array row_out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return row_out;
    }
    const int count = row->wiring.count();
    row_out.resize(count);
    double *values = row_out.ptrw();
    for (int at = 0; at < count; ++at) {
        values[at] = p_tolerances.get(field_name(p_slot, at), -1.0);
    }
    return row_out;
}

RecoveryPlan NetwPredictionEngine::recovery_of(
    int64_t p_slot,
    const predict::WritePlan &p_plan
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return RecoveryPlan();
    }
    Dictionary restore;
    Dictionary write;
    const int count = row->wiring.count();
    for (int at = 0; at < count; ++at) {
        const StringName key = field_name(p_slot, at);
        if (p_plan.restore.has(at)) {
            restore[key] = p_plan.restore.values[uint32_t(at)];
        }
        if (p_plan.write.has(at)) {
            write[key] = p_plan.write.values[uint32_t(at)];
        }
    }
    RecoveryPlan out;
    out.restore = restore;
    out.write = write;
    out.teleport = p_plan.teleport;
    out.skip = p_plan.skip;
    return out;
}

Dictionary NetwPredictionEngine::restore_payload_of(
    int64_t p_slot,
    const predict::JointPassPlan &p_plan,
    int p_index
) const {
    Dictionary payload;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_index < 0
        || p_index >= int(p_plan.restores.size())) {
        return payload;
    }
    const predict::StateRow &state = p_plan.restores[uint32_t(p_index)].state;
    const int count = row->wiring.count();
    for (int at = 0; at < count; ++at) {
        if (state.has(at)) {
            payload[field_name(p_slot, at)] = state.values[uint32_t(at)];
        }
    }
    return payload;
}

bool NetwPredictionEngine::apply_restore(
    int64_t p_slot,
    const Dictionary &p_payload,
    int p_operator,
    int64_t p_basis,
    int64_t p_provenance_slot,
    const StringName &p_target,
    int p_ack_age_ticks,
    double p_divergence_epsilon,
    bool p_evidence_free,
    bool p_pool_planned
) {
    if (p_basis < -1 || p_operator < int(NetwPredictJournal::NONE)
        || p_operator > int(NetwPredictJournal::JOINT_REBASE)) {
        return false;
    }
    if (row_of(p_slot) == nullptr) {
        return false;
    }
    if (!apply_state(p_slot, p_payload)) {
        const Ref<NetwPropertySetBinding> binding = state_binding_of(p_slot);
        if (binding.is_valid()) {
            binding->apply_payload(p_payload);
        }
    }
    if (predict::Slot *row = mutable_row_of(p_slot)) {
        port_sync_transform(row->port.resolve(sys::PREDICTION));
    }
    record_restore(
        p_slot,
        p_payload,
        p_operator,
        p_basis,
        p_provenance_slot,
        p_target,
        p_ack_age_ticks,
        p_divergence_epsilon,
        p_evidence_free,
        p_pool_planned
    );
    return true;
}

Dictionary NetwPredictionEngine::write_deltas(
    int64_t p_slot,
    const Dictionary &p_before,
    const Dictionary &p_staged
) {
    Dictionary deltas;
    if (row_of(p_slot) == nullptr) {
        return deltas;
    }
    Dictionary after = capture_state(p_slot);
    const Array staged_keys = p_staged.keys();
    for (int at = 0; at < staged_keys.size(); ++at) {
        after[staged_keys[at]] = p_staged[staged_keys[at]];
    }
    const Array keys = after.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        if (!p_before.has(key)) {
            continue;
        }
        const Variant delta = prediction_core::pose_delta(
            after[key],
            p_before[key],
            is_angle_field(p_slot, key)
        );
        if (delta.get_type() == Variant::NIL || delta_negligible(delta)) {
            continue;
        }
        deltas[key] = delta;
    }
    return deltas;
}

bool NetwPredictionEngine::recent_witness_clean(
    int64_t p_slot,
    int64_t p_basis
) const {
    int64_t expected = p_basis;
    const PackedInt64Array held = journal_transitions(p_slot);
    for (int at = 0; at < held.size(); ++at) {
        const int64_t transition = held[at];
        if (transition < p_basis) {
            continue;
        }
        if (transition != expected) {
            return false;
        }
        const int index = journal_slot_of(p_slot, transition);
        if (index < 0
            || (journal_flags_at(p_slot, index) & predict::ROW_CLOSED) == 0) {
            return expected > p_basis;
        }
        if (!witness_row_clean(p_slot, transition, false)) {
            return false;
        }
        expected += 1;
    }
    return expected > p_basis;
}

Dictionary NetwPredictionEngine::try_transport(
    int64_t p_slot,
    const Dictionary &p_predicted,
    const Dictionary &p_authority,
    const Dictionary &p_current,
    int64_t p_basis,
    bool p_escalated,
    const Callable &p_corridor,
    double p_teleport_default,
    double p_divergence_epsilon,
    int p_recovery_policy
) {
    const predict::Slot *row = row_of(p_slot);
    const PackedInt64Array stats = episode_stats(p_slot);
    if (row == nullptr || !p_corridor.is_valid()
        || stats[STAT_EPISODE_ACTIVE] == 0
        || stats[STAT_EPISODE_TRANSPORT_DECIDED] != 0) {
        return Dictionary();
    }
    const Dictionary candidate
        = transport_of(p_slot, p_predicted, p_authority, p_current);
    const bool basis_clean = witness_row_clean(p_slot, p_basis, true);
    const bool recent_clean = recent_witness_clean(p_slot, p_basis);
    const Dictionary non_pose = non_pose_eligibility(
        p_slot,
        p_predicted,
        p_authority,
        p_divergence_epsilon
    );
    const Dictionary deltas = transport_deltas(
        p_slot,
        p_current,
        candidate.get(StringName("restore"), Dictionary())
    );
    const bool below_teleport = transport_moved(deltas)
        && !teleport_reached_of(p_slot, deltas, p_teleport_default);
    const bool observing
        = p_recovery_policy == int(NetwPredict::RECOVERY_POLICY_OBSERVE);
    const bool valid = candidate.get(StringName("valid"), false);
    const bool prior_ok = transport_admissible(
        p_slot,
        valid,
        basis_clean,
        recent_clean,
        bool(non_pose[StringName("agrees")]),
        below_teleport,
        p_escalated,
        observing
    );
    bool corridor_clear = false;
    if (prior_ok) {
        Dictionary proposed = p_current.duplicate();
        proposed.merge(candidate[StringName("restore")], true);
        corridor_clear = bool(p_corridor.call(p_current, proposed));
    }
    const bool eligible = prior_ok && corridor_clear;

    Dictionary eligibility;
    eligibility[StringName("candidate")] = valid;
    eligibility[StringName("basis_witness_clean")] = basis_clean;
    eligibility[StringName("recent_witness_clean")] = recent_clean;
    eligibility[StringName("non_pose")] = non_pose;
    eligibility[StringName("delta")] = deltas;
    eligibility[StringName("below_teleport")] = below_teleport;
    eligibility[StringName("policy")] = !p_escalated && !observing
        && row->config.correction == int(CorrectionMode::SNAP);
    eligibility[StringName("corridor_clear")] = corridor_clear;
    record_episode_decision(
        p_slot,
        int(predict::Operator::TRANSPORT_DELTA),
        p_basis,
        eligible,
        eligible,
        eligibility
    );
    return eligible ? candidate : Dictionary();
}

Dictionary NetwPredictionEngine::solve_evidence(
    int64_t p_slot,
    const Dictionary &p_base_facts,
    int64_t p_transition,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants
) {
    NETW_ZONE_NC("predict solve evidence", colors::PREDICTION);
    roster_clear(p_slot, ROSTER_REALIZED_CONTACT);

    Dictionary out;
    out[StringName("topology_facts")] = p_base_facts;
    out[StringName("contacts")] = TypedArray<Dictionary>();
    out[StringName("sleeping")] = false;
    out[StringName("detail")] = Dictionary();
    if (p_sample.is_empty()) {
        return out;
    }

    const Dictionary ground
        = sensor_samples(p_slot).get(StringName("ground"), Dictionary());
    const Variant named = ground.get(StringName("collider"), Variant());
    const String support = named.get_type() == Variant::STRING
            || named.get_type() == Variant::STRING_NAME
        ? String(named)
        : String();

    Array contact_classes;
    PackedStringArray collider_classes;
    PackedStringArray collider_ids;
    PackedStringArray compared_contacts;
    PackedStringArray outside_boundary_ids;
    int witness_class_bits = 0;
    TypedArray<Dictionary> contacts;

    const Array colliders = p_sample[StringName("colliders")];
    for (int at = 0; at < colliders.size(); ++at) {
        Object *collider = colliders[at];
        const Ref<NetwEntity> touched = contact_entity(collider);
        if (touched.is_valid() && slot_of(touched) != p_slot) {
            roster_add(p_slot, ROSTER_REALIZED_CONTACT, touched);
        }
        const int realization = classify_collider(collider, support);
        if (!contact_classes.has(realization)) {
            contact_classes.append(realization);
        }
        const String kind
            = collider != nullptr ? collider->get_class() : String();
        if (!collider_classes.has(kind)) {
            collider_classes.append(kind);
        }
        const String identity = collider_identity(collider);
        if (!collider_ids.has(identity)) {
            collider_ids.append(identity);
        }
        if (contact_breaches_boundary(p_slot, collider, support, p_participants)
            && !outside_boundary_ids.has(identity)) {
            outside_boundary_ids.append(identity);
        }
        const int witness = witness_class_of(collider, support);
        witness_class_bits |= witness;
        const String compared = identity + String(":") + itos(witness);
        if (!compared_contacts.has(compared)) {
            compared_contacts.append(compared);
        }
        Dictionary contact;
        contact[StringName("identity")] = identity;
        contact[StringName("witness_class")] = witness;
        contact[StringName("realization")] = realization;
        contact[StringName("outside_boundary")]
            = outside_boundary_ids.has(identity);
        contacts.append(contact);
    }
    if (contact_classes.is_empty()) {
        contact_classes.append(int(NetwPredict::CONTACT_CLASS_NONE));
    }
    contact_classes.sort();
    collider_classes.sort();
    collider_ids.sort();
    compared_contacts.sort();
    outside_boundary_ids.sort();

    const bool sleeping = p_sample[StringName("sleeping")];
    const bool woke = has_previous_witness_of(p_slot)
        && previous_witness_sleeping_of(p_slot) && !sleeping;
    set_previous_witness_sleeping(p_slot, sleeping);
    set_has_previous_witness(p_slot, true);

    const Dictionary continuous
        = p_sample.get(StringName("continuous"), Dictionary());
    Dictionary detail;
    detail[StringName("contact_classes")] = contact_classes;
    detail[StringName("collider_classes")] = collider_classes;
    detail[StringName("collider_ids")] = collider_ids;
    detail[StringName("contact_bucket")]
        = prediction_core::contact_count_bucket(colliders.size());
    detail[StringName("sleeping")] = sleeping;
    detail[StringName("woke")] = woke;
    detail[StringName("solve_ordinal")] = p_transition;
    detail[StringName("witness_class_bits")] = witness_class_bits;
    detail[StringName("outside_boundary_ids")] = outside_boundary_ids;
    detail[StringName("breach")] = !outside_boundary_ids.is_empty();
    detail[StringName("continuous")] = continuous.duplicate(true);

    Dictionary facts = p_base_facts.duplicate();
    const StringName SAMPLED[] = {
        StringName("body_mode"),
        StringName("collision_layer"),
        StringName("collision_mask"),
    };
    for (const StringName &key : SAMPLED) {
        if (p_sample.has(key)) {
            facts[key] = p_sample[key];
        }
    }

    out[StringName("topology_facts")] = facts;
    out[StringName("breach")] = !outside_boundary_ids.is_empty();
    out[StringName("contacts")] = contacts;
    out[StringName("sleeping")] = sleeping;
    out[StringName("detail")] = detail;
    return out;
}

int NetwPredictionEngine::witness_class_of(
    Object *p_collider,
    const String &p_support
) const {
    return witness_class(
        p_collider,
        !p_support.is_empty() && collider_identity(p_collider) == p_support
    );
}

bool NetwPredictionEngine::contact_breaches_boundary(
    int64_t p_slot,
    Object *p_collider,
    const String &p_support,
    const PackedStringArray &p_participants
) const {
    if (!p_support.is_empty() && collider_identity(p_collider) == p_support) {
        return false;
    }
    if (static_geometry(p_collider)) {
        return false;
    }
    const Ref<NetwEntity> entity = contact_entity(p_collider);
    if (entity.is_null()) {
        return true;
    }
    if (slot_of(entity) == p_slot) {
        return false;
    }
    if (!p_participants.has(String(entity->get_entity_id()))) {
        return true;
    }
    const Ref<NetwPredictionHandle> declared = entity->get_prediction();
    return declared.is_valid()
        && declared->get_sim_mode() == int(NetwPredict::SIM_MODE_DISPLAY);
}

int NetwPredictionEngine::judge_carry(
    int64_t p_slot,
    const StringName &p_field,
    bool p_same_type,
    bool p_finite,
    bool p_within_envelope,
    bool p_pure,
    bool p_faithful
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        CARRY_DECLINED,
        sys::PREDICTION,
        "Carry evidence names closed slot %d.",
        p_slot
    );
    NETW_ERR_COND_V(
        row->carry_rules.is_empty(),
        CARRY_DECLINED,
        sys::PREDICTION,
        "Carry evidence needs a carry declaration."
    );
    const int field = row->wiring.fields.index_of(p_field);
    NETW_ERR_COND_V(
        field < 0,
        CARRY_DECLINED,
        sys::PREDICTION,
        "Carry evidence names undeclared field '%s'.",
        String(p_field).utf8().get_data()
    );
    predict::CarryProbe probe;
    probe.same_type = p_same_type;
    probe.finite = p_finite;
    probe.within_envelope = p_within_envelope;
    probe.pure = p_pure;
    probe.faithful = p_faithful;
    return int(row->carry.judge(field, row->config.schedule, probe));
}

int NetwPredictionEngine::decline_carry(
    int64_t p_slot,
    const StringName &p_field
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        CARRY_DECLINED,
        sys::PREDICTION,
        "Carry refusal names closed slot %d.",
        p_slot
    );
    const int field = row->wiring.fields.index_of(p_field);
    NETW_ERR_COND_V(
        field < 0,
        CARRY_DECLINED,
        sys::PREDICTION,
        "Carry refusal names undeclared field '%s'.",
        String(p_field).utf8().get_data()
    );
    return int(row->carry.decline(field, row->config.schedule));
}

bool NetwPredictionEngine::carry_eligible(
    int64_t p_slot,
    const StringName &p_field
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || row->carry_rules.is_empty()
        || row->config.schedule != int(Schedule::FRAME)) {
        return false;
    }
    const predict::CarryFieldStats *stats
        = row->carry.field(row->wiring.fields.index_of(p_field));
    return stats != nullptr && !stats->retired;
}

bool NetwPredictionEngine::carry_retired(
    int64_t p_slot,
    const StringName &p_field
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    const predict::CarryFieldStats *stats
        = row->carry.field(row->wiring.fields.index_of(p_field));
    return stats != nullptr && stats->retired;
}

PackedInt64Array NetwPredictionEngine::carry_stats(
    int64_t p_slot,
    const StringName &p_field
) const {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(3);
    const predict::Slot *row = row_of(p_slot);
    const predict::CarryFieldStats *stats = row != nullptr
        ? row->carry.field(row->wiring.fields.index_of(p_field))
        : nullptr;
    if (stats != nullptr) {
        int64_t *values = out.ptrw();
        values[0] = stats->carried;
        values[1] = stats->declined;
        values[2] = stats->infidelity;
    }
    return out;
}

void NetwPredictionEngine::mark_carry_dirty(
    int64_t p_slot,
    int64_t p_transition
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->mark_carry_dirty(p_transition);
    }
}

bool NetwPredictionEngine::carry_judgeable(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->carry_judgeable(p_transition);
}

int32_t NetwPredictionEngine::compared_fingerprint(
    predict::Slot &p_row,
    const Dictionary &p_payload
) {
    Dictionary scope;
    const Array fields = p_payload.keys();
    for (int at = 0; at < fields.size(); ++at) {
        const int slot = p_row.wiring.fields.index_of(fields[at]);
        if (slot >= 0 && p_row.wiring.causal[uint32_t(slot)] != 0) {
            scope[fields[at]] = p_payload[fields[at]];
        }
    }
    const PackedByteArray bytes
        = predict::canonical_bytes(p_row.wiring.codec, scope);
    return predict::fnv1a(bytes.ptr(), int(bytes.size()));
}

int32_t NetwPredictionEngine::state_fingerprint(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict state fingerprint", colors::PREDICTION);
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    return compared_fingerprint(*row, p_payload);
}

PackedInt32Array NetwPredictionEngine::state_family_fingerprints(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict state family fingerprints", colors::PREDICTION);
    PackedInt32Array out = gd::zeroed<PackedInt32Array>(3);
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    Dictionary families[3];
    const Array fields = p_payload.keys();
    for (int at = 0; at < fields.size(); ++at) {
        const int field = row->wiring.fields.index_of(fields[at]);
        if (field < 0 || row->wiring.causal[uint32_t(field)] == 0) {
            continue;
        }
        const int family = row->wiring.state_family[uint32_t(field)];
        if (family < 0 || family >= 3) {
            continue;
        }
        families[family][fields[at]] = p_payload[fields[at]];
    }
    int32_t *values = out.ptrw();
    for (int family = 0; family < 3; ++family) {
        if (families[family].is_empty()) {
            continue;
        }
        const PackedByteArray bytes
            = predict::canonical_bytes(row->wiring.codec, families[family]);
        values[family] = predict::fnv1a(bytes.ptr(), int(bytes.size()));
    }
    return out;
}

Variant NetwPredictionEngine::call_carry(
    int64_t p_slot,
    const Callable &p_rule,
    const Variant &p_value,
    const predict::ReplayEntry &p_entry,
    const Dictionary &p_state
) {
    Ref<NetwPredictCarryContext> context;
    context.instantiate();
    context->state_at = p_state;
    context->input_at = p_entry.input;
    context->authored_label = p_entry.label;
    const predict::Slot *row = row_of(p_slot);
    context->step_delta = row != nullptr ? row->tick_delta : 0.0;

    Array arguments;
    arguments.push_back(p_value);
    arguments.push_back(context);
    depth += 1;
    const Variant result = p_rule.callv(arguments);
    depth -= 1;

    switch (result.get_type()) {
        case Variant::FLOAT:
            return std::isfinite(double(result)) ? result : Variant();
        case Variant::VECTOR2:
            return Vector2(result).is_finite() ? result : Variant();
        case Variant::VECTOR3:
            return Vector3(result).is_finite() ? result : Variant();
        default:
            return result;
    }
}

void NetwPredictionEngine::replay_carry(
    predict::CarryAttempt &r_attempt,
    int64_t p_slot,
    const Callable &p_rule,
    const StringName &p_field,
    int p_field_slot,
    const LocalVector<predict::ReplayEntry> &p_entries,
    bool p_angle,
    double p_divergence_epsilon
) {
    for (uint32_t at = 0; at < p_entries.size(); ++at) {
        const predict::Slot *row = row_of(p_slot);
        if (row == nullptr) {
            break;
        }
        const int64_t index = p_entries[at].index;
        if (!row->carry_judgeable(index)) {
            continue;
        }
        const Dictionary from = row->state_before(index);
        const Dictionary reached = row->state_before(index + 1);
        if (!from.has(p_field) || !reached.has(p_field)) {
            continue;
        }
        const double tolerance
            = row->wiring.epsilon[uint32_t(p_field_slot)] >= 0.0
            ? row->wiring.epsilon[uint32_t(p_field_slot)]
            : p_divergence_epsilon;
        const Variant stepped
            = call_carry(p_slot, p_rule, from[p_field], p_entries[at], from);
        if (stepped.get_type() != reached[p_field].get_type()) {
            r_attempt.probe.faithful = false;
            return;
        }
        const double residual
            = predict::value_error(stepped, reached[p_field], p_angle);
        if (residual > tolerance) {
            r_attempt.probe.faithful = false;
            r_attempt.residual = residual;
            r_attempt.tolerance = tolerance;
        }
        return;
    }
    r_attempt.evidence = false;
}

void NetwPredictionEngine::fold_carry(
    predict::CarryAttempt &r_attempt,
    int64_t p_slot,
    const Callable &p_rule,
    int p_field_slot,
    const Variant &p_start,
    const LocalVector<predict::ReplayEntry> &p_entries,
    bool p_angle,
    double p_teleport_default
) {
    Variant value = p_start;
    for (uint32_t at = 0; at < p_entries.size(); ++at) {
        const predict::Slot *row = row_of(p_slot);
        if (row == nullptr) {
            r_attempt.evidence = false;
            return;
        }
        const Dictionary state = row->state_before(p_entries[at].index);
        if (state.is_empty()) {
            r_attempt.evidence = false;
            return;
        }
        const Variant stepped
            = call_carry(p_slot, p_rule, value, p_entries[at], state);
        if (stepped.get_type() == Variant::NIL) {
            r_attempt.probe.finite = false;
            return;
        }
        if (stepped.get_type() != p_start.get_type()) {
            r_attempt.probe.same_type = false;
            return;
        }
        value = stepped;
    }
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        r_attempt.evidence = false;
        return;
    }
    const double envelope = row->wiring.teleport[uint32_t(p_field_slot)] >= 0.0
        ? row->wiring.teleport[uint32_t(p_field_slot)]
        : p_teleport_default;
    if (predict::value_error(value, p_start, p_angle) >= envelope) {
        r_attempt.probe.within_envelope = false;
        return;
    }
    r_attempt.value = value;
}

predict::CarryAttempt NetwPredictionEngine::attempt_carry(
    int64_t p_slot,
    const StringName &p_field,
    const Variant &p_acknowledged,
    int64_t p_basis,
    double p_teleport_default,
    double p_divergence_epsilon
) {
    NETW_ZONE_NC("predict attempt carry", colors::PREDICTION);
    predict::CarryAttempt out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const int field = row->wiring.fields.index_of(p_field);
    HashMap<StringName, Callable>::Iterator rule
        = row->carry_rules.find(p_field);
    if (field < 0 || !rule) {
        return out;
    }
    const LocalVector<predict::ReplayEntry> entries
        = row->replay_entries(p_basis);
    if (entries.is_empty()) {
        return out;
    }
    out.evidence = true;
    const bool angle = row->wiring.angle[uint32_t(field)] != 0;
    const Callable step = rule->value;

    replay_carry(
        out,
        p_slot,
        step,
        p_field,
        field,
        entries,
        angle,
        p_divergence_epsilon
    );
    if (!out.evidence || !out.probe.faithful) {
        return out;
    }

    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        out.evidence = false;
        return out;
    }
    const bool guarded = row->port.bound();
    const int32_t before = guarded
        ? compared_fingerprint(
              *row,
              capture_through(*row, row->wiring.codec, row->owner_has_state)
          )
        : 0;

    fold_carry(
        out,
        p_slot,
        step,
        field,
        p_acknowledged,
        entries,
        angle,
        p_teleport_default
    );

    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        out.evidence = false;
        return out;
    }
    const int32_t after = guarded
        ? compared_fingerprint(
              *row,
              capture_through(*row, row->wiring.codec, row->owner_has_state)
          )
        : 0;
    out.probe.pure = !guarded || after == before;
    return out;
}

LocalVector<predict::ReplayEntry> NetwPredictionEngine::replay_entries(
    int64_t p_slot,
    int64_t p_basis
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->replay_entries(p_basis)
                          : LocalVector<predict::ReplayEntry>();
}

Dictionary NetwPredictionEngine::state_before(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->state_before(p_transition) : Dictionary();
}

void NetwPredictionEngine::close_drive(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_post_fp,
    int64_t p_post_pose_fp,
    int64_t p_post_momentum_fp,
    int64_t p_post_controller_fp
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    predict::StateStamp post;
    post.fp = int32_t(p_post_fp);
    post.families.pose = int32_t(p_post_pose_fp);
    post.families.momentum = int32_t(p_post_momentum_fp);
    post.families.controller = int32_t(p_post_controller_fp);
    row->close_drive(p_transition, post);
}

void NetwPredictionEngine::mark_domain(
    int64_t p_slot,
    int64_t p_transition,
    int p_domain
) {
    NETW_ERR_COND(
        p_domain < int(predict::Domain::IN_DOMAIN)
            || p_domain > int(predict::Domain::OUT_OF_DOMAIN),
        sys::PREDICTION,
        "Journal domain %d is outside the declared range.",
        p_domain
    );
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->journal.mark_domain(p_transition, predict::Domain(p_domain));
    }
}

void NetwPredictionEngine::mark_chain_broken(
    int64_t p_slot,
    int64_t p_transition
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Chain break names closed slot %d.",
        p_slot
    );
    row->journal.mark_chain_broken(p_transition);
}

void NetwPredictionEngine::mark_provenance(
    int64_t p_slot,
    int64_t p_transition,
    int p_episode_id,
    int p_write_id,
    int p_operator,
    int64_t p_basis
) {
    NETW_ERR_COND(
        p_operator < int(predict::Operator::NONE)
            || p_operator > int(predict::Operator::JOINT_REBASE),
        sys::PREDICTION,
        "Journal operator %d is outside the declared range.",
        p_operator
    );
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Journal provenance names closed slot %d.",
        p_slot
    );
    const int index = row->journal.index_of(p_transition);
    const bool cleared_chain_break = index >= 0
        && (row->journal.flags_at(index) & predict::ROW_CHAIN_BROKEN) != 0;
    row->journal.mark_provenance(
        p_transition,
        p_episode_id,
        p_write_id,
        predict::Operator(p_operator),
        p_basis
    );
    if (cleared_chain_break) {
        row->stats.chain_breaks -= 1;
    }
}

void NetwPredictionEngine::mark_attribution(
    int64_t p_slot,
    int64_t p_transition,
    int p_attribution
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Attribution names closed slot %d.",
        p_slot
    );
    row->journal.mark_attribution(
        p_transition,
        predict::Attribution(p_attribution)
    );
}

void NetwPredictionEngine::mark_differing_family(
    int64_t p_slot,
    int64_t p_transition,
    int p_family
) {
    NETW_ERR_COND(
        p_family < int(predict::DifferingFamily::NONE)
            || p_family > int(predict::DifferingFamily::CONTROLLER_LATCH),
        sys::PREDICTION,
        "Differing family %d is outside the declared range.",
        p_family
    );
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Differing family names closed slot %d.",
        p_slot
    );
    row->journal.mark_differing_family(
        p_transition,
        predict::DifferingFamily(p_family)
    );
}

void NetwPredictionEngine::mark_solve(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_topo_fp,
    int64_t p_witness_fp,
    int p_evidence_mask,
    int p_witness_class_bits
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Solve evidence names closed slot %d.",
        p_slot
    );
    row->journal.mark_solve(
        p_transition,
        int32_t(p_topo_fp),
        int32_t(p_witness_fp),
        uint8_t(p_evidence_mask),
        uint8_t(p_witness_class_bits)
    );
}

bool NetwPredictionEngine::witness_judged(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->journal.witness_judged(p_transition);
}

void NetwPredictionEngine::charge_divergence(
    int64_t p_slot,
    const predict::AckFrame &p_frame,
    int64_t p_transition,
    int p_index,
    bool p_complete,
    int p_attribution
) {
    const bool named
        = p_index >= 0 && uint32_t(p_index) < p_frame.records.size();
    const predict::JournalRow row = journal_row(p_slot, p_transition);
    const int local_evidence = row.present ? int(row.evidence_mask) : 0;
    const int peer_evidence
        = p_complete && named ? int(p_frame.records[p_index].evidence_mask) : 0;
    const bool both_witness = p_complete
        && (local_evidence & int(predict::EVIDENCE_WITNESS)) != 0
        && (peer_evidence & int(predict::EVIDENCE_WITNESS)) != 0;
    const bool witness_equal = both_witness && row.present && named
        && row.witness_fp == p_frame.records[p_index].witness_fp;
    if (journal_has(p_slot, p_transition)) {
        mark_witness_match(p_slot, p_transition, witness_equal);
    }
    note_attribution(p_slot, p_attribution, p_transition);
}

void NetwPredictionEngine::mark_witness_match(
    int64_t p_slot,
    int64_t p_transition,
    bool p_matched
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Witness verdict names closed slot %d.",
        p_slot
    );
    row->journal.mark_witness_match(p_transition, p_matched);
}

void NetwPredictionEngine::declare_skipped(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->declare_skipped(p_transition, p_label);
    }
}

void NetwPredictionEngine::mark_aligned_error(
    int64_t p_slot,
    int64_t p_transition,
    double p_error
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->journal.mark_aligned_error(p_transition, p_error);
    }
}

void NetwPredictionEngine::acknowledge(
    int64_t p_slot,
    int64_t p_transition,
    bool p_matched
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->acknowledge(p_transition, p_matched);
    }
}

predict::AckVerdict NetwPredictionEngine::admit_ack(
    int64_t p_slot,
    int64_t p_transition,
    const predict::EvidenceRow &p_evidence,
    bool p_substituted
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        ? row->admit_ack(p_transition, p_evidence, p_substituted)
        : predict::AckVerdict();
}

PackedInt64Array NetwPredictionEngine::admit_ack_run(
    int64_t p_slot,
    const predict::AckFrame &p_frame,
    const Callable &p_quarantine_witness,
    const Callable &p_retry_operator
) {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(ACK_RUN_COLUMN_COUNT);
    int64_t *counts = out.ptrw();
    counts[ACK_RUN_FIRST_DIVERGENT] = -1;
    counts[ACK_RUN_AGE] = -1;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const int64_t base = int64_t(p_frame.base);
    const int record_count = int(p_frame.records.size());
    for (int at = 0; at < record_count; ++at) {
        const predict::AckEvidenceWire &record = p_frame.records[at];
        const int64_t transition = base + at;
        row->lane_cursor.ack_of_acks
            = MAX(row->lane_cursor.ack_of_acks, transition);
        const int64_t age = mark_authority_ack(p_slot, transition);
        if (age >= 0) {
            counts[ACK_RUN_AGE] = age;
        }
        record_authority_witness_class(
            p_slot,
            transition,
            predict::ack_witness_class(record.flags)
        );
        if (row->latches.fallback_latched && p_quarantine_witness.is_valid()) {
            p_quarantine_witness.call(transition);
        }
        const predict::JournalRow judged = journal_row(p_slot, transition);
        if ((record.flags & predict::ACK_SUBSTITUTED) != 0) {
            admit_ack_row(p_slot, p_frame, transition, at, true, true);
            counts[ACK_RUN_SUBSTITUTED] += 1;
            continue;
        }
        if (!judged.present || (judged.flags & predict::ROW_ACKED) != 0) {
            continue;
        }
        const predict::AckVerdict verdict
            = admit_ack_row(p_slot, p_frame, transition, at, true, false);
        const bool matched = judged.post_fp == record.post_fp;
        counts[ACK_RUN_VERIFIED] += 1;
        if (matched) {
            continue;
        }
        counts[ACK_RUN_MISMATCHED] += 1;
        if (counts[ACK_RUN_FIRST_DIVERGENT] < 0) {
            counts[ACK_RUN_FIRST_DIVERGENT] = transition;
        }
        if (verdict.transition >= 0) {
            charge_divergence(
                p_slot,
                p_frame,
                transition,
                at,
                true,
                int(verdict.attribution)
            );
            if (p_retry_operator.is_valid()) {
                p_retry_operator.call(transition);
            }
        }
    }
    row->compare_stats.fp_verified += int(counts[ACK_RUN_VERIFIED]);
    row->compare_stats.fp_mismatches += int(counts[ACK_RUN_MISMATCHED]);
    if (counts[ACK_RUN_FIRST_DIVERGENT] >= 0
        && row->compare_stats.first_divergent_transition < 0) {
        row->compare_stats.first_divergent_transition
            = counts[ACK_RUN_FIRST_DIVERGENT];
    }
    counts[ACK_RUN_OF_ACKS] = row->lane_cursor.ack_of_acks;
    refresh_ack_age(p_slot);
    const PackedInt64Array stats = drive_stats(p_slot);
    counts[ACK_RUN_AGE] = stats[STAT_ACK_AGE_TICKS];
    counts[ACK_RUN_RECEIPT] = ACK_RECEIPT_ADMITTED;
    return out;
}

PackedInt64Array NetwPredictionEngine::receive_ack_frame(
    int64_t p_slot,
    const PackedByteArray &p_payload,
    int64_t p_tape_epoch,
    bool p_confirm_reseed,
    const Callable &p_quarantine_witness,
    const Callable &p_retry_operator
) {
    PackedInt64Array refused
        = gd::zeroed<PackedInt64Array>(ACK_RUN_COLUMN_COUNT);
    int64_t *counts = refused.ptrw();
    counts[ACK_RUN_FIRST_DIVERGENT] = -1;
    counts[ACK_RUN_AGE] = -1;
    predict::AckFrame frame;
    if (!predict::decode_ack(p_payload, frame)) {
        counts[ACK_RUN_RECEIPT] = ACK_RECEIPT_UNDECODED;
        return refused;
    }
    if (int64_t(frame.epoch) != p_tape_epoch) {
        counts[ACK_RUN_RECEIPT] = ACK_RECEIPT_FOREIGN_EPOCH;
        return refused;
    }
    if (p_confirm_reseed) {
        confirm_reseed_epoch(p_slot);
    }
    return admit_ack_run(p_slot, frame, p_quarantine_witness, p_retry_operator);
}

predict::AckVerdict NetwPredictionEngine::admit_ack_row(
    int64_t p_slot,
    const predict::AckFrame &p_frame,
    int64_t p_transition,
    int p_index,
    bool p_complete,
    bool p_substituted
) {
    if (p_index < 0 || uint32_t(p_index) >= p_frame.records.size()
        || !journal_has(p_slot, p_transition)) {
        return predict::AckVerdict();
    }
    const predict::AckEvidenceWire &record = p_frame.records[p_index];
    const predict::EvidenceRow peer = evidence_row(
        p_complete ? record.pre_fp : 0,
        p_complete ? record.c_hash : 0,
        p_complete ? record.e_digest : 0,
        record.post_fp,
        p_complete ? record.pre_pose_fp : 0,
        p_complete ? record.pre_momentum_fp : 0,
        p_complete ? record.pre_controller_fp : 0,
        p_complete ? record.post_pose_fp : 0,
        p_complete ? record.post_momentum_fp : 0,
        p_complete ? record.post_controller_fp : 0,
        p_complete ? record.topo_fp : 0,
        p_complete ? record.witness_fp : 0,
        p_complete ? record.raw_fp : 0,
        p_complete ? int(record.evidence_mask) : 0,
        p_complete
    );
    return admit_ack(p_slot, p_transition, peer, p_substituted);
}

predict::StateVerdict NetwPredictionEngine::compare_state_for(
    int64_t p_slot,
    int64_t p_transition,
    const Dictionary &p_predicted,
    const Dictionary &p_authority,
    const PackedFloat64Array &p_meter_tolerances,
    double p_divergence_epsilon
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !journal_has(p_slot, p_transition)) {
        return predict::StateVerdict();
    }
    return compare_state(
        p_slot,
        p_transition,
        p_transition,
        state_columns_of(p_slot, p_predicted),
        state_columns_of(p_slot, p_authority),
        correction_tolerances(p_slot, p_divergence_epsilon),
        p_meter_tolerances,
        p_divergence_epsilon,
        row->latches.stream_reconstructed,
        row->lane_cursor.ack_domain_confirmed
    );
}

predict::StateVerdict NetwPredictionEngine::compare_state(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_transition,
    const Array &p_predicted,
    const Array &p_authority,
    const PackedFloat64Array &p_correction_tolerances,
    const PackedFloat64Array &p_meter_tolerances,
    double p_fallback_epsilon,
    bool p_stream_reconstructed,
    bool p_ack_domain_confirmed
) {
    predict::StateVerdict out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const int width = row->wiring.count();
    out = row->compare(
        p_recv_tick,
        p_transition,
        state_row(p_predicted, width),
        state_row(p_authority, width),
        tolerance_row(p_correction_tolerances, width),
        tolerance_row(p_meter_tolerances, width),
        p_fallback_epsilon,
        p_stream_reconstructed,
        p_ack_domain_confirmed
    );
    return out;
}

void NetwPredictionEngine::set_state(int64_t p_slot, const Array &p_state) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->set_state(state_row(p_state, row->wiring.count()));
    }
}

bool NetwPredictionEngine::state_has(int64_t p_slot, int p_field) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->state.has(p_field);
}

Variant NetwPredictionEngine::state_at(int64_t p_slot, int p_field) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->state.has(p_field)
        ? row->state.values[uint32_t(p_field)]
        : Variant();
}

predict::WritePlan NetwPredictionEngine::recover_for(
    int64_t p_slot,
    int64_t p_basis,
    int64_t p_current_label,
    const Dictionary &p_predicted,
    const Dictionary &p_authority,
    const Dictionary &p_current,
    const Dictionary &p_tier_errors,
    int p_policy,
    double p_divergence_epsilon,
    double p_teleport_threshold,
    int p_max_restore_ticks,
    int p_ack_age_ticks,
    int p_collision_cooldown_ticks,
    int p_domain,
    int p_attribution,
    bool p_suppressed
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return predict::WritePlan();
    }
    const predict::RecoveryRequest request = recovery_request(
        row->wiring.count(),
        state_columns_of(p_slot, p_predicted),
        state_columns_of(p_slot, p_authority),
        state_columns_of(p_slot, p_current),
        tolerance_columns_of(p_slot, p_tier_errors),
        p_basis,
        p_current_label,
        p_policy,
        p_divergence_epsilon,
        p_teleport_threshold,
        p_max_restore_ticks,
        p_ack_age_ticks,
        p_collision_cooldown_ticks,
        row->cursor.tick_delta,
        p_domain,
        p_attribution,
        out_of_domain_at(p_slot, p_current_label),
        p_suppressed,
        !has_pose_fields(p_slot)
    );
    return recover(p_slot, request);
}

predict::WritePlan NetwPredictionEngine::recover(
    int64_t p_slot,
    const predict::RecoveryRequest &p_request
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? row->recover(p_request) : predict::WritePlan();
}

void NetwPredictionEngine::open_recovery_window(
    int64_t p_slot,
    int64_t p_label,
    int p_cooldown
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->open_recovery_window(p_label, p_cooldown);
    }
}

void NetwPredictionEngine::suppress_recovery_until(
    int64_t p_slot,
    int64_t p_label,
    int p_cooldown
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->suppress_recovery_until(p_label, p_cooldown);
    }
}

bool NetwPredictionEngine::escalation_pending(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->recovery.escalate_next;
}

int64_t NetwPredictionEngine::recovery_window_until(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->recovery.window_until : -1;
}

int64_t NetwPredictionEngine::recovery_cooldown_until(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->recovery.cooldown_until : -1;
}

EpisodeReport NetwPredictionEngine::episode(int64_t p_slot) const {
    EpisodeReport out;
    const predict::Slot *row = row_of(p_slot);
    if (row != nullptr) {
        out.episode = row->episode;
    }
    return out;
}

void NetwPredictionEngine::enter_quarantine(
    int64_t p_slot,
    int64_t p_transition,
    bool p_stream_reconstructed,
    int p_attribution,
    bool p_demoted
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->latch_quarantine(
            p_transition,
            p_stream_reconstructed,
            predict::Attribution(p_attribution),
            p_demoted
        );
    }
}

predict::WritePlan NetwPredictionEngine::quarantine_state(
    int64_t p_slot,
    int64_t p_tick,
    int64_t p_basis,
    const Array &p_payload,
    bool p_whole
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? row->quarantine_state(
                                p_tick,
                                p_basis,
                                state_row(p_payload, row->wiring.count()),
                                p_whole
                            )
                          : predict::WritePlan();
}

predict::WritePlan NetwPredictionEngine::quarantine_witness(
    int64_t p_slot,
    int64_t p_basis,
    int p_bits
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? row->quarantine_witness(p_basis, p_bits)
                          : predict::WritePlan();
}

void NetwPredictionEngine::confirm_reseed_epoch(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->confirm_reseed_epoch();
    }
}

predict::WritePlan NetwPredictionEngine::align_reseed(
    int64_t p_slot,
    int64_t p_transition,
    const Array &p_payload,
    int64_t p_ignore_through
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? row->align_reseed(
                                p_transition,
                                state_row(p_payload, row->wiring.count()),
                                p_ignore_through
                            )
                          : predict::WritePlan();
}

bool NetwPredictionEngine::admit_post_reseed(int64_t p_slot, int64_t p_basis) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && row->admit_post_reseed(p_basis);
}

void NetwPredictionEngine::adopt_alignment(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_ignore_through
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->adopt_alignment(p_transition, p_ignore_through);
    }
}

bool NetwPredictionEngine::finish_probation(int64_t p_slot, bool p_corrected) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && row->finish_probation(p_corrected);
}

#define NETW_PREDICT_QUARANTINE_READ(m_name, m_member, m_type, m_absent) \
    m_type NetwPredictionEngine::m_name(int64_t p_slot) const { \
        const predict::Slot *row = row_of(p_slot); \
        return row != nullptr ? m_type(row->quarantine.m_member) : m_absent; \
    }

NETW_PREDICT_QUARANTINE_READ(quarantine_latched, latched, bool, false)
NETW_PREDICT_QUARANTINE_READ(quarantine_clean_run, clean_run, int, 0)
NETW_PREDICT_QUARANTINE_READ(quarantine_target, target, int, 0)
NETW_PREDICT_QUARANTINE_READ(reseed_align_pending, align_pending, bool, false)
NETW_PREDICT_QUARANTINE_READ(
    reseed_epoch_confirmed,
    epoch_confirmed,
    bool,
    false
)
NETW_PREDICT_QUARANTINE_READ(probation_pending, probation_pending, bool, false)
NETW_PREDICT_QUARANTINE_READ(reseed_ignore_through, ignore_through, int64_t, -1)

#undef NETW_PREDICT_QUARANTINE_READ

namespace {

Vector3 entity_position(const Ref<NetwEntity> &p_entity) {
    Node *root = p_entity.is_valid() ? p_entity->get_owner() : nullptr;
    Node3D *spatial = Object::cast_to<Node3D>(root);
    if (spatial != nullptr) {
        return spatial->get_global_position();
    }
    Node2D *flat = Object::cast_to<Node2D>(root);
    if (flat != nullptr) {
        const Vector2 point = flat->get_global_position();
        return Vector3(point.x, point.y, 0.0);
    }
    return Vector3();
}

bool entity_id_less(const Ref<NetwEntity> &p_a, const Ref<NetwEntity> &p_b) {
    const String a_id = String(p_a->get_entity_id());
    const String b_id = String(p_b->get_entity_id());
    if (a_id == b_id) {
        return p_a->get_instance_id() < p_b->get_instance_id();
    }
    return a_id < b_id;
}

bool valid_island_member(
    const Ref<NetwEntity> &p_owner,
    const Ref<NetwEntity> &p_member
) {
    if (p_member.is_null() || p_member == p_owner
        || p_member->get_owner() == nullptr || p_member->get_declares_scene()) {
        return false;
    }
    const Ref<NetwSceneHandle> mine = p_owner->get_scene();
    const Ref<NetwSceneHandle> theirs = p_member->get_scene();
    if (mine.is_null() || theirs.is_null()) {
        return false;
    }
    return p_member->get_multiplayer() == p_owner->get_multiplayer()
        && theirs->get_entity() == mine->get_entity();
}

bool can_automatically_simulate(const Ref<NetwEntity> &p_member) {
    const Ref<NetwPredictionHandle> handle = p_member->get_prediction();
    if (handle.is_null() || !handle->is_registered()) {
        return false;
    }
    return handle->get_sim_mode() == NetwPredict::SIM_MODE_DISPLAY
        || handle->simulated_by_count() > 0;
}

} // namespace

TypedArray<NetwEntity> NetwPredictionEngine::island_roster(
    int64_t p_slot,
    Object *p_session,
    const Ref<NetwPredictIsland> &p_island
) {
    TypedArray<NetwEntity> out;
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    if (owner.is_null() || p_island.is_null()) {
        return out;
    }
    LocalVector<Ref<NetwEntity>> found;
    HashSet<uint64_t> seen;
    const TypedArray<NetwEntity> declared = p_island->get_participants();
    for (int at = 0; at < declared.size(); ++at) {
        const Ref<NetwEntity> member = declared[at];
        if (valid_island_member(owner, member)
            && !seen.has(member->get_instance_id())) {
            seen.insert(member->get_instance_id());
            found.push_back(member);
        }
    }
    NetwMultiplayer *session = Object::cast_to<NetwMultiplayer>(p_session);
    const PackedStringArray layers = p_island->get_producers();
    for (int at = 0; session != nullptr && at < layers.size(); ++at) {
        const TypedArray<Object> shared
            = session->interest_shared_entities(owner, StringName(layers[at]));
        for (int seat = 0; seat < shared.size(); ++seat) {
            const Ref<NetwEntity> member
                = Ref<NetwEntity>(Object::cast_to<NetwEntity>(shared[seat]));
            if (valid_island_member(owner, member)
                && !seen.has(member->get_instance_id())) {
                seen.insert(member->get_instance_id());
                found.push_back(member);
            }
        }
    }
    std::sort(found.ptr(), found.ptr() + found.size(), entity_id_less);
    for (uint32_t at = 0; at < found.size(); ++at) {
        out.push_back(found[at]);
    }
    return out;
}

Ref<NetwEntity> NetwPredictionEngine::owner_entity(int64_t p_slot) const {
    const HashMap<int64_t, Ref<RefCounted>>::ConstIterator seated
        = entity_by_slot.find(p_slot);
    return seated != entity_by_slot.end()
        ? Ref<NetwEntity>(Object::cast_to<NetwEntity>(seated->value.ptr()))
        : Ref<NetwEntity>();
}

TypedArray<NetwEntity> NetwPredictionEngine::live_participants(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island
) {
    TypedArray<NetwEntity> seated = roster_list(p_slot, ROSTER_ISLAND_MEMBERS);
    if (!seated.is_empty() || p_island.is_null()) {
        return seated;
    }
    const TypedArray<NetwEntity> declared = p_island->get_participants();
    for (int at = 0; at < declared.size(); ++at) {
        const Ref<NetwEntity> participant = declared[at];
        if (participant.is_valid()) {
            seated.push_back(participant);
        }
    }
    return seated;
}

PackedStringArray NetwPredictionEngine::live_participant_ids(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island
) {
    NETW_ZONE_NC("predict live participant ids", colors::PREDICTION);
    const TypedArray<NetwEntity> seated = live_participants(p_slot, p_island);
    PackedStringArray ids;
    ids.resize(seated.size());
    String *names = ids.ptrw();
    for (int at = 0; at < seated.size(); ++at) {
        names[at] = String(Ref<NetwEntity>(seated[at])->get_entity_id());
    }
    return ids;
}

void NetwPredictionEngine::open_tenure(
    int64_t p_slot,
    const Ref<NetwEntity> &p_member,
    int64_t p_transition
) {
    roster_erase(p_slot, ROSTER_JOINT_LINGERING, p_member);
    predict::Slot *seat = mutable_row_of(slot_of(p_member));
    if (seat != nullptr) {
        seat->latches.tenure_begin = p_transition;
        seat->latches.tenure_end = -1;
    }
}

void NetwPredictionEngine::close_tenure(
    int64_t p_slot,
    const Ref<NetwEntity> &p_member,
    int64_t p_transition
) {
    predict::Slot *seat = mutable_row_of(slot_of(p_member));
    if (seat == nullptr) {
        return;
    }
    seat->latches.tenure_end = p_transition;
    roster_add(p_slot, ROSTER_JOINT_LINGERING, p_member);
}

namespace {

bool same_roster(
    const TypedArray<NetwEntity> &p_left,
    const TypedArray<NetwEntity> &p_right
) {
    if (p_left.size() != p_right.size()) {
        return false;
    }
    for (int at = 0; at < p_left.size(); ++at) {
        if (Ref<NetwEntity>(p_left[at]) != Ref<NetwEntity>(p_right[at])) {
            return false;
        }
    }
    return true;
}

PackedStringArray sorted_ids(const TypedArray<NetwEntity> &p_roster) {
    PackedStringArray ids;
    for (int at = 0; at < p_roster.size(); ++at) {
        const Ref<NetwEntity> member = p_roster[at];
        ids.push_back(String(member->get_entity_id()));
    }
    ids.sort();
    return ids;
}

} // namespace

void NetwPredictionEngine::publish_island_roster(int64_t p_slot) {
    Object *held = handle_of(p_slot);
    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(held);
    if (handle == nullptr) {
        return;
    }
    const Ref<NetwPredictStats> counters = handle->get_stats();
    if (counters.is_null()) {
        return;
    }
    counters->set(
        StringName("island_members"),
        sorted_ids(roster_list(p_slot, ROSTER_ISLAND_MEMBERS))
    );
    counters->set(
        StringName("simulated_members"),
        sorted_ids(roster_list(p_slot, ROSTER_SIMULATED))
    );
}

void NetwPredictionEngine::refresh_island_membership(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island,
    bool p_apply_promotion,
    const Callable &p_admit_reconcile
) {
    if (p_island.is_null() || !p_island->get_declared()) {
        clear_island_promotions(p_slot);
        roster_clear(p_slot, ROSTER_ISLAND_MEMBERS);
        publish_topology_roster(p_slot, p_island);
        return;
    }
    NetwMultiplayer *host = core();
    const TypedArray<NetwEntity> members
        = island_roster(p_slot, host, p_island);
    if (p_apply_promotion) {
        TypedArray<NetwEntity> promoted;
        if (host != nullptr) {
            promoted = island_commit_members(
                p_slot,
                members,
                p_island,
                drive_frontier(p_slot)
            );
        }
        if (apply_island_promotions(p_slot, p_island, promoted)) {
            publish_island_roster(p_slot);
        }
    } else {
        clear_island_promotions(p_slot);
    }

    const bool changed
        = !same_roster(members, roster_list(p_slot, ROSTER_ISLAND_MEMBERS));
    if (island_roster_seeded_of(p_slot) && changed) {
        NetwPredictionHandle *handle
            = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
        open_out_of_domain_window(
            p_slot,
            latest_input_tick_of(p_slot),
            handle != nullptr ? handle->get_collision_cooldown_ticks() : 0
        );
    }
    set_island_roster_seeded(p_slot, true);
    roster_assign(p_slot, ROSTER_ISLAND_MEMBERS, members);
    publish_topology_roster(p_slot, p_island);
    if (p_admit_reconcile.is_valid()) {
        p_admit_reconcile.call();
    }
    if (changed) {
        publish_island_roster(p_slot);
    }
}

void NetwPredictionEngine::clear_island_promotions(int64_t p_slot) {
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    const int64_t frontier = drive_frontier(p_slot);
    const TypedArray<NetwEntity> simulated
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < simulated.size(); ++at) {
        const Ref<NetwEntity> member = simulated[at];
        close_tenure(p_slot, member, frontier);
        const Ref<NetwPredictionHandle> handle = member->get_prediction();
        if (handle.is_valid()) {
            handle->set_simulated_by(owner, false, Callable());
        }
    }
    roster_clear(p_slot, ROSTER_SIMULATED);
}

bool NetwPredictionEngine::apply_island_promotions(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island,
    const TypedArray<NetwEntity> &p_promoted
) {
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    const int64_t frontier = drive_frontier(p_slot);
    const TypedArray<NetwEntity> standing
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < standing.size(); ++at) {
        const Ref<NetwEntity> member = standing[at];
        if (p_promoted.has(member)) {
            continue;
        }
        close_tenure(p_slot, member, frontier);
        const Ref<NetwPredictionHandle> handle = member->get_prediction();
        if (handle.is_valid()) {
            handle->set_simulated_by(owner, false, Callable());
        }
    }
    for (int at = 0; at < p_promoted.size(); ++at) {
        const Ref<NetwEntity> member = p_promoted[at];
        if (!standing.has(member)) {
            open_tenure(p_slot, member, frontier + 1);
        }
        const Ref<NetwPredictionHandle> handle = member->get_prediction();
        if (handle.is_valid()) {
            handle->set_simulated_by(
                owner,
                true,
                p_island.is_valid() ? p_island->predictor_of(member)
                                    : Callable()
            );
        }
    }
    bool changed = p_promoted.size() != standing.size();
    for (int at = 0; !changed && at < p_promoted.size(); ++at) {
        changed = !standing.has(p_promoted[at]);
    }
    roster_assign(p_slot, ROSTER_SIMULATED, p_promoted);
    return changed;
}

int NetwPredictionEngine::release_lingering(
    int64_t p_slot,
    int64_t p_floor_transition
) {
    int held = 0;
    const TypedArray<NetwEntity> lingering
        = roster_list(p_slot, ROSTER_JOINT_LINGERING);
    for (int at = 0; at < lingering.size(); ++at) {
        const Ref<NetwEntity> member = lingering[at];
        const predict::Slot *seat = row_of(slot_of(member));
        if (seat == nullptr || seat->latches.tenure_end < p_floor_transition) {
            roster_erase(p_slot, ROSTER_JOINT_LINGERING, member);
            continue;
        }
        held += 1;
    }
    return held;
}

void NetwPredictionEngine::publish_topology_roster(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island
) {
    set_island_participants(p_slot, live_participant_ids(p_slot, p_island));
}

bool NetwPredictionEngine::contact_is_equivalent(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island,
    bool p_has_witness
) {
    const TypedArray<NetwEntity> seated = live_participants(p_slot, p_island);
    if (seated.is_empty()) {
        return false;
    }
    const TypedArray<NetwEntity> touched
        = p_has_witness ? roster_list(p_slot, ROSTER_REALIZED_CONTACT) : seated;
    for (int at = 0; at < touched.size(); ++at) {
        const Ref<NetwEntity> participant = touched[at];
        if (!seated.has(participant)) {
            return false;
        }
        const Ref<NetwPredictionHandle> handle = participant->get_prediction();
        if (handle.is_null()
            || handle->get_sim_mode() == NetwPredict::SIM_MODE_DISPLAY) {
            return false;
        }
    }
    return true;
}

void NetwPredictionEngine::notify_contact(
    int64_t p_slot,
    const Ref<NetwPredictIsland> &p_island,
    bool p_has_witness,
    int p_cooldown_ticks
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || row->config.role != int(Role::PREDICT)) {
        return;
    }
    const int64_t authored = row->cursor.latest_input_tick;
    set_cooldown_until_tick(p_slot, authored + p_cooldown_ticks);
    if (!row->latches.island_gap_reported
        && live_participants(p_slot, p_island).is_empty()) {
        row->latches.island_gap_reported = true;
        const Ref<NetwEntity> seated = owner_entity(p_slot);
        NETW_WARN(
            sys::PREDICTION,
            "Prediction: %s reported contact with no declared island. Declare "
            "the bodies it touches on prediction.island, or its contact "
            "transitions will keep claiming an exactness they cannot hold.",
            String(seated.is_valid() ? seated->get_entity_id() : StringName())
                .utf8()
                .get_data()
        );
    }
    if (!contact_is_equivalent(p_slot, p_island, p_has_witness)) {
        open_out_of_domain_window(p_slot, authored, p_cooldown_ticks);
    }
}

TypedArray<NetwEntity> NetwPredictionEngine::island_commit_members(
    int64_t p_slot,
    const TypedArray<NetwEntity> &p_members,
    const Ref<NetwPredictIsland> &p_island,
    int64_t p_frontier
) {
    TypedArray<NetwEntity> promoted;
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    if (row_of(p_slot) == nullptr || owner.is_null() || p_island.is_null()
        || island_of(p_slot) == ISLAND_NONE) {
        return promoted;
    }
    LocalVector<Ref<NetwEntity>> ranked;
    ranked.reserve(uint32_t(p_members.size()) + 1);
    for (int at = 0; at < p_members.size(); ++at) {
        ranked.push_back(p_members[at]);
    }
    ranked.push_back(owner);
    std::sort(ranked.ptr(), ranked.ptr() + ranked.size(), entity_id_less);

    const Vector3 here = entity_position(owner);
    PackedInt64Array slots;
    PackedInt64Array order_keys;
    PackedInt32Array fidelities;
    PackedFloat64Array distances;
    PackedByteArray eligible;
    PackedByteArray contact;
    HashMap<int64_t, Ref<NetwEntity>> by_slot;
    int64_t owner_order_key = -1;
    for (uint32_t at = 0; at < ranked.size(); ++at) {
        if (ranked[at] == owner) {
            owner_order_key = int64_t(at);
            break;
        }
    }
    for (int at = 0; at < p_members.size(); ++at) {
        const Ref<NetwEntity> member = p_members[at];
        const int64_t row = slot_of(member);
        if (member.is_null() || row < 0 || row == p_slot || by_slot.has(row)) {
            continue;
        }
        int64_t key = -1;
        for (uint32_t seat = 0; seat < ranked.size(); ++seat) {
            if (ranked[seat] == member) {
                key = int64_t(seat);
                break;
            }
        }
        slots.push_back(row);
        order_keys.push_back(key);
        fidelities.push_back(p_island->fidelity_of(member));
        distances.push_back(here.distance_squared_to(entity_position(member)));
        eligible.push_back(can_automatically_simulate(member) ? 1 : 0);
        contact.push_back(
            roster_has(p_slot, ROSTER_REALIZED_CONTACT, member) ? 1 : 0
        );
        by_slot.insert(row, member);
    }
    const PackedInt64Array committed = island_commit(
        p_slot,
        owner_order_key,
        slots,
        order_keys,
        distances,
        fidelities,
        eligible,
        contact,
        p_island->get_promotion(),
        p_island->get_promotion_count(),
        p_island->get_promotion_meters(),
        p_frontier
    );
    for (int at = 0; at < committed.size(); ++at) {
        const HashMap<int64_t, Ref<NetwEntity>>::ConstIterator found
            = by_slot.find(committed[at]);
        if (found != by_slot.end()) {
            promoted.push_back(found->value);
        }
    }
    return promoted;
}

PackedInt64Array NetwPredictionEngine::island_commit(
    int64_t p_slot,
    int64_t p_owner_order_key,
    const PackedInt64Array &p_members,
    const PackedInt64Array &p_order_keys,
    const PackedFloat64Array &p_distance_squared,
    const PackedInt32Array &p_fidelities,
    const PackedByteArray &p_eligible,
    const PackedByteArray &p_contact,
    int p_promotion,
    int p_promotion_count,
    double p_promotion_meters,
    int64_t p_frontier
) {
    NETW_ZONE_NC("NetwPredictionEngine island commit", colors::PREDICTION);
    PackedInt64Array out;
    predict::Slot *owner = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        owner == nullptr || owner->config.island == ISLAND_NONE,
        out,
        sys::PREDICTION,
        "Island commit requires an open slot with a declared island."
    );
    const int count = p_members.size();
    NETW_ERR_COND_V(
        p_order_keys.size() != count || p_distance_squared.size() != count
            || p_fidelities.size() != count || p_eligible.size() != count
            || p_contact.size() != count,
        out,
        sys::PREDICTION,
        "Island columns must have the same length."
    );
    NETW_ERR_COND_V(
        p_promotion < int(predict::Promotion::NONE)
            || p_promotion > int(predict::Promotion::ALL),
        out,
        sys::PREDICTION,
        "Island promotion %d is outside the declared range.",
        p_promotion
    );
    NETW_ERR_COND_V(
        p_promotion_count < 0 || p_promotion_meters < 0.0
            || !std::isfinite(p_promotion_meters),
        out,
        sys::PREDICTION,
        "Island promotion budgets must be non-negative."
    );

    LocalVector<predict::IslandCandidate> candidates;
    candidates.resize(uint32_t(count));
    for (int at = 0; at < count; ++at) {
        const int64_t member_slot = p_members[at];
        const int64_t order_key = p_order_keys[at];
        predict::Slot *member = mutable_row_of(member_slot);
        NETW_ERR_COND_V(
            member == nullptr || member_slot == p_slot,
            out,
            sys::PREDICTION,
            "Island member slot %d is invalid.",
            member_slot
        );
        NETW_ERR_COND_V(
            owner->config.island == ISLAND_JOINT
                && !rerunnable(member->config.schedule),
            out,
            sys::PREDICTION,
            "Island member slot %d is not a re-runnable participant.",
            member_slot
        );
        NETW_ERR_COND_V(
            p_fidelities[at] < int(predict::Fidelity::UNDECLARED)
                || p_fidelities[at] > int(predict::Fidelity::SIMULATED),
            out,
            sys::PREDICTION,
            "Island fidelity %d is outside the declared range.",
            p_fidelities[at]
        );
        NETW_ERR_COND_V(
            p_distance_squared[at] < 0.0
                || !std::isfinite(p_distance_squared[at]),
            out,
            sys::PREDICTION,
            "Island distance squared must be non-negative."
        );
        NETW_ERR_COND_V(
            order_key == p_owner_order_key,
            out,
            sys::PREDICTION,
            "Island order keys must be unique."
        );
        for (int prior = 0; prior < at; ++prior) {
            NETW_ERR_COND_V(
                p_members[prior] == member_slot
                    || p_order_keys[prior] == order_key,
                out,
                sys::PREDICTION,
                "Island member slots and order keys must be unique."
            );
        }
        predict::IslandCandidate &candidate = candidates[uint32_t(at)];
        candidate.slot = member_slot;
        candidate.order_key = order_key;
        candidate.distance_squared = p_distance_squared[at];
        candidate.fidelity = predict::Fidelity(p_fidelities[at]);
        candidate.eligible = p_eligible[at] != 0;
        candidate.contact = p_contact[at] != 0;
    }

    owner->island.owner_order_key = p_owner_order_key;
    owner->island.promotion = predict::Promotion(p_promotion);
    owner->island.promotion_count = p_promotion_count;
    owner->island.promotion_meters = p_promotion_meters;
    owner->island.commit(candidates, p_frontier);
    for (uint32_t at = 0; at < owner->island.members.size(); ++at) {
        const predict::IslandMember &island_member = owner->island.members[at];
        predict::Slot *member = mutable_row_of(island_member.slot);
        if (member == nullptr) {
            continue;
        }
        member->joint.tenure = island_member.tenure;
        if (island_member.promoted) {
            out.push_back(island_member.slot);
        }
    }
    NETW_TRACE(
        sys::PREDICTION,
        "Committed island slot=%d members=%d promoted=%d lingering=%d.",
        p_slot,
        owner->island.present_count(),
        owner->island.promoted_count(),
        owner->island.lingering_count()
    );
    return out;
}

int NetwPredictionEngine::island_member_count(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->island.present_count() : 0;
}

bool NetwPredictionEngine::island_promoted(
    int64_t p_slot,
    int64_t p_member
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::IslandMember *member
        = row != nullptr ? row->island.member(p_member) : nullptr;
    return member != nullptr && member->promoted;
}

double NetwPredictionEngine::island_distance_squared(
    int64_t p_slot,
    int64_t p_member
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::IslandMember *member
        = row != nullptr ? row->island.member(p_member) : nullptr;
    return member != nullptr ? member->distance_squared : -1.0;
}

int64_t NetwPredictionEngine::tenure_begin(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->joint.tenure.begin : -1;
}

int64_t NetwPredictionEngine::tenure_end(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->joint.tenure.end : -1;
}

void NetwPredictionEngine::joint_record(
    int64_t p_slot,
    int64_t p_transition,
    const Array &p_state,
    const Variant &p_command,
    bool p_authored,
    bool p_relayed,
    bool p_predictor_valid
) {
    NETW_ZONE_NC("predict joint record", colors::PREDICTION);
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Joint record names a closed slot."
    );
    NETW_ERR_COND(
        p_transition < 0,
        sys::PREDICTION,
        "Joint record transition must be non-negative."
    );
    const predict::StateRow columns = state_row(p_state, row->wiring.count());
    {
        NETW_ZONE_NC("predict joint track record", colors::PREDICTION);
        row->joint.record(
            p_transition,
            columns,
            p_command,
            predict::joint_cell(p_authored, p_relayed, p_predictor_valid)
        );
    }
}

void NetwPredictionEngine::joint_note_basis(
    int64_t p_slot,
    int64_t p_basis,
    int p_source
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr || row->config.island == ISLAND_NONE,
        sys::PREDICTION,
        "Joint floor move requires an open slot with a declared island."
    );
    NETW_ERR_COND(
        p_basis < 0 || p_source < JOINT_FLOOR_ACK
            || p_source > JOINT_FLOOR_EPOCH,
        sys::PREDICTION,
        "Joint floor source or basis is outside the declared range."
    );
    predict::JointTrack &track = row->joint;
    predict::JointStats &stats = row->joint_stats;
    if (p_source == JOINT_FLOOR_RELAY) {
        if (track.relay_floor < 0 || p_basis < track.relay_floor) {
            track.relay_floor = p_basis;
            stats.floor_relay_moves += 1;
        }
    } else if (p_source == JOINT_FLOOR_EPOCH) {
        if (p_basis > track.epoch_floor) {
            track.epoch_floor = p_basis;
            stats.floor_epoch_moves += 1;
        }
    } else if (p_basis > track.basis) {
        track.basis = p_basis;
        if (p_source == JOINT_FLOOR_ACK) {
            stats.floor_ack_moves += 1;
        } else {
            stats.floor_state_moves += 1;
        }
    }
}

void NetwPredictionEngine::joint_clear(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->joint.clear_cells();
    }
}

Variant NetwPredictionEngine::joint_command_at(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::JointCommandRecord *cell
        = row != nullptr ? row->joint.command_at(p_transition) : nullptr;
    return cell != nullptr ? cell->command : Variant();
}

int NetwPredictionEngine::joint_provenance_at(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::JointCommandRecord *cell
        = row != nullptr ? row->joint.command_at(p_transition) : nullptr;
    return cell != nullptr ? int(cell->provenance) : -1;
}

predict::JointPassPlan NetwPredictionEngine::joint_pass(
    int64_t p_slot,
    int64_t p_present
) {
    NETW_ZONE_NC("NetwPredictionEngine joint pass", colors::PREDICTION);
    predict::JointPassPlan plan;
    predict::Slot *owner = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        owner == nullptr || owner->config.island != ISLAND_JOINT
            || !rerunnable(owner->config.schedule),
        plan,
        sys::PREDICTION,
        "Joint pass requires an open JOINT owner on a re-runnable schedule."
    );
    NETW_ERR_COND_V(
        p_present < 0,
        plan,
        sys::PREDICTION,
        "Joint pass present transition must be non-negative."
    );

    struct PassMember {
        predict::Slot *row = nullptr;
        int64_t slot = 0;
        int64_t order_key = 0;
    };
    LocalVector<PassMember> members;
    PassMember subject;
    subject.row = owner;
    subject.slot = p_slot;
    subject.order_key = owner->island.owner_order_key;
    members.push_back(subject);
    for (uint32_t at = 0; at < owner->island.members.size(); ++at) {
        const predict::IslandMember &island_member = owner->island.members[at];
        if (!island_member.promoted && !island_member.lingering) {
            continue;
        }
        predict::Slot *member = mutable_row_of(island_member.slot);
        NETW_WARN_COND(
            member == nullptr,
            sys::PREDICTION,
            "Joint member slot %d closed before its linger completed.",
            island_member.slot
        );
        if (member == nullptr) {
            continue;
        }
        PassMember pass_member;
        pass_member.row = member;
        pass_member.slot = island_member.slot;
        pass_member.order_key = island_member.order_key;
        members.push_back(pass_member);
    }
    struct ByOrder {
        bool operator()(
            const PassMember &p_left,
            const PassMember &p_right
        ) const {
            return p_left.order_key < p_right.order_key;
        }
    };
    members.sort_custom<ByOrder>();

    bool moved = false;
    int64_t epoch_floor = -1;
    int64_t history_floor = p_present - predict::ACK_AGE_MAX;
    LocalVector<int64_t> bases;
    LocalVector<int64_t> relay_floors;
    for (uint32_t at = 0; at < members.size(); ++at) {
        const predict::JointTrack &track = members[at].row->joint;
        moved = moved || track.moved();
        bases.push_back(track.basis);
        relay_floors.push_back(track.relay_floor);
        epoch_floor = std::max(epoch_floor, track.epoch_floor);
        history_floor = std::max(history_floor, track.history_floor());
    }
    if (!moved) {
        return plan;
    }

    predict::JointFloorDecision decision = predict::joint_floor(
        bases,
        relay_floors,
        epoch_floor,
        history_floor,
        p_present
    );
    const auto release_expired = [&](int64_t p_floor) {
        owner->island.release_lingering(p_floor);
        for (uint32_t at = 0; at < members.size();) {
            const bool expired = members[at].slot != p_slot
                && members[at].row->joint.tenure.end >= 0
                && members[at].row->joint.tenure.end < p_floor;
            if (expired) {
                members[at].row->joint.clear_floors();
                members.remove_at(at);
            } else {
                at += 1;
            }
        }
    };
    release_expired(decision.floor);
    if (!decision.heal) {
        for (uint32_t at = 0; at < members.size(); ++at) {
            if (members[at].row->joint.state_at(decision.floor) == nullptr) {
                decision.floor = p_present;
                decision.heal = true;
                break;
            }
        }
    }
    if (decision.heal) {
        release_expired(decision.floor);
    }

    plan.floor = decision.floor;
    plan.present = p_present;
    plan.heal = decision.heal;
    LocalVector<int> depths;
    depths.resize(members.size());
    for (uint32_t at = 0; at < depths.size(); ++at) {
        depths[at] = 0;
    }
    for (uint32_t at = 0; at < members.size(); ++at) {
        predict::JointTrack &track = members[at].row->joint;
        const predict::JointStateRecord *state_record = nullptr;
        if (decision.heal) {
            state_record = track.newest_state_at_or_before(p_present);
        }
        const predict::StateRow *state = decision.heal
            ? (state_record != nullptr ? &state_record->state : nullptr)
            : track.state_at(decision.floor);
        if (state == nullptr) {
            NETW_WARN(
                sys::PREDICTION,
                "Joint pass slot=%d has no retained state for member=%d.",
                p_slot,
                members[at].slot
            );
            for (uint32_t clear_at = 0; clear_at < members.size(); ++clear_at) {
                members[clear_at].row->joint.clear_floors();
            }
            return plan;
        }
        predict::JointRestore restore;
        restore.slot = members[at].slot;
        restore.state = *state;
        plan.restores.push_back(restore);
        members[at].row->set_state(*state);
    }
    NETW_ASSERT(
        plan.restores.size() == members.size(),
        sys::PREDICTION,
        "A joint pass must restore every member."
    );

    predict::JointStats &stats = owner->joint_stats;
    if (!decision.heal) {
        for (int64_t transition = decision.floor + 1; transition <= p_present;
             ++transition) {
            for (uint32_t at = 0; at < members.size(); ++at) {
                const predict::JointTrack &track = members[at].row->joint;
                if (!track.tenure.contains(transition)) {
                    continue;
                }
                const predict::JointCommandRecord *command
                    = track.command_at(transition);
                predict::JointStep step;
                step.slot = members[at].slot;
                step.transition = transition;
                if (command != nullptr) {
                    step.command = command->command;
                    step.provenance = command->provenance;
                }
                plan.steps.push_back(step);
                depths[at] += 1;
                stats.cells_relayed
                    += step.provenance == predict::CellProvenance::RELAYED ? 1
                                                                           : 0;
                stats.cells_substituted
                    += step.provenance == predict::CellProvenance::SUBSTITUTED
                    ? 1
                    : 0;
            }
        }
        stats.passes += 1;
    } else {
        stats.heal_snaps += 1;
    }
    for (uint32_t at = 0; at < depths.size(); ++at) {
        stats.max_depth = std::max(stats.max_depth, depths[at]);
    }
    stats.members = int(plan.restores.size());
    stats.floor = plan.floor;
    stats.present = plan.present;
    plan.valid = true;

    stats.linger_held = owner->island.lingering_count();
    for (uint32_t at = 0; at < members.size(); ++at) {
        members[at].row->joint.clear_floors();
    }
    NETW_TRACE(
        sys::PREDICTION,
        "Joint pass slot=%d floor=%d present=%d members=%d heal=%d.",
        p_slot,
        plan.floor,
        plan.present,
        stats.members,
        plan.heal ? 1 : 0
    );
    return plan;
}

PackedInt64Array NetwPredictionEngine::joint_stats(int64_t p_slot) const {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(STAT_JOINT_COUNT);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::JointStats &stats = row->joint_stats;
    int64_t *values = out.ptrw();
    values[STAT_JOINT_PASSES] = stats.passes;
    values[STAT_JOINT_MEMBERS] = stats.members;
    values[STAT_JOINT_CELLS_RELAYED] = stats.cells_relayed;
    values[STAT_JOINT_CELLS_SUBSTITUTED] = stats.cells_substituted;
    values[STAT_JOINT_HEAL_SNAPS] = stats.heal_snaps;
    values[STAT_JOINT_LINGER_HELD] = stats.linger_held;
    values[STAT_JOINT_FLOOR] = stats.floor;
    values[STAT_JOINT_PRESENT] = stats.present;
    values[STAT_JOINT_MAX_DEPTH] = stats.max_depth;
    values[STAT_JOINT_FLOOR_ACK_MOVES] = stats.floor_ack_moves;
    values[STAT_JOINT_FLOOR_STATE_MOVES] = stats.floor_state_moves;
    values[STAT_JOINT_FLOOR_RELAY_MOVES] = stats.floor_relay_moves;
    values[STAT_JOINT_FLOOR_EPOCH_MOVES] = stats.floor_epoch_moves;
    return out;
}

#define NETW_PREDICT_JOURNAL_READ(m_name, m_call, m_type, m_absent) \
    m_type NetwPredictionEngine::m_name(int64_t p_slot, int p_index) const { \
        const predict::Slot *row = row_of(p_slot); \
        return row != nullptr ? m_type(row->journal.m_call(p_index)) \
                              : m_absent; \
    }

NETW_PREDICT_JOURNAL_READ(journal_transition_at, transition_at, int64_t, -1)
NETW_PREDICT_JOURNAL_READ(journal_label_at, label_at, int64_t, -1)
NETW_PREDICT_JOURNAL_READ(journal_kind_at, kind_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_c_hash_at, c_hash_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(journal_pre_fp_at, pre_fp_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(journal_post_fp_at, post_fp_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(journal_environment_at, e_digest_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(journal_topology_at, topo_fp_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(journal_raw_at, raw_fp_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(journal_witness_at, witness_fp_at, int64_t, 0)
NETW_PREDICT_JOURNAL_READ(
    journal_witness_class_at,
    witness_class_bits_at,
    int,
    0
)
NETW_PREDICT_JOURNAL_READ(journal_episode_id_at, episode_id_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_write_id_at, write_id_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_operator_at, operator_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_basis_at, basis_at, int64_t, -1)
NETW_PREDICT_JOURNAL_READ(journal_evidence_at, evidence_mask_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_flags_at, flags_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_domain_at, domain_at, int, 0)
NETW_PREDICT_JOURNAL_READ(journal_attribution_at, attribution_at, int, 0)
NETW_PREDICT_JOURNAL_READ(
    journal_differing_family_at,
    differing_family_at,
    int,
    0
)
NETW_PREDICT_JOURNAL_READ(
    journal_aligned_error_at,
    aligned_error_at,
    double,
    0.0
)

#undef NETW_PREDICT_JOURNAL_READ

PackedInt32Array NetwPredictionEngine::journal_pre_families_at(
    int64_t p_slot,
    int p_index
) const {
    PackedInt32Array out = gd::zeroed<PackedInt32Array>(3);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::FamilyFingerprints families
        = row->journal.pre_families_at(p_index);
    int32_t *values = out.ptrw();
    values[0] = families.pose;
    values[1] = families.momentum;
    values[2] = families.controller;
    return out;
}

PackedInt32Array NetwPredictionEngine::journal_post_families_at(
    int64_t p_slot,
    int p_index
) const {
    PackedInt32Array out = gd::zeroed<PackedInt32Array>(3);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::FamilyFingerprints families
        = row->journal.post_families_at(p_index);
    int32_t *values = out.ptrw();
    values[0] = families.pose;
    values[1] = families.momentum;
    values[2] = families.controller;
    return out;
}

int NetwPredictionEngine::journal_size(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.size() : 0;
}

Dictionary NetwPredictionEngine::field_divergence(
    int64_t p_slot,
    const predict::StateVerdict &p_verdict
) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (uint32_t at = 0; at < p_verdict.all_field_errors.size(); ++at) {
        if (p_verdict.all_field_errors[at] < 0.0) {
            continue;
        }
        if (int(at) >= row->wiring.codec.count()) {
            break;
        }
        out[row->wiring.codec.keys[at]] = p_verdict.all_field_errors[at];
    }
    return out;
}

int NetwPredictionEngine::transition_span(
    int64_t p_slot,
    int64_t p_basis
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->transition_span(p_basis) : 0;
}

Ref<NetwPredictJournal> NetwPredictionEngine::journal_snapshot(
    int64_t p_slot
) const {
    Ref<NetwPredictJournal> out;
    out.instantiate();
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    out->adopt(row->journal, row->witness_details.rows());
    return out;
}

void NetwPredictionEngine::bind_session(NetwMultiplayer *p_core) {
    core_id = gd::instance_id(p_core);
}

NetwMultiplayer *NetwPredictionEngine::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

void NetwPredictionEngine::report_predict(
    int64_t p_slot,
    int64_t p_event,
    const Dictionary &p_detail,
    const Dictionary &p_model
) {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (host == nullptr || seated.is_null()) {
        return;
    }
    const int64_t route = host->liveness_route_of(seated.ptr());
    if (!host->event_wants(p_event, route)) {
        return;
    }
    host->event_emit(
        p_event,
        route,
        p_detail,
        seated->get_entity_id(),
        0,
        OK,
        p_model
    );
}

Object *NetwPredictionEngine::handle_of(int64_t p_slot) const {
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    return seated.is_valid() ? seated->get_prediction().ptr() : nullptr;
}

StringName NetwPredictionEngine::episode_edge(int64_t p_event) {
    if (p_event == EventPlane::EPISODE_OPEN) {
        return StringName("episode_opened");
    }
    if (p_event == EventPlane::EPISODE_CLOSE) {
        return StringName("episode_closed");
    }
    if (p_event == EventPlane::EPISODE_FALLBACK) {
        return StringName("episode_fallback");
    }
    return StringName();
}

void NetwPredictionEngine::report_consume(
    int64_t p_slot,
    int64_t p_depth,
    int64_t p_buffer,
    int64_t p_action
) {
    NetwMultiplayer *host = core();
    if (host == nullptr || !host->event_wants(EventPlane::PREDICT_CONSUME, 0)) {
        return;
    }
    Dictionary detail;
    detail[StringName("depth")] = p_depth;
    detail[StringName("buffer")] = p_buffer;
    detail[StringName("action")] = p_action;
    report_predict(p_slot, EventPlane::PREDICT_CONSUME, detail, Dictionary());
}

void NetwPredictionEngine::announce_episode(
    int64_t p_slot,
    int64_t p_event,
    const Dictionary &p_extra
) {
    const StringName edge = episode_edge(p_event);
    if (edge == StringName()) {
        return;
    }
    const Dictionary report = episode_record(p_slot);
    Object *held = handle_of(p_slot);
    if (held != nullptr) {
        held->emit_signal(edge, report);
    }
    const Dictionary generator
        = report.get(StringName("generator"), Dictionary());
    Dictionary detail;
    detail[StringName("transition")]
        = generator.get(StringName("transition"), -1);
    detail[StringName("attribution")] = generator.get(
        StringName("boundary"),
        int(predict::Attribution::UNKNOWN)
    );
    detail.merge(p_extra, true);
    report_predict(p_slot, p_event, detail, report);
}

bool NetwPredictionEngine::announce_recovered(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_attribution,
    const Dictionary &p_before,
    const Dictionary &p_correction_write
) {
    const Dictionary deltas
        = write_deltas(p_slot, p_before, p_correction_write);
    if (deltas.is_empty()) {
        return false;
    }
    ledger_note_writes(p_slot, deltas);
    const bool teleported = last_correction_teleported_of(p_slot);
    Object *held = handle_of(p_slot);
    if (held != nullptr) {
        held->emit_signal(
            StringName("recovered"),
            p_transition,
            deltas,
            teleported,
            p_attribution
        );
    }
    Dictionary detail;
    detail[StringName("transition")] = p_transition;
    detail[StringName("attribution")] = p_attribution;
    detail[StringName("teleport")] = teleported;
    detail[StringName("moved")] = deltas;
    report_predict(p_slot, EventPlane::RECOVERY, detail, Dictionary());
    return true;
}

void NetwPredictionEngine::announce_divergence(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_attribution,
    double p_divergence
) {
    Object *held = handle_of(p_slot);
    if (held != nullptr) {
        held->emit_signal(
            StringName("divergence_detected"),
            p_transition,
            p_attribution
        );
    }
    Dictionary detail;
    detail[StringName("transition")] = p_transition;
    detail[StringName("attribution")] = p_attribution;
    detail[StringName("divergence")] = p_divergence;
    report_predict(p_slot, EventPlane::DIVERGENCE, detail, Dictionary());
}

namespace {

Ref<Script> declaring_script(const Ref<NetwEntity> &p_entity) {
    Node *owner = p_entity.is_valid() ? p_entity->get_owner() : nullptr;
    return owner != nullptr ? Ref<Script>(owner->get_script()) : Ref<Script>();
}

Ref<NetwPropertySet> broadcast_set_of(const Ref<NetwEntity> &p_entity) {
    const Ref<Script> script = declaring_script(p_entity);
    return script.is_valid() ? property_set_builder::from_script(
                                   script,
                                   NetwPropertySet::RECORD_BROADCAST,
                                   nullptr,
                                   nullptr
                               )
                             : Ref<NetwPropertySet>();
}

} // namespace

namespace {

bool declared_angle(Node *p_node, const StringName &p_key) {
    if (p_node == nullptr) {
        return false;
    }
    const Ref<NetwInterpolate> spec
        = netw::script::model::get_node_property_interpolator(p_node, p_key);
    return spec.is_valid() && spec->get_mode() == NetwInterpolate::MODE_ANGLE;
}

int64_t declared_type(Node *p_node, const StringName &p_key) {
    return p_node != nullptr
        ? netw::script::model::get_node_property_type(p_node, p_key)
        : int64_t(Variant::NIL);
}

LocalVector<predict::FieldDecl> field_declaration(
    const Ref<NetwPropertySetBinding> &p_binding
) {
    LocalVector<predict::FieldDecl> out;
    const Ref<NetwPropertySet> declared
        = p_binding.is_valid() ? p_binding->get_set() : Ref<NetwPropertySet>();
    if (declared.is_null()) {
        return out;
    }
    Node *node = p_binding->node();
    const TypedArray<NetwPropertySetColumn> columns = declared->get_columns();
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        const StringName key = column->get_key();
        out.push_back(field_decl(
            key,
            int(column->get_property_class()),
            p_binding->carry_channel_of(key),
            p_binding->converge_stiffness_of(key),
            p_binding->teleport_only_of(key),
            p_binding->reconcile_only_of(key),
            p_binding->epsilon_override_of(key),
            p_binding->teleport_at_of(key),
            declared_angle(node, key),
            column->get_quantizer(),
            int(declared_type(node, key))
        ));
    }
    return out;
}

} // namespace

int64_t NetwPredictionEngine::current_tick() const {
    NetwMultiplayer *host = core();
    return host != nullptr ? host->clock_engine().get_tick() : -1;
}

void NetwPredictionEngine::report_quantum_declaration(int64_t p_slot) {
    NetwMultiplayer *host = core();
    if (host == nullptr) {
        return;
    }
    const ClockEngine &clock = host->clock_engine();
    const Engine *engine = Engine::get_singleton();
    const int steps
        = engine != nullptr ? engine->get_physics_ticks_per_second() : 60;
    const int64_t declared = int64_t(clock.get_tickrate()) * 1000LL + steps;
    if (declared == quantum_reported) {
        return;
    }
    quantum_reported = declared;
    const double factor = clock.physics_factor();
    if (Math::is_equal_approx(factor, Math::round(factor))) {
        return;
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    NETW_ERROR(
        sys::PREDICTION,
        "Prediction: %s drives a solver body at %d physics steps per second "
        "against a tickrate of %d, so one transition costs %.3f steps. The "
        "physics server runs exactly one step per frame, so a fractional cost "
        "alternates on a phase each peer keeps privately and the two never "
        "advance the same simulated time. Make physics_ticks_per_second a "
        "whole multiple of tickrate.",
        String(
            seated.is_valid() ? seated->get_entity_id() : StringName("entity")
        )
            .utf8()
            .get_data(),
        steps,
        clock.get_tickrate(),
        factor
    );
}

Ref<NetwTimeline> NetwPredictionEngine::register_timeline(
    const Ref<NetwEntity> &p_entity
) {
    NetwMultiplayer *host = core();
    if (host == nullptr || p_entity.is_null()) {
        return Ref<NetwTimeline>();
    }
    NetwLagCompCore *registry = host->get_lagcomp_core();
    const int64_t slot
        = registry->timeline_register(p_entity, NetwTimeline::DEFAULT_LIMIT);
    return slot >= 0 ? registry->timeline_history(slot) : Ref<NetwTimeline>();
}

int NetwPredictionEngine::adopt_declaration(
    const Ref<NetwEntity> &p_entity,
    const Ref<NetwPropertySetBinding> &p_state,
    const Ref<NetwPropertySetBinding> &p_input,
    int p_schedule,
    int p_role,
    int p_declared_correction,
    int p_restore,
    int p_max_restore_ticks,
    int p_island,
    bool p_witness
) {
    const int64_t slot = slot_of(p_entity);
    const LocalVector<predict::FieldDecl> declared = field_declaration(p_state);
    if (slot < 0 || declared.is_empty()) {
        return -1;
    }
    rewire(slot, declared, field_declaration(p_input));

    Node *node = p_state->node();
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(slot));
    if (node != nullptr && handle != nullptr) {
        bind_owner(slot, node);
        if (!handle->get_simulate().is_valid()) {
            Node *root = p_entity->get_owner();
            if (root != nullptr
                && root->has_method(StringName("_network_tick"))) {
                handle->set_simulate(
                    Callable(root, StringName("_network_tick"))
                );
            }
        }
        NetwMultiplayer *host = core();
        const int64_t route
            = host != nullptr ? host->liveness_route_of(p_entity.ptr()) : -1;
        set_order_key(slot, route > 0 ? route : -1);
        set_simulate(slot, handle->get_simulate());
        set_witness(slot, handle->get_witness_contacts());
        set_corridor(slot, handle->get_transport_corridor());
        const Dictionary sensors = handle->get_sensors();
        const Array named = sensors.keys();
        for (int at = 0; at < named.size(); ++at) {
            set_sensor(slot, named[at], sensors[named[at]]);
        }
    }

    const int correction = resolve_correction(slot, p_declared_correction);
    if (!configure(
            slot,
            p_schedule,
            p_role,
            correction,
            p_restore,
            p_max_restore_ticks,
            p_island,
            p_witness
        )) {
        return -1;
    }
    return correction;
}

Dictionary NetwPredictionEngine::capture_current(int64_t p_slot) {
    const Ref<NetwPropertySetBinding> binding = state_binding_of(p_slot);
    const Dictionary raw = owner_bound(p_slot) ? capture_state(p_slot)
        : binding.is_valid()                   ? binding->snapshot_payload()
                                               : Dictionary();
    return canonicalize_state(p_slot, raw);
}

Dictionary NetwPredictionEngine::recovery_before_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->rows.recovery_before : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_write_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->rows.recovery_write : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_projection_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->rows.recovery_projection : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_carried_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->rows.recovery_carried : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_tier_errors_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->rows.recovery_tier_errors : Dictionary();
}

predict::WritePlan NetwPredictionEngine::plan_recovery(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_ack_label,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    bool p_escalated,
    int p_domain,
    int p_attribution
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return predict::WritePlan();
    }
    const double epsilon = handle->get_divergence_epsilon();
    const double teleport = handle->get_teleport_threshold();
    const int ack_age = handle->get_ack_age_ticks();

    Dictionary epsilon_overrides;
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (row->wiring.epsilon[uint32_t(at)] >= 0.0) {
            epsilon_overrides[row->wiring.fields.name_at(at)]
                = row->wiring.epsilon[uint32_t(at)];
        }
    }
    row->rows.recovery_projection = prediction_core::guard_projection(
        projection_map(p_slot),
        divergence_report(p_slot),
        row->config.epsilon,
        epsilon_overrides,
        row->config.max_restore_ticks,
        ack_age,
        row->cursor.tick_delta
    );
    row->rows.recovery_carried
        = carry_payload(p_slot, p_payload, p_ack, teleport, epsilon);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return predict::WritePlan();
    }
    row->rows.recovery_tier_errors = p_escalated
        ? Dictionary()
        : pose_errors_against(p_slot, p_payload, ack_age);
    note_tier_errors(p_slot, row->rows.recovery_tier_errors);

    const bool suppressed = handle->get_sleeping() || probation_pending(p_slot)
        || latest_input_tick_of(p_slot) < cooldown_until_tick_of(p_slot);
    return recover_for(
        p_slot,
        p_ack,
        p_ack_label,
        p_predicted,
        row->rows.recovery_carried,
        row->rows.recovery_before,
        row->rows.recovery_tier_errors,
        handle->resolved_recovery_policy(),
        epsilon,
        teleport,
        handle->get_max_restore_ticks(),
        ack_age,
        handle->get_collision_cooldown_ticks(),
        p_domain,
        p_attribution,
        suppressed
    );
}

void NetwPredictionEngine::apply_recovery_plan(
    int64_t p_slot,
    const RecoveryPlan &p_plan,
    int64_t p_ack,
    bool p_pool_planned
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return;
    }
    const bool projected = !row->rows.recovery_projection.is_empty();
    NetwMultiplayer *host = core();
    if (host != nullptr && host->event_wants(EventPlane::PREDICT_RECOVER, 0)) {
        Dictionary detail;
        detail[StringName("transition")] = p_ack;
        detail[StringName("skip")] = p_plan.skip;
        detail[StringName("teleport")] = p_plan.teleport;
        report_predict(
            p_slot,
            EventPlane::PREDICT_RECOVER,
            detail,
            Dictionary()
        );
    }
    set_last_correction_teleported(p_slot, p_plan.teleport);
    if (p_plan.skip) {
        note_verdict_reason(p_slot, NetwPredict::VERDICT_REASON_DECLINED);
        return;
    }
    int op = int(predict::Operator::REBASE_EXACT);
    if (p_plan.teleport) {
        op = int(predict::Operator::FULL_CLOSURE);
    } else if (
        row->config.correction == int(CorrectionMode::SNAP)
        && handle->get_snap_restore() == NetwPredict::RESTORE_MODE_EXTRAPOLATED
        && projected
    ) {
        op = int(predict::Operator::REBASE_PROJECTED);
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (apply_restore(
            p_slot,
            p_plan.restore,
            op,
            p_ack,
            p_slot,
            seated.is_valid() ? seated->get_entity_id() : StringName(),
            handle->get_ack_age_ticks(),
            handle->get_divergence_epsilon(),
            false,
            p_pool_planned
        )) {
        if (!p_pool_planned) {
            sync_episode(p_slot);
        }
    } else {
        NETW_ERROR(sys::PREDICTION, "Prediction restore effect is invalid.");
    }
    row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->rows.recovery_write = p_plan.write;
    }
}

int NetwPredictionEngine::open_recovery(
    int64_t p_slot,
    int64_t p_ack,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    int p_meter,
    int p_domain
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return RECOVERY_PLAN;
    }
    row->config.correction
        = resolve_correction(p_slot, handle->get_correction_mode());
    const Dictionary before = capture_current(p_slot);
    row->rows.recovery_before = before;
    row->rows.recovery_write = Dictionary();

    const Ref<NetwEntity> seated = owner_entity(p_slot);
    const double epsilon = handle->get_divergence_epsilon();
    const int policy = handle->resolved_recovery_policy();
    const bool escalated = escalation_pending(p_slot);

    Dictionary carried;
    const PackedInt64Array stats = episode_stats(p_slot);
    if (handle->get_transport_corridor().is_valid()
        && stats[STAT_EPISODE_ACTIVE] != 0
        && stats[STAT_EPISODE_TRANSPORT_DECIDED] == 0) {
        carried = try_transport(
            p_slot,
            p_predicted,
            p_payload,
            before,
            p_ack,
            escalated,
            handle->get_transport_corridor(),
            handle->get_teleport_threshold(),
            epsilon,
            policy
        );
        sync_episode(p_slot);
    }

    if (carried.is_empty()) {
        const int decision = try_dissipate(
            p_slot,
            p_ack,
            p_meter,
            p_domain,
            escalated,
            epsilon,
            policy == NetwPredict::RECOVERY_POLICY_OBSERVE,
            seated.is_valid() ? seated->get_entity_id() : StringName("body"),
            handle->get_ack_age_ticks()
        );
        if (decision != DISSIPATE_UNDECIDED) {
            sync_episode(p_slot);
        }
        if (decision == DISSIPATE_APPLIED) {
            note_verdict_reason(p_slot, NetwPredict::VERDICT_REASON_DISSIPATED);
            return RECOVERY_DISSIPATED;
        }
    }

    note_reconciling(p_slot, true);
    const Ref<NetwPredictStats> counters = handle->get_stats();
    if (counters.is_valid()) {
        counters->set(
            StringName("corrections"),
            int64_t(counters->get(StringName("corrections"))) + 1
        );
    }
    if (carried.is_empty()) {
        return RECOVERY_PLAN;
    }

    const Dictionary restore_row
        = carried.get(StringName("restore"), Dictionary());
    set_last_correction_teleported(p_slot, false);
    if (apply_restore(
            p_slot,
            restore_row,
            int(predict::Operator::TRANSPORT_DELTA),
            p_ack,
            p_slot,
            seated.is_valid() ? seated->get_entity_id() : StringName(),
            handle->get_ack_age_ticks(),
            epsilon,
            false,
            false
        )) {
        sync_episode(p_slot);
    } else {
        NETW_ERROR(sys::PREDICTION, "Prediction restore effect is invalid.");
    }
    row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->rows.recovery_write = restore_row;
    }
    return RECOVERY_TRANSPORTED;
}

predict::Feed NetwPredictionEngine::role_feed(
    int p_role,
    bool p_fallback_latched,
    const Callable &p_on_state,
    const Callable &p_on_input,
    const Callable &p_on_simulated,
    const Callable &p_on_quarantine
) const {
    predict::Feed feed;
    switch (p_role) {
        case int(Role::PREDICT):
            feed.input_volatile_external = true;
            feed.state_write_gate = false;
            feed.state_on_applied = p_on_state;
            break;
        case int(Role::CONSUME):
            feed.input_write_gate = false;
            feed.input_on_applied = p_on_input;
            break;
        case int(Role::SIMULATE):
            feed.input_volatile_external = true;
            feed.state_write_gate = false;
            feed.state_on_applied = p_on_simulated;
            break;
        case int(Role::REMOTE):
            if (p_fallback_latched) {
                feed.input_volatile_external = true;
                feed.state_on_applied = p_on_quarantine;
            }
            break;
        default:
            break;
    }
    return feed;
}

void NetwPredictionEngine::push_carry_rules(int64_t p_slot) {
    const Ref<NetwPropertySetBinding> binding = state_binding_of(p_slot);
    Node *node = binding.is_valid() ? binding->node() : nullptr;
    const Ref<NetwPropertySet> declared
        = binding.is_valid() ? binding->get_set() : Ref<NetwPropertySet>();
    if (node == nullptr || declared.is_null()) {
        return;
    }
    const TypedArray<NetwPropertySetColumn> columns = declared->get_columns();
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        const StringName key = column->get_key();
        const Callable step
            = netw::script::model::get_node_property_carry(node, key);
        if (!step.is_valid()) {
            continue;
        }
        if (projection_of(p_slot, field_slot(p_slot, key)) >= 0) {
            NETW_ERROR(
                sys::PREDICTION,
                "Prediction: '%s' declares both carry_along() and "
                "carry_step(). One field, one forward model: keep the step "
                "for a rate that varies across the acknowledgement window, "
                "the channel for one that holds still. The channel is being "
                "used.",
                String(key).utf8().get_data()
            );
            continue;
        }
        set_carry(p_slot, key, step);
    }
}

void NetwPredictionEngine::report_authority_model(int64_t p_slot) {
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (seated.is_null()) {
        return;
    }
    const Ref<NetwPropertySet> broadcast = broadcast_set_of(seated);
    if (broadcast.is_valid() && !broadcast->get_columns().is_empty()) {
        PackedStringArray keys;
        const TypedArray<NetwPropertySetColumn> columns
            = broadcast->get_columns();
        for (int at = 0; at < columns.size(); ++at) {
            const Ref<NetwPropertySetColumn> column = columns[at];
            keys.push_back(String(column->get_key()));
        }
        NETW_WARN(
            sys::PREDICTION,
            "Prediction: %s declares broadcast() fields [%s] and declares "
            "prediction. A broadcast field trusts its owner outright, so "
            "predicting the entity that owns it is a contradiction. Mark the "
            "fields state(), or drop the prediction block.",
            String(seated->get_entity_id()).utf8().get_data(),
            String(", ").join(keys).utf8().get_data()
        );
    }
    const Ref<NetwPropertySetBinding> input = input_binding_of(p_slot);
    const Ref<NetwPropertySet> declared
        = input.is_valid() ? input->get_set() : Ref<NetwPropertySet>();
    if (declared.is_null() || declared->get_columns().is_empty()) {
        return;
    }
    predict::CommandFrameRecord planned;
    const SchemaRecord *seated_schema = input_schema(p_slot);
    static const SchemaRecord unplannable;
    if (!predict::CommandFrameRecord::open(
            seated_schema != nullptr ? *seated_schema : unplannable,
            planned
        )) {
        NETW_WARN(
            sys::PREDICTION,
            "Prediction: %s declares input() fields whose types the command "
            "lane cannot plan a row from. A self-describing column has no "
            "fixed width, so nothing will cross. Type the properties, or "
            "quantize them.",
            String(seated->get_entity_id()).utf8().get_data()
        );
    }
}

int64_t NetwPredictionEngine::property_class_report_hash(
    int64_t p_slot,
    const Ref<NetwPropertySet> &p_set
) const {
    PackedStringArray parts;
    const TypedArray<NetwPropertySetColumn> columns = p_set->get_columns();
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        Array row;
        row.push_back(String(column->get_key()));
        row.push_back(column->get_property_class());
        row.push_back(column->get_quantizer().is_valid() ? 1 : 0);
        row.push_back(column->get_epsilon_override());
        row.push_back(column->get_explicit_teleport_only() ? 1 : 0);
        row.push_back(column->get_explicit_reconcile_only() ? 1 : 0);
        row.push_back(String(column->get_carry_channel()));
        row.push_back(has_carry_rule(p_slot, column->get_key()) ? 1 : 0);
#if defined(NETW_MODULE)
        parts.push_back(
            String("%s:%d:%d:%.4f:%d:%d:%s:%d").sprintf(row, nullptr)
        );
#else
        parts.push_back(String("%s:%d:%d:%.4f:%d:%d:%s:%d") % row);
#endif
    }
    parts.push_back(vformat("masked:%d", p_set->get_masked() ? 1 : 0));
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    const Ref<NetwPropertySet> broadcast = broadcast_set_of(seated);
    if (broadcast.is_valid()) {
        const TypedArray<NetwPropertySetColumn> shared
            = broadcast->get_columns();
        for (int at = 0; at < shared.size(); ++at) {
            const Ref<NetwPropertySetColumn> column = shared[at];
            parts.push_back(String("broadcast:") + String(column->get_key()));
        }
    }
    parts.push_back(vformat(
        "controller:%d",
        seated.is_valid() && seated->get_controller() > 0 ? 1 : 0
    ));
    return int64_t(String("\n").join(parts).hash());
}

void NetwPredictionEngine::record_witness_detail(
    int64_t p_slot,
    int64_t p_transition,
    const Dictionary &p_detail
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->witness_details
            .record(p_transition, p_detail, predict::TAPE_HISTORY_LIMIT);
    }
}

void NetwPredictionEngine::clear_witness_details(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->witness_details.clear();
    }
}

void NetwPredictionEngine::hold_deferred_operator(
    int64_t p_slot,
    int64_t p_basis,
    int64_t p_recv_tick,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->deferred_operator.hold(p_basis, p_recv_tick, p_payload);
    }
}

int64_t NetwPredictionEngine::deferred_operator_basis(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->deferred_operator.basis : -1;
}

int64_t NetwPredictionEngine::deferred_operator_recv_tick(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->deferred_operator.recv_tick : -1;
}

Dictionary NetwPredictionEngine::release_deferred_operator(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->deferred_operator.held()) {
        return Dictionary();
    }
    const Dictionary payload = row->deferred_operator.payload;
    row->deferred_operator.release();
    return payload;
}

int NetwPredictionEngine::defer_operator_for_witness(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_basis,
    const Dictionary &p_payload,
    bool p_has_corridor,
    bool p_observing
) {
    const PackedInt64Array stats = episode_stats(p_slot);
    const int64_t judged = journal_has(p_slot, p_basis) ? p_basis : -1;
    if (stats[STAT_EPISODE_ACTIVE] == 0 || witness_judged(p_slot, judged)) {
        return DEFER_REFUSED;
    }
    const bool transport_waits
        = p_has_corridor && stats[STAT_EPISODE_TRANSPORT_DECIDED] == 0;
    const bool dissipate_waits = dissipate_declared(p_slot)
        && stats[STAT_EPISODE_DISSIPATE_DECIDED] == 0 && !p_observing;
    if (!transport_waits && !dissipate_waits) {
        return DEFER_REFUSED;
    }
    const int64_t held = deferred_operator_basis(p_slot);
    if (held >= 0 && held != p_basis) {
        Dictionary unaligned;
        unaligned[StringName("witness_aligned")] = false;
        if (transport_waits && stats[STAT_EPISODE_TRANSPORT_DECIDED] == 0) {
            record_episode_decision(
                p_slot,
                int(predict::Operator::TRANSPORT_DELTA),
                held,
                false,
                false,
                unaligned
            );
        }
        if (dissipate_waits && stats[STAT_EPISODE_DISSIPATE_DECIDED] == 0) {
            record_episode_decision(
                p_slot,
                int(predict::Operator::DISSIPATE),
                held,
                false,
                false,
                unaligned
            );
        }
        drop_deferred_operator(p_slot);
        return DEFER_DISPLACED;
    }
    hold_deferred_operator(p_slot, p_basis, p_recv_tick, p_payload);
    return DEFER_HELD;
}

void NetwPredictionEngine::drop_deferred_operator(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->deferred_operator.release();
    }
}

void NetwPredictionEngine::set_island_participants(
    int64_t p_slot,
    const PackedStringArray &p_names
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    PackedStringArray sorted = p_names.duplicate();
    sorted.sort();
    row->island_participants = sorted;
}

PackedStringArray NetwPredictionEngine::island_participants(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->island_participants : PackedStringArray();
}

Dictionary NetwPredictionEngine::topology_facts(
    int64_t p_slot,
    int p_schedule,
    int64_t p_epoch
) const {
    Dictionary out;
    out[StringName("schedule")] = p_schedule;
    out[StringName("epoch")] = p_epoch;
    out[StringName("participants")] = island_participants(p_slot);
    return out;
}

namespace {

Dictionary causal_only(
    const predict::Slot &p_row,
    const Dictionary &p_payload
) {
    bool any_causal = false;
    for (uint32_t at = 0; at < p_row.wiring.causal.size(); ++at) {
        if (p_row.wiring.causal[at] != 0) {
            any_causal = true;
            break;
        }
    }
    if (!any_causal) {
        return p_payload;
    }
    Dictionary out;
    const Array fields = p_payload.keys();
    for (int at = 0; at < fields.size(); ++at) {
        const int field = p_row.wiring.fields.index_of(fields[at]);
        if (field >= 0 && p_row.wiring.causal[uint32_t(field)] != 0) {
            out[fields[at]] = p_payload[fields[at]];
        }
    }
    return out;
}

} // namespace

PackedInt64Array NetwPredictionEngine::open_state_stamp(
    int64_t p_slot,
    bool p_raw_enabled
) {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(STAMP_COLUMN_COUNT);
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->port.bound()) {
        return out;
    }
    out.set(STAMP_BOUND, 1);

    const Dictionary raw = capture_state(p_slot);
    out.set(STAMP_PRE_FP, compared_fingerprint(*row, raw));

    const PackedInt32Array families = state_family_fingerprints(p_slot, raw);
    out.set(STAMP_POSE_FP, families[0]);
    out.set(STAMP_MOMENTUM_FP, families[1]);
    out.set(STAMP_CONTROLLER_FP, families[2]);

    if (p_raw_enabled) {
        out.set(
            STAMP_RAW_FP,
            prediction_core::raw_state_fingerprint(causal_only(*row, raw))
        );
        out.set(STAMP_EVIDENCE_MASK, predict::EVIDENCE_RAW);
    }
    return out;
}

Dictionary NetwPredictionEngine::transport_deltas(
    int64_t p_slot,
    const Dictionary &p_current,
    const Dictionary &p_restore
) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_restore.is_empty()) {
        return out;
    }
    const Array fields = p_restore.keys();
    for (int at = 0; at < fields.size(); ++at) {
        const Variant key = fields[at];
        if (!p_current.has(key)) {
            return Dictionary();
        }
        const int field = row->wiring.fields.index_of(key);
        const bool angle
            = field >= 0 && row->wiring.angle[uint32_t(field)] != 0;
        out[key] = predict::value_error(p_current[key], p_restore[key], angle);
    }
    return out;
}

PackedFloat64Array NetwPredictionEngine::correction_tolerances(
    int64_t p_slot,
    double p_fallback_epsilon
) const {
    PackedFloat64Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    out.resize(row->wiring.count());
    double *values = out.ptrw();
    for (int at = 0; at < row->wiring.count(); ++at) {
        const uint32_t field = uint32_t(at);
        if (row->wiring.vote_exclude[field] != 0) {
            values[at] = -1.0;
            continue;
        }
        const double declared = row->wiring.epsilon[field];
        values[at] = declared >= 0.0 ? declared : p_fallback_epsilon;
    }
    return out;
}

PackedFloat64Array NetwPredictionEngine::meter_tolerances(
    int64_t p_slot,
    int p_domain,
    double p_fallback_epsilon
) const {
    PackedFloat64Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    out.resize(row->wiring.count());
    double *values = out.ptrw();
    for (int at = 0; at < row->wiring.count(); ++at) {
        const uint32_t field = uint32_t(at);
        const double declared = row->wiring.epsilon[field];
        if (row->wiring.trigger_exclude[field] != 0) {
            values[at] = row->wiring.causal[field] != 0 && declared >= 0.0
                ? declared
                : -1.0;
            continue;
        }
        if (row->wiring.withheld[field] != 0 && declared >= 0.0) {
            values[at] = declared;
            continue;
        }
        if (p_domain == int(predict::Domain::OUT_OF_DOMAIN)) {
            values[at] = declared >= 0.0 ? declared : p_fallback_epsilon;
            continue;
        }
        const Ref<NetwQuantize> quantizer = row->wiring.codec.quantizers[field];
        values[at] = quantizer.is_valid()
            ? quantizer->max_error(
                  static_cast<Variant::Type>(row->wiring.codec.types[field])
              )
            : 0.0;
    }
    return out;
}

int NetwPredictionEngine::meter_of(
    int64_t p_slot,
    const Dictionary &p_field_divergence,
    int p_domain,
    double p_fallback_epsilon
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    const PackedFloat64Array tolerances
        = meter_tolerances(p_slot, p_domain, p_fallback_epsilon);
    Dictionary declared;
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (tolerances[at] >= 0.0) {
            declared[row->wiring.fields.name_at(at)] = tolerances[at];
        }
    }
    return prediction_core::measure(p_field_divergence, declared);
}

namespace {

int64_t counted(const Dictionary &p_record, const char *p_key) {
    const Array held = p_record.get(StringName(p_key), Array());
    return int64_t(held.size());
}

Dictionary last_attempt(const Dictionary &p_record) {
    Dictionary out;
    const Array writes = p_record.get(StringName("writes"), Array());
    if (!writes.is_empty()) {
        const Dictionary write = writes[writes.size() - 1];
        out[StringName("operator")] = int(write.get(StringName("operator"), 0));
        out[StringName("basis")] = int(write.get(StringName("basis"), -1));
        out[StringName("outcome")] = int(write.get(StringName("outcome"), -1));
        return out;
    }
    const Array decisions = p_record.get(StringName("decisions"), Array());
    if (!decisions.is_empty()) {
        const Dictionary decision = decisions[decisions.size() - 1];
        out[StringName("operator")]
            = int(decision.get(StringName("operator"), 0));
        out[StringName("basis")] = int(decision.get(StringName("basis"), -1));
        out[StringName("outcome")] = -1;
    }
    return out;
}

} // namespace

int64_t NetwPredictionEngine::episode_revision(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->episode_revision : 0;
}

void NetwPredictionEngine::stamp_episode_revision(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->episode_revision += 1;
    }
}

void NetwPredictionEngine::sync_episode(int64_t p_slot) {
    if (episode_stats(p_slot)[STAT_EPISODE_ID] > 0) {
        stamp_episode_revision(p_slot);
    }
}

void NetwPredictionEngine::close_episode(int64_t p_slot) {
    stamp_episode_revision(p_slot);
    const Dictionary report = episode_record(p_slot);
    announce_episode(
        p_slot,
        EventPlane::EPISODE_CLOSE,
        report.get(StringName("disposition"), Dictionary())
    );
}

int NetwPredictionEngine::settle_comparison(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_ack,
    bool p_reconstructed,
    bool p_corrected,
    bool p_settled,
    double p_divergence,
    bool p_probation_before,
    int64_t p_episode_state_before,
    const Callable &p_enter_fallback
) {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    const double epsilon
        = handle != nullptr ? handle->get_divergence_epsilon() : 0.0;
    if (p_reconstructed) {
        ledger_note_comparison(p_slot, p_corrected, p_ack, epsilon);
    }
    const int attribution = attribution_for(p_slot, p_ack);

    if (p_probation_before && p_reconstructed && p_corrected) {
        if (open_episode(p_slot, p_ack, attribution)) {
            sync_episode(p_slot);
        }
        note_verdict_reason(
            p_slot,
            NetwPredict::VERDICT_REASON_PROBATION_REQUARANTINE
        );
        announce_divergence(p_slot, p_ack, attribution, p_divergence);
        p_enter_fallback.call(p_ack, attribution);
        if (handle != nullptr) {
            handle->emit_signal(
                StringName("state_evaluated"),
                p_recv_tick,
                p_ack,
                p_divergence,
                true
            );
        }
        return SETTLE_REQUARANTINED;
    }

    if (p_corrected) {
        if (episode_state(p_slot) == NetwPredict::EPISODE_STATE_OPEN) {
            record_episode_divergence(p_slot, p_ack);
            sync_episode(p_slot);
        } else if (open_episode(p_slot, p_ack, attribution)) {
            sync_episode(p_slot);
        }
        announce_divergence(p_slot, p_ack, attribution, p_divergence);
    }
    if (p_settled) {
        mark_aligned_error(p_slot, p_ack, p_divergence);
    }
    record_episode_comparison(p_slot, p_episode_state_before);

    const int refusal = refuse_recovery(p_slot, p_corrected);
    if (refusal == NetwPredict::VERDICT_REASON_NONE) {
        return SETTLE_PROCEED;
    }
    if (refusal == NetwPredict::VERDICT_REASON_EVIDENCE_EXHAUSTED) {
        p_enter_fallback.call(p_ack, attribution);
    }
    if (handle != nullptr) {
        handle->emit_signal(
            StringName("state_evaluated"),
            p_recv_tick,
            p_ack,
            p_divergence,
            p_corrected
        );
    }
    return SETTLE_REFUSED;
}

void NetwPredictionEngine::record_episode_comparison(
    int64_t p_slot,
    int64_t p_previous_state
) {
    const int64_t state = episode_state(p_slot);
    if (p_previous_state == NetwPredict::EPISODE_STATE_OPEN
        && state == NetwPredict::EPISODE_STATE_CLOSED) {
        close_episode(p_slot);
        return;
    }
    sync_episode(p_slot);
    if (p_previous_state != NetwPredict::EPISODE_STATE_OPEN
        && state == NetwPredict::EPISODE_STATE_OPEN) {
        announce_episode(p_slot, EventPlane::EPISODE_OPEN, Dictionary());
    }
}

Dictionary NetwPredictionEngine::episode_digest(int64_t p_slot) const {
    const Dictionary record = episode_record(p_slot);
    if (record.is_empty()) {
        return Dictionary();
    }
    const Dictionary generator_row
        = record.get(StringName("generator_row_copy"), Dictionary());
    const Array comparisons = record.get(StringName("comparisons"), Array());

    Dictionary generator;
    generator[StringName("transition")] = int(generator_row.get(
        StringName("transition"),
        record.get(StringName("opened_transition"), -1)
    ));
    generator[StringName("boundary")]
        = int(record.get(StringName("attribution"), 0));

    Dictionary evidence;
    evidence[StringName("comparisons")] = counted(record, "comparisons");
    evidence[StringName("writes")] = counted(record, "writes");
    evidence[StringName("decisions")] = counted(record, "decisions");
    evidence[StringName("taint")] = counted(record, "taint");
    evidence[StringName("secondary_generators")]
        = counted(record, "secondary_generators");
    evidence[StringName("dropped")]
        = int(record.get(StringName("evidence_dropped"), 0));

    Dictionary out;
    out[StringName("id")] = int(record.get(StringName("id"), 0));
    out[StringName("revision")] = episode_revision(p_slot);
    out[StringName("last_comparison_transition")]
        = int(record.get(StringName("last_comparison_transition"), -1));
    out[StringName("last_meter")] = comparisons.is_empty()
        ? 0
        : int(Dictionary(comparisons[comparisons.size() - 1])
                  .get(StringName("meter"), 0));
    out[StringName("generator")] = generator;
    out[StringName("last_operator")] = last_attempt(record);
    out[StringName("disposition")]
        = record.get(StringName("disposition"), Dictionary());
    out[StringName("evidence")] = evidence;
    return out;
}

namespace {

StringName class_name_of(int p_property_class) {
    if (p_property_class == int(predict::PropertyClass::DERIVED)) {
        return StringName("DERIVED");
    }
    if (p_property_class == int(predict::PropertyClass::COSMETIC)) {
        return StringName("COSMETIC");
    }
    return StringName("CAUSAL");
}

String joined(const PackedStringArray &p_names) {
    return String(", ").join(p_names);
}

Dictionary finding_of(
    const char *p_code,
    const char *p_severity,
    const PackedStringArray &p_fields,
    const String &p_message
) {
    Dictionary out;
    out[StringName("code")] = StringName(p_code);
    out[StringName("severity")] = StringName(p_severity);
    out[StringName("fields")] = p_fields;
    out[StringName("message")] = p_message;
    return out;
}

} // namespace

Dictionary NetwPredictionEngine::advanced_seed(
    int64_t p_slot,
    const Dictionary &p_payload,
    int64_t p_basis,
    int p_snap_restore,
    int p_max_restore_ticks,
    double p_teleport_threshold,
    double p_divergence_epsilon
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_payload.is_empty()) {
        return p_payload;
    }
    const Dictionary carried = carry_payload(
        p_slot,
        p_payload,
        p_basis,
        p_teleport_threshold,
        p_divergence_epsilon
    );
    const Dictionary projection = projection_map(p_slot);
    if (p_snap_restore != int(NetwPredict::RESTORE_MODE_EXTRAPOLATED)
        || projection.is_empty()) {
        return carried;
    }
    const int64_t span = CLAMP(
        transition_span(p_slot, p_basis),
        int64_t(0),
        int64_t(p_max_restore_ticks)
    );
    if (span <= 0) {
        return carried;
    }
    return prediction_core::project_payload(
        carried,
        projection,
        double(span) * row->cursor.tick_delta
    );
}

Dictionary NetwPredictionEngine::project_state(
    int64_t p_slot,
    const Dictionary &p_payload,
    double p_age
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_age <= 0.0) {
        return p_payload;
    }
    Dictionary projection;
    for (int at = 0; at < row->wiring.count(); ++at) {
        const int channel = row->wiring.projection[uint32_t(at)];
        if (channel >= 0) {
            projection[row->wiring.fields.name_at(at)]
                = row->wiring.fields.name_at(channel);
        }
    }
    return prediction_core::project_payload(p_payload, projection, p_age);
}

bool NetwPredictionEngine::has_pose_fields(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    for (int at = 0; at < row->wiring.count(); ++at) {
        if (row->wiring.pose[uint32_t(at)] != 0) {
            return true;
        }
    }
    return false;
}

Dictionary NetwPredictionEngine::pose_errors(
    int64_t p_slot,
    const Dictionary &p_current,
    const Dictionary &p_target
) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    for (int at = 0; at < row->wiring.count(); ++at) {
        const uint32_t field = uint32_t(at);
        if (row->wiring.pose[field] == 0) {
            continue;
        }
        const StringName key = row->wiring.fields.name_at(at);
        if (!p_current.has(key) || !p_target.has(key)) {
            continue;
        }
        out[key] = predict::value_error(
            p_current[key],
            p_target[key],
            row->wiring.angle[field] != 0
        );
    }
    return out;
}

void NetwPredictionEngine::validate_declaration(
    int64_t p_slot,
    const Ref<NetwPropertySet> &p_set,
    const Callable &p_emit_findings
) {
    if (p_set.is_null()) {
        return;
    }
    const TypedArray<NetwPropertySetColumn> columns = p_set->get_columns();
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwPropertySetColumn> column = columns[at];
        if (column->get_lane() != NetwPropertySet::RETAINED) {
            continue;
        }
        NETW_ERROR(
            sys::PREDICTION,
            "Prediction: predicted state property '%s' is retained. "
            "Predicted state must be volatile so timeline snapshots arrive "
            "atomically.",
            String(column->get_key()).utf8().get_data()
        );
    }

    const int64_t config = property_class_report_hash(p_slot, p_set);
    if (config == validated_class_hash_of(p_slot)) {
        return;
    }
    set_validated_class_hash(p_slot, config);

    const Dictionary report = reachability_report_of(p_slot);
    p_emit_findings.call(report.get(StringName("findings"), Array()));
    report_authority_model(p_slot);
}

Dictionary NetwPredictionEngine::reachability_report_of(int64_t p_slot) const {
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (handle == nullptr) {
        return Dictionary();
    }
    const Ref<NetwPredictIsland> island = handle->get_island();
    int claim = 0;
    if (island.is_valid() && island->get_approximate()) {
        claim = 2;
    } else if (island.is_valid() && island->get_exact_claim()) {
        claim = 1;
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    return reachability_report(
        p_slot,
        seated.is_valid() ? seated->get_entity_id() : StringName(),
        handle->get_divergence_epsilon(),
        handle->get_teleport_threshold(),
        handle->get_breach_response(),
        breach_source_of(p_slot),
        claim,
        handle->get_collision_cooldown_ticks(),
        handle->get_transport_corridor().is_valid()
    );
}

Dictionary NetwPredictionEngine::reachability_report(
    int64_t p_slot,
    const StringName &p_entity_id,
    double p_divergence_epsilon,
    double p_teleport_threshold,
    int p_breach_response,
    const StringName &p_breach_source,
    int p_island_claim,
    int p_contact_window_ticks,
    bool p_has_corridor
) const {
    Dictionary out;
    Array findings;
    Dictionary fields;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        out[StringName("entity")] = p_entity_id;
        out[StringName("fields")] = fields;
        out[StringName("findings")] = findings;
        return out;
    }
    const bool frame_tier = row->config.schedule == int(Schedule::FRAME);

    PackedStringArray unquantized;
    PackedStringArray uncompared;
    PackedStringArray transport_without_epsilon;
    PackedStringArray unobserved;
    PackedStringArray unrepairable;
    PackedStringArray inert_steps;
    PackedStringArray tier_inheritors;
    PackedStringArray rate_inheritors;

    for (int at = 0; at < row->wiring.count(); ++at) {
        const uint32_t field = uint32_t(at);
        const StringName key = row->wiring.fields.name_at(at);
        const String name = String(key);
        const bool causal = row->wiring.causal[field] != 0;
        const StringName channel = row->wiring.carry_channel[field];
        const bool reconcile_only = row->wiring.trigger_exclude[field] != 0;
        const bool teleport_only = row->wiring.withheld[field] != 0;
        const double epsilon_override = row->wiring.epsilon[field];
        const double teleport_override = row->wiring.teleport[field];
        const bool enrolled = row->wiring.pose[field] != 0;

        StringName model = StringName("none");
        if (channel != StringName()) {
            model = StringName("channel");
        } else if (row->carry_rules.has(key)) {
            model = StringName("step");
        }

        bool live = false;
        String why = String("no forward model is declared");
        if (model == StringName("channel")) {
            live = row->wiring.projection[field] >= 0;
            why = live ? String()
                       : String("carry_along(&\"") + String(channel)
                    + String("\") names a column this set does not carry, so "
                             "the channel cannot be read at the tick it would "
                             "advance");
        } else if (model == StringName("step")) {
            if (!frame_tier) {
                why = String(
                    "carry_step() needs prediction.schedule = FRAME; under "
                    "the tick tier it is refused on first use and retired"
                );
                inert_steps.push_back(name);
            } else if (carry_retired(p_slot, key)) {
                why = String(
                    "the rule was retired at runtime after failing its "
                    "fidelity gate"
                );
            } else {
                live = true;
                why = String();
            }
        }

        PackedStringArray operators;
        if (!teleport_only) {
            operators.push_back("sub_teleport_restore");
        }
        if (row->wiring.converge_rate[field] > 0.0) {
            operators.push_back("converge");
        }
        if (live) {
            operators.push_back("carry_advance");
        }
        operators.push_back("full_closure");

        Dictionary forward_model;
        forward_model[StringName("kind")] = model;
        forward_model[StringName("live")] = live;
        forward_model[StringName("why")] = why;

        Dictionary judged;
        judged[StringName("class")]
            = class_name_of(row->wiring.property_class[field]);
        judged[StringName("triggers")] = !reconcile_only && causal;
        judged[StringName("tolerance")]
            = epsilon_override >= 0.0 ? epsilon_override : p_divergence_epsilon;
        judged[StringName("tolerance_declared")] = epsilon_override >= 0.0;
        judged[StringName("teleport_at")] = teleport_override >= 0.0
            ? teleport_override
            : p_teleport_threshold;
        judged[StringName("teleport_at_declared")] = teleport_override >= 0.0;
        judged[StringName("in_tier")] = enrolled;
        judged[StringName("operators")] = operators;
        judged[StringName("forward_model")] = forward_model;
        fields[key] = judged;

        const bool quantized = row->wiring.codec.quantizers[field].is_valid();
        if (causal && !quantized) {
            unquantized.push_back(name);
        }
        if (causal && reconcile_only && epsilon_override < 0.0) {
            unobserved.push_back(name);
        }
        if (causal && teleport_only && !reconcile_only
            && model == StringName("none")) {
            unrepairable.push_back(name);
        }
        if (!causal && epsilon_override >= 0.0) {
            uncompared.push_back(name);
        }
        if (p_has_corridor && causal
            && row->wiring.state_family[field]
                != int(predict::StateFamily::POSE)
            && epsilon_override < 0.0) {
            transport_without_epsilon.push_back(name);
        }
        if (enrolled && teleport_override < 0.0) {
            tier_inheritors.push_back(name);
        }
        if (causal && epsilon_override < 0.0
            && row->wiring.state_family[field]
                == int(predict::StateFamily::MOMENTUM)) {
            rate_inheritors.push_back(name);
        }
    }

    if (!unquantized.is_empty()) {
        findings.push_back(finding_of(
            "unquantized",
            "debug",
            unquantized,
            String("NetwLagCompensation: causal state properties [")
                + joined(unquantized)
                + String(
                    "] have no quantizer, so their canonical form is "
                    "their raw bits. Legal, but two peers agree only if "
                    "they produce those bits identically."
                )
        ));
    }
    if (!uncompared.is_empty()) {
        findings.push_back(finding_of(
            "uncompared",
            "error",
            uncompared,
            String("Prediction: non-causal state properties [")
                + joined(uncompared)
                + String(
                    "] carry a divergence epsilon. Only a causal() field "
                    "is compared, so the threshold decides nothing: it "
                    "cannot raise a correction, and the meter reads its "
                    "own tolerance row. Mark them causal() if the next "
                    "step reads them, or drop the epsilon() mark."
                )
        ));
    }
    if (!transport_without_epsilon.is_empty()) {
        findings.push_back(finding_of(
            "transport_without_epsilon",
            "debug",
            transport_without_epsilon,
            String(
                "NetwLagCompensation: transport reads causal non-pose "
                "properties ["
            ) + joined(transport_without_epsilon)
                + String(
                    "] without declared epsilons. Their units fall back "
                    "to the entity default, so the transport gate has no "
                    "world-scale tolerance."
                )
        ));
    }
    if (!unobserved.is_empty()) {
        findings.push_back(finding_of(
            "unobserved",
            "error",
            unobserved,
            String("Prediction: causal state properties [") + joined(unobserved)
                + String(
                    "] are reconcile_only() and declare no epsilon(), so "
                    "nothing observes them. A reconcile-only field cannot "
                    "trigger a correction, and without a declared scale "
                    "it does not reach the convergence meter either, "
                    "while the next transition still reads it. A field "
                    "like that drifts until some other field crosses on "
                    "its behalf, which charges the divergence to the "
                    "wrong place. Give each an epsilon() at its own world "
                    "scale, or drop reconcile_only() so it can answer for "
                    "itself."
                )
        ));
    }
    if (!unrepairable.is_empty()) {
        findings.push_back(finding_of(
            "unrepairable",
            "warning",
            unrepairable,
            String("Prediction: causal state properties [")
                + joined(unrepairable)
                + String(
                    "] are teleport_only() and can still trigger, so they "
                    "demand recoveries that no sub-teleport restore may "
                    "write, and each one is answered by writing other "
                    "fields instead. The promoted full closure is the "
                    "only operator that reaches them, and with neither "
                    "carry_along() nor carry_step() it writes the "
                    "acknowledged value, which a moving field has already "
                    "left. Declare one of those if the field can be "
                    "advanced, or reconcile_only() so its drift stops "
                    "asking for recoveries that answer elsewhere. "
                    "NetwPredictionHandle.field_recovery counts which "
                    "happened."
                )
        ));
    }
    if (!inert_steps.is_empty()) {
        findings.push_back(finding_of(
            "inert_forward_model",
            "warning",
            inert_steps,
            String("Prediction: state properties [") + joined(inert_steps)
                + String(
                    "] declare carry_step() while this entity resolved "
                    "the tick schedule. The rule is legal, is accepted "
                    "here, and is then refused on its first use and "
                    "permanently retired, so every recovery writes the "
                    "acknowledged value as if none had been declared. Set "
                    "prediction.schedule to FRAME, or carry_along() a "
                    "replicated derivative instead."
                )
        ));
    }
    if (tier_inheritors.size() >= 2) {
        findings.push_back(finding_of(
            "unit_unsafe_inheritance",
            "warning",
            tier_inheritors,
            String("NetwLagCompensation: state properties [")
                + joined(tier_inheritors)
                + String(
                    "] are enrolled in the teleport tier and none "
                    "declares teleport_at(), so all of them are measured "
                    "against the entity's "
                )
                + String::num(p_teleport_threshold, 4)
                + String(
                    ". Each advances by its own forward model, so they "
                    "are different quantities sharing one distance, and a "
                    "rotation rate read against a length teleports on "
                    "noise while a length read against a rate never "
                    "teleports at all. Give each one teleport_at() in its "
                    "own units."
                )
        ));
    }
    if (!rate_inheritors.is_empty()) {
        findings.push_back(finding_of(
            "unit_unsafe_inheritance",
            "warning",
            rate_inheritors,
            String("NetwLagCompensation: state properties [")
                + joined(rate_inheritors)
                + String(
                    "] are carry channels, so each is the rate of the "
                    "field it advances, and none declares epsilon(). They "
                    "fall back to the entity's "
                )
                + String::num(p_divergence_epsilon, 4)
                + String(
                    ", which is the tolerance a position was sized for. A "
                    "derivative compared at its own quantity's scale "
                    "reports a divergence in the wrong units. Give each an "
                    "epsilon() at the scale its rate actually moves."
                )
        ));
    }

    Dictionary breach;
    breach[StringName("response")] = p_breach_response == 1
        ? StringName("DEMOTE")
        : StringName("PREDICT_THROUGH");
    breach[StringName("declared_by")] = p_breach_source;

    Dictionary domain;
    domain[StringName("island")] = p_island_claim == 2
        ? StringName("approximate")
        : (p_island_claim == 1 ? StringName("exact") : StringName("none"));
    domain[StringName("contact_window_ticks")] = p_contact_window_ticks;

    out[StringName("entity")] = p_entity_id;
    out[StringName("schedule")]
        = frame_tier ? StringName("FRAME") : StringName("TICK");
    out[StringName("breach")] = breach;
    out[StringName("domain")] = domain;
    out[StringName("transport")] = p_has_corridor;
    out[StringName("fields")] = fields;
    out[StringName("findings")] = findings;
    return out;
}

Dictionary NetwPredictionEngine::dissipate_fields(
    int64_t p_slot,
    const Dictionary &p_field_divergence,
    int p_domain,
    double p_fallback_epsilon
) const {
    Dictionary fields;
    bool momentum_active = false;
    bool other_active = false;
    const predict::Slot *row = row_of(p_slot);
    if (row != nullptr) {
        const PackedFloat64Array tolerances
            = meter_tolerances(p_slot, p_domain, p_fallback_epsilon);
        for (int at = 0; at < row->wiring.count(); ++at) {
            const uint32_t field = uint32_t(at);
            const bool momentum = row->wiring.withheld[field] != 0;
            const bool excluded = row->wiring.trigger_exclude[field] != 0;
            if (excluded && !momentum) {
                continue;
            }
            const StringName key = row->wiring.fields.name_at(at);
            if (!p_field_divergence.has(key)) {
                continue;
            }
            const double error = double(p_field_divergence[key]);
            double tolerance = tolerances[at];
            if (tolerance < 0.0) {
                const double declared = row->wiring.epsilon[field];
                tolerance = declared >= 0.0 ? declared : p_fallback_epsilon;
            }
            const bool active = error > tolerance;
            Dictionary judged;
            judged[StringName("error")] = error;
            judged[StringName("epsilon")] = tolerance;
            judged[StringName("active")] = active;
            judged[StringName("momentum")] = momentum;
            fields[key] = judged;
            if (!active) {
                continue;
            }
            if (momentum) {
                momentum_active = true;
            } else if (!excluded) {
                other_active = true;
            }
        }
    }
    Dictionary out;
    out[StringName("momentum_active")] = momentum_active;
    out[StringName("other_active")] = other_active;
    out[StringName("fields")] = fields;
    return out;
}

Dictionary NetwPredictionEngine::non_pose_eligibility(
    int64_t p_slot,
    const Dictionary &p_predicted,
    const Dictionary &p_authority,
    double p_fallback_epsilon
) const {
    Dictionary fields;
    bool agrees = true;
    const predict::Slot *row = row_of(p_slot);
    if (row != nullptr) {
        for (int at = 0; at < row->wiring.count(); ++at) {
            const uint32_t field = uint32_t(at);
            if (row->wiring.causal[field] == 0
                || row->wiring.state_family[field]
                    == int(predict::StateFamily::POSE)) {
                continue;
            }
            const StringName key = row->wiring.fields.name_at(at);
            double error = std::numeric_limits<double>::infinity();
            if (p_predicted.has(key) && p_authority.has(key)) {
                error = predict::value_error(
                    p_predicted[key],
                    p_authority[key],
                    row->wiring.angle[field] != 0
                );
            }
            const double declared = row->wiring.epsilon[field];
            const double epsilon
                = declared >= 0.0 ? declared : p_fallback_epsilon;
            const bool field_agrees = !(error > epsilon);
            Dictionary judged;
            judged[StringName("error")] = error;
            judged[StringName("epsilon")] = epsilon;
            judged[StringName("agrees")] = field_agrees;
            fields[key] = judged;
            agrees = agrees && field_agrees;
        }
    }
    Dictionary out;
    out[StringName("agrees")] = agrees;
    out[StringName("fields")] = fields;
    return out;
}

bool NetwPredictionEngine::transport_moved(const Dictionary &p_deltas) {
    const Array fields = p_deltas.keys();
    for (int at = 0; at < fields.size(); ++at) {
        if (double(p_deltas[fields[at]]) > 0.0) {
            return true;
        }
    }
    return false;
}

Dictionary NetwPredictionEngine::episode_record(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    return episode(p_slot).to_dictionary(row->witness_details.rows());
}

int64_t NetwPredictionEngine::journal_epoch(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.epoch() : -1;
}

PackedInt64Array NetwPredictionEngine::journal_transitions(
    int64_t p_slot
) const {
    PackedInt64Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    out.resize(row->journal.size());
    int64_t *values = out.ptrw();
    for (int at = 0; at < out.size(); ++at) {
        values[at] = row->journal.transition_at(at);
    }
    return out;
}

predict::JournalRow NetwPredictionEngine::journal_row(
    int64_t p_slot,
    int64_t p_transition
) const {
    predict::JournalRow out;
    const predict::Slot *slot = row_of(p_slot);
    if (slot == nullptr) {
        return out;
    }
    const int at = slot->journal.index_of(p_transition);
    if (at < 0) {
        return out;
    }
    out.present = true;
    out.transition = slot->journal.transition_at(at);
    out.label = slot->journal.label_at(at);
    out.kind = slot->journal.kind_at(at);
    out.c_hash = slot->journal.c_hash_at(at);
    out.e_digest = slot->journal.e_digest_at(at);
    out.pre_fp = slot->journal.pre_fp_at(at);
    out.topo_fp = slot->journal.topo_fp_at(at);
    out.raw_fp = slot->journal.raw_fp_at(at);
    out.witness_fp = slot->journal.witness_fp_at(at);
    out.witness_class_bits = slot->journal.witness_class_bits_at(at);
    out.aligned_error = slot->journal.aligned_error_at(at);
    out.evidence_mask = slot->journal.evidence_mask_at(at);
    out.pre_families = slot->journal.pre_families_at(at);
    out.post_fp = slot->journal.post_fp_at(at);
    out.post_families = slot->journal.post_families_at(at);
    out.episode_id = slot->journal.episode_id_at(at);
    out.write_id = slot->journal.write_id_at(at);
    out.op = slot->journal.operator_at(at);
    out.basis = slot->journal.basis_at(at);
    out.differing_family = slot->journal.differing_family_at(at);
    out.domain = slot->journal.domain_at(at);
    out.attribution = slot->journal.attribution_at(at);
    out.flags = slot->journal.flags_at(at);
    return out;
}

int NetwPredictionEngine::journal_slot_of(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.index_of(p_transition) : -1;
}

int64_t NetwPredictionEngine::journal_last_closed(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.last_closed() : -1;
}

int64_t NetwPredictionEngine::journal_first_unmatched(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.first_unmatched() : -1;
}

int64_t NetwPredictionEngine::journal_first_chain_break(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.first_chain_break() : -1;
}

void NetwPredictionEngine::journal_clear(int64_t p_slot, int64_t p_epoch) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Journal reset names closed slot %d.",
        p_slot
    );
    row->journal.clear(p_epoch);
}

void NetwPredictionEngine::tape_reset(int64_t p_slot, int64_t p_epoch) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Tape reset names closed slot %d.",
        p_slot
    );
    row->reset_tape(p_epoch);
}

PackedByteArray NetwPredictionEngine::build_ack_frame(
    int64_t p_slot,
    int p_epoch,
    int64_t p_ack,
    int64_t p_owner_ack_floor
) const {
    const predict::Slot *row = row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        PackedByteArray(),
        sys::PREDICTION,
        "Acknowledgement frame names closed slot %d.",
        p_slot
    );
    if (p_ack < 0) {
        return PackedByteArray();
    }
    const predict::Journal &journal = row->journal;
    const int64_t frontier = std::min(p_ack, journal.last_closed());
    if (frontier < 0) {
        return PackedByteArray();
    }
    const int64_t base
        = std::max(p_owner_ack_floor + 1, frontier - ACK_WINDOW_MAX + 1);
    if (base > frontier) {
        return PackedByteArray();
    }

    predict::AckFrame frame;
    frame.epoch = uint8_t(p_epoch);
    frame.base = uint64_t(base);
    for (int at = 0; at < journal.size(); ++at) {
        const int64_t transition = journal.transition_at(at);
        if (transition < base) {
            continue;
        }
        if (transition > frontier) {
            break;
        }
        const predict::FamilyFingerprints pre = journal.pre_families_at(at);
        const predict::FamilyFingerprints post = journal.post_families_at(at);
        predict::AckEvidenceWire evidence;
        evidence.evidence_mask = journal.evidence_mask_at(at);
        evidence.pre_fp = journal.pre_fp_at(at);
        evidence.c_hash = journal.c_hash_at(at);
        evidence.e_digest = journal.e_digest_at(at);
        evidence.post_fp = journal.post_fp_at(at);
        evidence.topo_fp = journal.topo_fp_at(at);
        evidence.witness_fp = journal.witness_fp_at(at);
        evidence.pre_pose_fp = pre.pose;
        evidence.pre_momentum_fp = pre.momentum;
        evidence.pre_controller_fp = pre.controller;
        evidence.post_pose_fp = post.pose;
        evidence.post_momentum_fp = post.momentum;
        evidence.post_controller_fp = post.controller;
        evidence.raw_fp = journal.raw_fp_at(at);
        if ((journal.flags_at(at) & predict::ROW_SUBSTITUTED) != 0) {
            evidence.flags |= predict::ACK_SUBSTITUTED;
        }
        evidence.flags |= uint8_t(
            (journal.witness_class_bits_at(at) << predict::ACK_WITNESS_SHIFT)
            & predict::ACK_WITNESS_MASK
        );
        frame.records.push_back(evidence);
    }
    if (frame.records.is_empty()) {
        return PackedByteArray();
    }
    return predict::encode_ack(frame);
}

int NetwPredictionEngine::tape_size(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->tape.size() : 0;
}

PackedInt64Array NetwPredictionEngine::tape_span(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    PackedInt64Array span;
    span.resize(2);
    int64_t *values = span.ptrw();
    values[0] = row != nullptr ? row->tape.oldest_index() : -1;
    values[1] = row != nullptr ? row->tape.newest_index() : -1;
    return span;
}

int64_t NetwPredictionEngine::tape_label_of(
    int64_t p_slot,
    int64_t p_index
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->tape.label_of(p_index) : -1;
}

bool NetwPredictionEngine::tape_is_fresh(
    int64_t p_slot,
    int64_t p_index
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->tape.is_fresh(p_index);
}

void NetwPredictionEngine::tape_prepare_tick(int64_t p_slot, int64_t p_tick) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Tape re-key names closed slot %d.",
        p_slot
    );
    row->prepare_tick_tape(p_tick);
}

void NetwPredictionEngine::tape_author(
    int64_t p_slot,
    int64_t p_label,
    bool p_fresh
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Tape entry names closed slot %d.",
        p_slot
    );
    row->author_tape_entry(p_label, p_fresh);
}

void NetwPredictionEngine::bind_timeline(
    int64_t p_slot,
    const Ref<NetwTimeline> &p_timeline
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Timeline bind names closed slot %d.",
        p_slot
    );
    row->adopt_timeline(p_timeline);
}

bool NetwPredictionEngine::wire_role_timeline(
    int64_t p_slot,
    int p_role,
    bool p_fallback_latched,
    const Ref<NetwTimeline> &p_declared
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Role timeline wiring names closed slot %d.",
        p_slot
    );
    const Ref<NetwEntity> entity = owner_entity(p_slot);
    const bool speculates = p_role == NetwPredict::ROLE_PREDICT
        || p_role == NetwPredict::ROLE_SIMULATE
        || (p_role == NetwPredict::ROLE_REMOTE && p_fallback_latched);
    const bool reads_registry = p_role == NetwPredict::ROLE_CONSUME
        || p_role == NetwPredict::ROLE_HOST_LOCAL;
    if (!speculates && !reads_registry) {
        return false;
    }
    if (speculates) {
        const Ref<NetwTimeline> own
            = NetwTimeline::create(NetwTimeline::DEFAULT_LIMIT);
        row->adopt_timeline(own);
        if (entity.is_valid()) {
            entity->set_timeline(own);
        }
    } else {
        const Ref<NetwTimeline> registered = register_timeline(entity);
        row->adopt_timeline(registered.is_valid() ? registered : p_declared);
    }
    if (p_role == NetwPredict::ROLE_PREDICT) {
        set_latest_input_tick(p_slot, -1);
    } else if (p_role == NetwPredict::ROLE_CONSUME) {
        set_next_input_tick(p_slot, -1);
        set_ack(p_slot, -1);
        set_last_input(p_slot, Dictionary());
    }
    return true;
}

Ref<NetwTimeline> NetwPredictionEngine::entry_history(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->entry_history : Ref<NetwTimeline>();
}

void NetwPredictionEngine::declare_axes(
    int64_t p_slot,
    bool p_authority,
    bool p_controlled_locally,
    bool p_inputless
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->axes.authority = p_authority;
        row->axes.controlled_locally = p_controlled_locally;
        row->axes.inputless = p_inputless;
    }
}

void NetwPredictionEngine::set_declared_epoch(int64_t p_slot, int64_t p_epoch) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->axes.epoch = p_epoch;
    }
}

int64_t NetwPredictionEngine::declared_epoch_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->axes.epoch;
}

Dictionary NetwPredictionEngine::record_drive(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label,
    int p_kind,
    const Dictionary &p_input,
    int64_t p_drive_tick,
    bool p_caller_selected,
    bool p_input_recorded,
    bool p_authoring
) {
    Dictionary drove;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return drove;
    }
    const bool is_new = !journal_has(p_slot, p_transition);
    NetwPredictionHandle *declared
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (is_new && declared != nullptr
        && declared->get_schedule() == NetwPredict::SCHEDULE_FRAME) {
        report_quantum_declaration(p_slot);
        row = mutable_row_of(p_slot);
        if (row == nullptr) {
            return drove;
        }
    }
    const PackedInt64Array stamp
        = open_state_stamp(p_slot, row->cursor.raw_fingerprints);
    const int64_t pre_fp = stamp[STAMP_PRE_FP];
    const int64_t raw_fp = stamp[STAMP_RAW_FP];
    const int evidence_mask = int(stamp[STAMP_EVIDENCE_MASK]);
    const int64_t c_hash
        = NetwPredictJournal::fnv1a(canonical_input_bytes(p_slot, p_input));

    const Dictionary facts
        = topology_facts(p_slot, row->config.schedule, row->axes.epoch);
    row->rows.open_topology = facts;

    if (p_caller_selected || p_drive_tick >= 0) {
        if (!p_input_recorded) {
            record_input(p_slot, p_label, c_hash);
        }
        const bool selected_kind
            = p_caller_selected || p_kind == int(DriveKind::SUBSTITUTED);
        const predict::DriveRecord record = selected_kind
            ? replay_drive(
                  p_slot,
                  facts,
                  p_transition,
                  p_label,
                  p_kind,
                  p_drive_tick,
                  row->cursor.frame_index,
                  row->cursor.tick_delta,
                  row->cursor.declared_quantum,
                  pre_fp,
                  stamp[STAMP_POSE_FP],
                  stamp[STAMP_MOMENTUM_FP],
                  stamp[STAMP_CONTROLLER_FP],
                  raw_fp,
                  evidence_mask,
                  p_authoring
              )
            : open_drive(
                  p_slot,
                  facts,
                  p_drive_tick,
                  row->cursor.frame_index,
                  row->cursor.tick_delta,
                  row->cursor.declared_quantum,
                  true,
                  pre_fp,
                  stamp[STAMP_POSE_FP],
                  stamp[STAMP_MOMENTUM_FP],
                  stamp[STAMP_CONTROLLER_FP],
                  raw_fp,
                  evidence_mask
              );
        const int64_t opened = record.transition;
        if (record.ran) {
            drove[StringName("transition")] = opened;
            drove[StringName("label")] = record.label;
            drove[StringName("kind")] = int(record.kind);
            drove[StringName("fresh")] = record.fresh;
        }
        if (opened >= 0) {
            stamp_pending_provenance(p_slot, opened);
        }
    }
    if (is_new) {
        set_pending_provenance(p_slot, Dictionary());
    }
    adopt_environment_epoch(
        p_slot,
        row->axes.epoch,
        row->cursor.latest_input_tick,
        row->config.collision_cooldown_ticks
    );
    sample_environment(p_slot, row->axes.epoch);

    const int domain = prediction_core::domain_of(
        row->config.island_declared,
        row->config.island_approximate,
        p_label,
        out_of_domain_until(p_slot)
    );
    if (journal_has(p_slot, p_transition)) {
        mark_domain(p_slot, p_transition, domain);
    }
    NetwMultiplayer *host = core();
    if (!drove.is_empty() && host != nullptr
        && host->event_wants(EventPlane::PREDICT_DRIVE, 0)) {
        report_predict(p_slot, EventPlane::PREDICT_DRIVE, drove, Dictionary());
    }
    return drove;
}

Dictionary NetwPredictionEngine::select_comparison(
    int64_t p_slot,
    int64_t p_ack
) {
    Dictionary chosen;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return chosen;
    }
    if (row->config.schedule == int(Schedule::FRAME)) {
        const int64_t label = tape_label_of(p_slot, p_ack);
        if (label < 0) {
            return chosen;
        }
        const Ref<NetwTimeline> entries = entry_history(p_slot);
        const Dictionary predicted
            = entries.is_valid() ? entries->state_at(p_ack + 1) : Dictionary();
        if (predicted.is_empty()) {
            return chosen;
        }
        note_compare_staleness(p_slot, 0);
        chosen[StringName("label")] = label;
        chosen[StringName("predicted")] = predicted;
        return chosen;
    }
    const Ref<NetwTimeline> lane = timeline_of(p_slot);
    if (lane.is_null()) {
        return chosen;
    }
    const int64_t compared = lane->latest_state_tick_at_or_before(p_ack + 1);
    note_compare_staleness(
        p_slot,
        compared < 0 ? -1 : int(p_ack + 1 - compared)
    );
    chosen[StringName("label")] = p_ack;
    chosen[StringName("predicted")]
        = lane->latest_state_at_or_before(p_ack + 1);
    return chosen;
}

Dictionary NetwPredictionEngine::open_state_comparison(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_ack,
    const Dictionary &p_payload,
    const Callable &p_realign
) {
    Dictionary opened;
    if (p_ack < 0) {
        return opened;
    }
    NetwPredictionHandle *handle
        = Object::cast_to<NetwPredictionHandle>(handle_of(p_slot));
    if (ack_domain_confirmed_of(p_slot)) {
        const int age = mark_authority_ack(p_slot, p_ack);
        if (age >= 0 && handle != nullptr) {
            handle->set_ack_age_ticks(age);
        }
    }
    const int admission = admit_state(p_slot, p_ack);
    if (admission == ADMIT_REALIGN) {
        if (p_realign.is_valid()) {
            Array realigned;
            realigned.push_back(p_recv_tick);
            realigned.push_back(p_ack);
            realigned.push_back(p_payload);
            p_realign.callv(realigned);
        }
        return opened;
    }
    if (admission == ADMIT_REALIGN_PENDING
        || admission == ADMIT_RESEED_IGNORED) {
        if (handle != nullptr) {
            handle->emit_signal(
                StringName("state_evaluated"),
                p_recv_tick,
                p_ack,
                0.0,
                false
            );
        }
        return opened;
    }
    if (admission == ADMIT_CLOSED) {
        return opened;
    }
    const Dictionary chosen = select_comparison(p_slot, p_ack);
    if (chosen.is_empty()) {
        return opened;
    }
    const PackedInt64Array columns = open_comparison(p_slot, p_ack);
    if (columns.size() < COMPARE_COLUMN_COUNT) {
        return opened;
    }
    const bool reconstructed = columns[COMPARE_RECONSTRUCTED] != 0;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->compare_stats.comparisons_ran += reconstructed ? 1 : 0;
        row->compare_stats.comparisons_skipped += reconstructed ? 0 : 1;
    }
    opened[StringName("label")] = chosen[StringName("label")];
    opened[StringName("predicted")] = chosen[StringName("predicted")];
    opened[StringName("domain")] = columns[COMPARE_DOMAIN];
    opened[StringName("row_flags")] = columns[COMPARE_ROW_FLAGS];
    opened[StringName("episode_state")] = columns[COMPARE_EPISODE_STATE];
    opened[StringName("probation")] = columns[COMPARE_PROBATION] != 0;
    opened[StringName("reconstructed")] = reconstructed;
    return opened;
}

int NetwPredictionEngine::admit_state(int64_t p_slot, int64_t p_ack) {
    if (row_of(p_slot) == nullptr) {
        return ADMIT_CLOSED;
    }
    note_verdict_reason(p_slot, int(NetwPredict::VERDICT_REASON_NONE));
    clear_tier_errors(p_slot);
    if (reseed_align_pending(p_slot)) {
        if (reseed_epoch_confirmed(p_slot)) {
            return ADMIT_REALIGN;
        }
        note_verdict_reason(
            p_slot,
            int(NetwPredict::VERDICT_REASON_REALIGN_PENDING)
        );
        return ADMIT_REALIGN_PENDING;
    }
    if (!admit_post_reseed(p_slot, p_ack)) {
        note_verdict_reason(
            p_slot,
            int(NetwPredict::VERDICT_REASON_RESEED_IGNORED)
        );
        return ADMIT_RESEED_IGNORED;
    }
    return ADMIT_PROCEED;
}

PackedInt64Array NetwPredictionEngine::open_comparison(
    int64_t p_slot,
    int64_t p_ack
) {
    PackedInt64Array opened
        = gd::zeroed<PackedInt64Array>(COMPARE_COLUMN_COUNT);
    int64_t *values = opened.ptrw();
    values[COMPARE_DOMAIN] = int(NetwPredictJournal::OUT_OF_DOMAIN);
    values[COMPARE_EPISODE_STATE] = -1;
    if (row_of(p_slot) == nullptr) {
        return opened;
    }
    const int at = journal_slot_of(p_slot, p_ack);
    if (at >= 0) {
        values[COMPARE_DOMAIN] = journal_domain_at(p_slot, at);
        values[COMPARE_ROW_FLAGS] = journal_flags_at(p_slot, at);
    }
    values[COMPARE_EPISODE_STATE] = episode_state(p_slot);
    values[COMPARE_PROBATION] = probation_pending(p_slot) ? 1 : 0;
    const bool reconstructed = stream_reconstructed_of(p_slot);
    values[COMPARE_RECONSTRUCTED] = reconstructed ? 1 : 0;
    if (!reconstructed) {
        clear_divergence(p_slot);
        note_verdict_reason(
            p_slot,
            int(NetwPredict::VERDICT_REASON_AWAITING_RECONSTRUCTION)
        );
    }
    return opened;
}

int NetwPredictionEngine::refuse_recovery(int64_t p_slot, bool p_corrected) {
    const int none = int(NetwPredict::VERDICT_REASON_NONE);
    if (!p_corrected || row_of(p_slot) == nullptr) {
        return none;
    }
    int reason = none;
    if (episode_budget_exhausted(p_slot)) {
        reason = int(NetwPredict::VERDICT_REASON_EVIDENCE_EXHAUSTED);
    } else if (
        episode_operator_pending(
            p_slot,
            int(NetwPredictJournal::TRANSPORT_DELTA)
        )
    ) {
        reason = int(NetwPredict::VERDICT_REASON_TRANSPORT_PENDING);
    } else if (
        episode_operator_pending(p_slot, int(NetwPredictJournal::DISSIPATE))
    ) {
        reason = int(NetwPredict::VERDICT_REASON_DISSIPATE_PENDING);
    }
    if (reason != none) {
        note_verdict_reason(p_slot, reason);
    }
    return reason;
}

int NetwPredictionEngine::attribution_for(
    int64_t p_slot,
    int64_t p_transition
) const {
    const int unknown = int(NetwPredictJournal::UNKNOWN);
    if (row_of(p_slot) == nullptr) {
        return unknown;
    }
    const int at = journal_slot_of(p_slot, p_transition);
    if (at >= 0) {
        const int charged = journal_attribution_at(p_slot, at);
        if (charged != unknown) {
            return charged;
        }
    }
    if (attributed_transition_of(p_slot) == p_transition) {
        return attribution_of(p_slot);
    }
    if (at >= 0
        && (journal_flags_at(p_slot, at) & predict::ROW_SUBSTITUTED) != 0) {
        return int(NetwPredictJournal::COMMAND);
    }
    return unknown;
}

Dictionary NetwPredictionEngine::pose_errors_against(
    int64_t p_slot,
    const Dictionary &p_payload,
    int p_ack_age_ticks
) {
    Dictionary errors;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !has_pose_fields(p_slot)) {
        return errors;
    }
    const int span
        = std::clamp(p_ack_age_ticks, 0, row->config.max_restore_ticks);
    const double age = double(span) * row->cursor.tick_delta;
    return pose_errors(
        p_slot,
        canonicalize_state(p_slot, capture_state(p_slot)),
        project_state(p_slot, p_payload, age)
    );
}

int NetwPredictionEngine::trigger_shape(
    int64_t p_slot,
    double p_fallback_epsilon
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return int(predict::TriggerShape::NONE);
    }
    LocalVector<double> errors;
    errors.resize(uint32_t(row->wiring.count()));
    for (uint32_t at = 0; at < errors.size(); ++at) {
        errors[at] = row->report.divergence.at(int(at), -1.0);
    }
    return int(predict::trigger_shape(row->wiring, errors, p_fallback_epsilon));
}

void NetwPredictionEngine::record_restore(
    int64_t p_slot,
    const Dictionary &p_payload,
    int p_operator,
    int64_t p_basis,
    int64_t p_provenance_slot,
    const StringName &p_target,
    int p_ack_age_ticks,
    double p_divergence_epsilon,
    bool p_evidence_free,
    bool p_pool_planned
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    mark_carry_dirty(p_slot, drive_frontier(p_slot) + 1);
    if (p_operator == int(predict::Operator::NONE)) {
        return;
    }
    const int64_t owner
        = row_of(p_provenance_slot) != nullptr ? p_provenance_slot : p_slot;
    const PackedInt64Array stats = episode_stats(owner);
    const int64_t episode
        = stats[STAT_EPISODE_STATE] == int(predict::EpisodeState::OPEN)
        ? stats[STAT_EPISODE_ID]
        : int64_t(0);
    const int64_t delta_fp
        = NetwPredictJournal::fnv1a(canonical_state_bytes(p_slot, p_payload));
    int64_t write_id = 0;
    if (p_pool_planned) {
        write_id = stats[STAT_EPISODE_LAST_WRITE_ID];
        stamp_episode_write_delta(owner, int(delta_fp));
    } else {
        write_id = record_episode_write(
            owner,
            p_operator,
            p_basis,
            int(delta_fp),
            p_target,
            p_ack_age_ticks,
            trigger_shape(owner, p_divergence_epsilon),
            p_evidence_free,
            false
        );
    }
    Dictionary provenance;
    provenance[StringName("episode")] = episode;
    provenance[StringName("write_id")] = write_id;
    provenance[StringName("operator")] = p_operator;
    provenance[StringName("basis")] = p_basis;
    set_pending_provenance(p_slot, provenance);
}

namespace {

String carry_residual_text(const predict::CarryAttempt &p_attempt) {
    if (p_attempt.residual < 0.0) {
        return String();
    }
    return String::num(p_attempt.residual, 4)
        + String(" against a tolerance of ")
        + String::num(p_attempt.tolerance, 4);
}

String carry_infidelity_reason(int p_count, const String &p_residual) {
    String measured;
    if (!p_residual.is_empty()) {
        measured = String(" The last replay left a residual of ") + p_residual
            + String(".");
    }
    return String("It did not reproduce ") + String::num_int64(p_count)
        + String(" transitions the owner had already recorded.") + measured
        + String(
               " Two things produce that, and the second is the common one. "
               "Either the rule is not a function of its recorded "
               "NetwPredictCarryContext alone -- reading the live world or "
               "writing anything makes it disagree with the history it is "
               "replayed against -- or this field is one the SOLVER also "
               "moves, "
               "in which case the residual IS the solver's contribution and no "
               "rule can reproduce it. carry_step() advances a field whose "
               "whole "
               "change the game authors; a field the solver shares is not one "
               "of "
               "those, however correct the rule is about the game's own share."
        );
}

void warn_carry_retired(const StringName &p_field, const String &p_reason) {
    NETW_WARN(
        sys::PREDICTION,
        "Prediction: the carry_step() rule for '%s' is retired, so "
        "every recovery now writes the acknowledged value. %s",
        String(p_field).utf8().get_data(),
        p_reason.utf8().get_data()
    );
}

} // namespace

Dictionary NetwPredictionEngine::carry_payload(
    int64_t p_slot,
    const Dictionary &p_payload,
    int64_t p_basis,
    double p_teleport_default,
    double p_divergence_epsilon
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return p_payload;
    }
    row->carry_attempts.clear();
    if (row->carry_rules.is_empty()) {
        return p_payload;
    }
    Dictionary out = p_payload;
    bool copied = false;
    const Array fields = carry_rule_fields(p_slot);
    for (int at = 0; at < fields.size(); ++at) {
        const StringName field = fields[at];
        if (!p_payload.has(field)) {
            continue;
        }
        predict::CarryAttempt charged;
        charged.field = field;
        if (!carry_eligible(p_slot, field)) {
            charged.verdict = decline_carry(p_slot, field);
            row->carry_attempts.push_back(charged);
            ledger_seed(p_slot, field);
            continue;
        }
        const predict::CarryAttempt attempt = attempt_carry(
            p_slot,
            field,
            p_payload[field],
            p_basis,
            p_teleport_default,
            p_divergence_epsilon
        );
        if (!attempt.evidence) {
            charged.verdict = decline_carry(p_slot, field);
            row->carry_attempts.push_back(charged);
            ledger_seed(p_slot, field);
            continue;
        }
        charged = attempt;
        charged.field = field;
        charged.verdict = judge_carry(
            p_slot,
            field,
            attempt.probe.same_type,
            attempt.probe.finite,
            attempt.probe.within_envelope,
            attempt.probe.pure,
            attempt.probe.faithful
        );
        row->carry_attempts.push_back(charged);
        ledger_seed(p_slot, field);
        if (charged.verdict != CARRY_CARRIED) {
            continue;
        }
        if (!copied) {
            out = p_payload.duplicate();
            copied = true;
        }
        out[field] = attempt.value;
    }
    report_carry_retirements(p_slot);
    return out;
}

void NetwPredictionEngine::report_carry_retirements(int64_t p_slot) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < row->carry_attempts.size(); ++at) {
        const predict::CarryAttempt &charged = row->carry_attempts[at];
        switch (charged.verdict) {
            case CARRY_RETIRED_INFIDELITY:
                warn_carry_retired(
                    charged.field,
                    carry_infidelity_reason(
                        int(carry_stats(p_slot, charged.field)[2]),
                        carry_residual_text(charged)
                    )
                );
                break;
            case CARRY_RETIRED_SCHEDULE:
                warn_carry_retired(
                    charged.field,
                    String(
                        "It needs prediction.schedule = FRAME: the tick tier "
                        "re-anchors its own state record to authority on every "
                        "correction, so too little of it is the owner's own to "
                        "replay a rule against. The rule itself is not in "
                        "question."
                    )
                );
                break;
            case CARRY_RETIRED_IMPURE:
                warn_carry_retired(
                    charged.field,
                    String(
                        "It wrote to the body it is supposed to describe. A "
                        "rule "
                        "is a function of its recorded NetwPredictCarryContext "
                        "alone: reading the live world or writing anything "
                        "makes "
                        "it disagree with the history it is replayed against."
                    )
                );
                break;
            default:
                break;
        }
    }
}

LocalVector<predict::CarryAttempt> NetwPredictionEngine::carry_attempts(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->carry_attempts
                          : LocalVector<predict::CarryAttempt>();
}

void NetwPredictionEngine::reset_for_rewire(
    int64_t p_slot,
    bool p_was_wired,
    bool p_same_stream,
    bool p_raw_fingerprints,
    const Dictionary &p_stall_input
) {
    if (row_of(p_slot) == nullptr) {
        return;
    }
    set_cooldown_until_tick(p_slot, -1);
    predict::Slot *rewired = mutable_row_of(p_slot);
    rewired->compare_stats.fp_verified = 0;
    rewired->compare_stats.fp_mismatches = 0;
    rewired->compare_stats.first_divergent_transition = -1;
    ledger_clear(p_slot);
    set_pending_provenance(p_slot, Dictionary());
    clear_witness_details(p_slot);
    set_open_topology(p_slot, Dictionary());
    set_previous_witness_sleeping(p_slot, false);
    set_has_previous_witness(p_slot, false);
    set_invalid_witness_reported(p_slot, false);
    set_invalid_command_predictor_reported(p_slot, false);
    set_joint_refusal_reported(p_slot, false);
    set_stepper_absence_reported(p_slot, false);
    clear_authority_witness_classes(p_slot);
    drop_deferred_operator(p_slot);
    set_raw_fingerprints(p_slot, p_raw_fingerprints);
    set_latest_input_tick(p_slot, -1);
    set_last_driven_input_tick(p_slot, -1);
    set_last_frame_transition_tick(p_slot, -1);
    set_last_recorded_input_tick(p_slot, -1);
    set_frame_input(p_slot, Dictionary());
    set_stall_input(p_slot, p_stall_input);
    set_tape_epoch(p_slot, (tape_epoch_of(p_slot) + 1) & 0xFF);
    set_next_tape_entry_index(p_slot, 0);
    set_last_driven_entry_index(p_slot, -1);
    set_last_recorded_entry_index(p_slot, -1);
    if (!p_same_stream) {
        set_stream_reconstructed(p_slot, false);
    }
    clear_out_of_domain_window(p_slot);
    clear_environment_epoch(p_slot);
    set_command_epoch(p_slot, -1);
    set_ack_of_acks(p_slot, -1);
    if (p_was_wired) {
        set_ack_domain_confirmed(p_slot, false);
    }
    set_owner_ack_floor(p_slot, -1);
    note_attribution(p_slot, int(NetwPredictJournal::UNKNOWN), -1);
    set_replay_cursor(p_slot, -1);
    set_last_replayed_label(p_slot, -1);
    set_last_replayed_fresh(p_slot, false);
}

bool NetwPredictionEngine::declared_authority_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->axes.authority;
}

bool NetwPredictionEngine::declared_controlled_locally_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->axes.controlled_locally;
}

void NetwPredictionEngine::bind_property_sets(
    int64_t p_slot,
    const Ref<NetwPropertySetBinding> &p_state,
    const Ref<NetwPropertySetBinding> &p_input
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->state_binding = p_state;
        row->input_binding = p_input;
    }
}

Ref<NetwPropertySetBinding> NetwPredictionEngine::state_binding_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Ref<NetwPropertySetBinding>() : row->state_binding;
}

Ref<NetwPropertySetBinding> NetwPredictionEngine::input_binding_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Ref<NetwPropertySetBinding>() : row->input_binding;
}

Ref<NetwTimeline> NetwPredictionEngine::timeline_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? Ref<NetwTimeline>() : row->timeline;
}

void NetwPredictionEngine::trim_history(int64_t p_slot, int64_t p_ack) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "History trim names closed slot %d.",
        p_slot
    );
    row->trim_history(p_ack);
}

bool NetwPredictionEngine::command_admit(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label,
    bool p_fresh,
    const Dictionary &p_command
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Command cell names closed slot %d.",
        p_slot
    );
    return row->commands.admit(p_transition, p_label, p_fresh, p_command);
}

bool NetwPredictionEngine::command_has(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->commands.has(p_transition);
}

int64_t NetwPredictionEngine::command_label_of(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::CommandCell *cell
        = row != nullptr ? row->commands.cell(p_transition) : nullptr;
    return cell != nullptr ? cell->label : -1;
}

bool NetwPredictionEngine::command_is_fresh(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::CommandCell *cell
        = row != nullptr ? row->commands.cell(p_transition) : nullptr;
    return cell != nullptr && cell->fresh;
}

Dictionary NetwPredictionEngine::command_payload_of(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    const predict::CommandCell *cell
        = row != nullptr ? row->commands.cell(p_transition) : nullptr;
    return cell != nullptr ? cell->command : Dictionary();
}

int NetwPredictionEngine::command_depth_from(
    int64_t p_slot,
    int64_t p_cursor
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->commands.depth_from(p_cursor) : 0;
}

PackedInt64Array NetwPredictionEngine::command_transitions(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->commands.transitions() : PackedInt64Array();
}

void NetwPredictionEngine::record_idle_drive(
    int64_t p_slot,
    int64_t p_label,
    int p_kind
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->record_idle_drive(p_label, DriveKind(p_kind));
    }
}

void NetwPredictionEngine::record_authoring_clamp(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->record_authoring_clamp();
    }
}

void NetwPredictionEngine::record_speculation_hold(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->record_speculation_hold();
    }
}

void NetwPredictionEngine::refresh_ack_age(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->refresh_ack_age();
    }
}

int NetwPredictionEngine::mark_authority_ack(
    int64_t p_slot,
    int64_t p_transition
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return -1;
    }
    row->mark_authority_ack(p_transition);
    return int(row->stats.ack_age_ticks);
}

bool NetwPredictionEngine::journal_has(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->journal.slot_of(p_transition) >= 0;
}

PackedInt64Array NetwPredictionEngine::drive_cursors(int64_t p_slot) const {
    PackedInt64Array out;
    out.resize(CURSOR_COUNT);
    const predict::Slot *row = row_of(p_slot);
    const predict::Slot absent;
    const predict::Slot &slot = row != nullptr ? *row : absent;
    int64_t *values = out.ptrw();
    values[CURSOR_TAPE_EPOCH] = slot.tape_epoch;
    values[CURSOR_NEXT_TAPE_ENTRY] = slot.next_tape_entry_index;
    values[CURSOR_LAST_DRIVEN_ENTRY] = slot.last_driven_entry_index;
    values[CURSOR_LATEST_INPUT_TICK] = slot.latest_input_tick;
    values[CURSOR_LAST_DRIVEN_INPUT_TICK] = slot.last_driven_input_tick;
    values[CURSOR_LAST_FRAME_TRANSITION_TICK] = slot.last_frame_transition_tick;
    return out;
}

PackedInt64Array NetwPredictionEngine::drive_stats(int64_t p_slot) const {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(STAT_COUNT);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::DriveStats &stats = row->stats;
    int64_t *values = out.ptrw();
    values[STAT_DRIVE_SEQ] = stats.drive_seq;
    values[STAT_LAST_DRIVE_LABEL] = stats.last_drive_label;
    values[STAT_LAST_DRIVE_KIND] = int64_t(stats.last_drive_kind);
    values[STAT_TAPE_EPOCH] = stats.tape_epoch;
    values[STAT_TAPE_INDEX] = stats.tape_index;
    values[STAT_ACK_AGE_TICKS] = stats.ack_age_ticks;
    values[STAT_AUTHORING_CLAMPED] = stats.authoring_clamped;
    values[STAT_SPECULATION_HELD] = stats.speculation_held;
    values[STAT_CHAIN_BREAKS] = stats.chain_breaks;
    values[STAT_QUANTUM_STEPS] = stats.quantum_steps;
    values[STAT_QUANTUM_DECLARED] = stats.quantum_declared;
    values[STAT_QUANTUM_FAULTS] = stats.quantum_faults;
    values[STAT_OWNER_LOST] = row->port.lost;
    values[STAT_MAX_REPLAY_DEPTH] = stats.max_replay_depth;
    return out;
}

PackedInt64Array NetwPredictionEngine::compare_stats(int64_t p_slot) const {
    PackedInt64Array out = gd::zeroed<PackedInt64Array>(STAT_COMPARE_COUNT);
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::CompareStats &stats = row->compare_stats;
    int64_t *values = out.ptrw();
    values[STAT_COMPARISONS_RAN] = stats.comparisons_ran;
    values[STAT_COMPARISONS_SKIPPED] = stats.comparisons_skipped;
    values[STAT_FP_VERIFIED] = stats.fp_verified;
    values[STAT_FP_MISMATCHES] = stats.fp_mismatches;
    values[STAT_FIRST_DIVERGENT_TRANSITION] = stats.first_divergent_transition;
    return out;
}

} // namespace netw

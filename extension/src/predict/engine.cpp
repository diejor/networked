#include "netw/predict/engine.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/predict/axes.hpp"
#include "netw/predict/frames.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_EPISODE_OPENED = "episode_opened";
const char *SIG_EPISODE_CLOSED = "episode_closed";
const char *SIG_EPISODE_FALLBACK = "episode_fallback";
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

Ref<NetwPredictEpisodeReport> NetwPredictionEngine::report_of(
    const predict::Episode &p_episode
) {
    Ref<NetwPredictEpisodeReport> out;
    if (p_episode.id <= 0) {
        return out;
    }
    out.instantiate();
    out->episode = p_episode;
    return out;
}

Ref<NetwPredictWritePlan> NetwPredictionEngine::plan_of(
    const predict::WritePlan &p_plan
) {
    Ref<NetwPredictWritePlan> out;
    out.instantiate();
    out->plan = p_plan;
    return out;
}

Ref<NetwPredictJointPlan> NetwPredictionEngine::joint_plan_of(
    const predict::JointPassPlan &p_plan
) {
    Ref<NetwPredictJointPlan> out;
    out.instantiate();
    out->plan = p_plan;
    return out;
}

predict::RecoveryRequest NetwPredictRecoveryRequest::build(int p_width) const {
    predict::RecoveryRequest out;
    out.predicted = state_row(predicted, p_width);
    out.authority = state_row(authority, p_width);
    out.current = state_row(current, p_width);
    out.field_errors = tolerance_row(field_errors, p_width);
    out.basis = basis;
    out.current_label = current_label;
    out.policy = policy;
    out.fallback_epsilon = fallback_epsilon;
    out.fallback_teleport = fallback_teleport;
    out.max_restore_ticks = max_restore_ticks;
    out.ack_age_ticks = ack_age_ticks;
    out.collision_cooldown_ticks = collision_cooldown_ticks;
    out.tick_delta = tick_delta;
    out.domain = predict::Domain(domain);
    out.attribution = predict::Attribution(attribution);
    out.contact_window = contact_window;
    out.suppressed = suppressed;
    out.pose_unmeasured = pose_unmeasured;
    return out;
}

void NetwPredictDeclaration::append_field(
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
    fields.push_back(decl);
}

int NetwPredictDeclaration::field_count() const {
    return int(fields.size());
}

StringName NetwPredictDeclaration::field_at(int p_index) const {
    if (p_index < 0 || p_index >= int(fields.size())) {
        return StringName();
    }
    return fields[uint32_t(p_index)].key;
}

void NetwPredictDeclaration::clear() {
    fields.clear();
}

void NetwPredictDeclaration::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD(
            "append_field",
            "key",
            "property_class",
            "carry_channel",
            "converge_stiffness",
            "teleport_only",
            "reconcile_only",
            "epsilon_override",
            "teleport_at",
            "angle",
            "quantizer",
            "type"
        ),
        &NetwPredictDeclaration::append_field,
        DEFVAL(StringName()),
        DEFVAL(0.0),
        DEFVAL(false),
        DEFVAL(false),
        DEFVAL(-1.0),
        DEFVAL(-1.0),
        DEFVAL(false),
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(int(Variant::NIL))
    );
    ClassDB::bind_method(
        D_METHOD("field_count"),
        &NetwPredictDeclaration::field_count
    );
    ClassDB::bind_method(
        D_METHOD("field_at", "index"),
        &NetwPredictDeclaration::field_at
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwPredictDeclaration::clear);
}

void NetwPredictRecoveryRequest::fill(
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
    NETW_ERR_COND(
        p_policy < int(RecoveryPolicy::REBASE_REPLAY)
            || p_policy > int(RecoveryPolicy::OBSERVE),
        "predict",
        "Recovery policy %d is outside the declared range.",
        p_policy
    );
    NETW_ERR_COND(
        p_domain < int(predict::Domain::IN_DOMAIN)
            || p_domain > int(predict::Domain::OUT_OF_DOMAIN),
        "predict",
        "Recovery domain %d is outside the declared range.",
        p_domain
    );
    NETW_ERR_COND(
        p_attribution < int(predict::Attribution::UNKNOWN)
            || p_attribution > int(predict::Attribution::CLOSURE),
        "predict",
        "Recovery attribution %d is outside the declared range.",
        p_attribution
    );
    NETW_ERR_COND(
        p_tick_delta <= 0.0,
        "predict",
        "Recovery tick delta must be positive."
    );
    predicted = p_predicted;
    authority = p_authority;
    current = p_current;
    field_errors = p_field_errors;
    basis = p_basis;
    current_label = p_current_label;
    policy = p_policy;
    fallback_epsilon = p_fallback_epsilon;
    fallback_teleport = p_fallback_teleport;
    max_restore_ticks = p_max_restore_ticks;
    ack_age_ticks = p_ack_age_ticks;
    collision_cooldown_ticks = p_collision_cooldown_ticks;
    tick_delta = p_tick_delta;
    domain = p_domain;
    attribution = p_attribution;
    contact_window = p_contact_window;
    suppressed = p_suppressed;
    pose_unmeasured = p_pose_unmeasured;
}

void NetwPredictRecoveryRequest::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD(
            "fill",
            "predicted",
            "authority",
            "current",
            "field_errors",
            "basis",
            "current_label",
            "policy",
            "fallback_epsilon",
            "fallback_teleport",
            "max_restore_ticks",
            "ack_age_ticks",
            "collision_cooldown_ticks",
            "tick_delta",
            "domain",
            "attribution",
            "contact_window",
            "suppressed",
            "pose_unmeasured"
        ),
        &NetwPredictRecoveryRequest::fill
    );
}

int64_t NetwPredictWritePlan::basis() const {
    return plan.basis;
}

int64_t NetwPredictWritePlan::replay_from() const {
    return plan.replay_from;
}

int64_t NetwPredictWritePlan::replay_through() const {
    return plan.replay_through;
}

int NetwPredictWritePlan::operator_kind() const {
    return int(plan.op);
}

bool NetwPredictWritePlan::teleport() const {
    return plan.teleport;
}

bool NetwPredictWritePlan::skip() const {
    return plan.skip;
}

bool NetwPredictWritePlan::escalated() const {
    return plan.escalated;
}

bool NetwPredictWritePlan::restore_has(int p_field) const {
    return plan.restore.has(p_field);
}

Variant NetwPredictWritePlan::restore_at(int p_field) const {
    return plan.restore.has(p_field) ? plan.restore.values[uint32_t(p_field)]
                                     : Variant();
}

bool NetwPredictWritePlan::write_has(int p_field) const {
    return plan.write.has(p_field);
}

Variant NetwPredictWritePlan::write_at(int p_field) const {
    return plan.write.has(p_field) ? plan.write.values[uint32_t(p_field)]
                                   : Variant();
}

void NetwPredictWritePlan::_bind_methods() {
    ClassDB::bind_method(D_METHOD("basis"), &NetwPredictWritePlan::basis);
    ClassDB::bind_method(
        D_METHOD("replay_from"),
        &NetwPredictWritePlan::replay_from
    );
    ClassDB::bind_method(
        D_METHOD("replay_through"),
        &NetwPredictWritePlan::replay_through
    );
    ClassDB::bind_method(
        D_METHOD("operator_kind"),
        &NetwPredictWritePlan::operator_kind
    );
    ClassDB::bind_method(D_METHOD("teleport"), &NetwPredictWritePlan::teleport);
    ClassDB::bind_method(D_METHOD("skip"), &NetwPredictWritePlan::skip);
    ClassDB::bind_method(
        D_METHOD("escalated"),
        &NetwPredictWritePlan::escalated
    );
    ClassDB::bind_method(
        D_METHOD("restore_has", "field"),
        &NetwPredictWritePlan::restore_has
    );
    ClassDB::bind_method(
        D_METHOD("restore_at", "field"),
        &NetwPredictWritePlan::restore_at
    );
    ClassDB::bind_method(
        D_METHOD("write_has", "field"),
        &NetwPredictWritePlan::write_has
    );
    ClassDB::bind_method(
        D_METHOD("write_at", "field"),
        &NetwPredictWritePlan::write_at
    );
}

int64_t NetwPredictJointPlan::floor() const {
    return plan.floor;
}

int64_t NetwPredictJointPlan::present() const {
    return plan.present;
}

bool NetwPredictJointPlan::heal() const {
    return plan.heal;
}

bool NetwPredictJointPlan::valid() const {
    return plan.valid;
}

int NetwPredictJointPlan::restore_count() const {
    return int(plan.restores.size());
}

int64_t NetwPredictJointPlan::restore_slot(int p_index) const {
    return p_index >= 0 && p_index < int(plan.restores.size())
        ? plan.restores[uint32_t(p_index)].slot
        : 0;
}

bool NetwPredictJointPlan::restore_has(int p_index, int p_field) const {
    return p_index >= 0 && p_index < int(plan.restores.size())
        && plan.restores[uint32_t(p_index)].state.has(p_field);
}

Variant NetwPredictJointPlan::restore_at(int p_index, int p_field) const {
    return restore_has(p_index, p_field)
        ? plan.restores[uint32_t(p_index)].state.values[uint32_t(p_field)]
        : Variant();
}

int NetwPredictJointPlan::step_count() const {
    return int(plan.steps.size());
}

int64_t NetwPredictJointPlan::step_slot(int p_index) const {
    return p_index >= 0 && p_index < int(plan.steps.size())
        ? plan.steps[uint32_t(p_index)].slot
        : 0;
}

int64_t NetwPredictJointPlan::step_transition(int p_index) const {
    return p_index >= 0 && p_index < int(plan.steps.size())
        ? plan.steps[uint32_t(p_index)].transition
        : -1;
}

Variant NetwPredictJointPlan::step_command(int p_index) const {
    return p_index >= 0 && p_index < int(plan.steps.size())
        ? plan.steps[uint32_t(p_index)].command
        : Variant();
}

int NetwPredictJointPlan::step_provenance(int p_index) const {
    return p_index >= 0 && p_index < int(plan.steps.size())
        ? int(plan.steps[uint32_t(p_index)].provenance)
        : -1;
}

void NetwPredictJointPlan::_bind_methods() {
    ClassDB::bind_method(D_METHOD("floor"), &NetwPredictJointPlan::floor);
    ClassDB::bind_method(D_METHOD("present"), &NetwPredictJointPlan::present);
    ClassDB::bind_method(D_METHOD("heal"), &NetwPredictJointPlan::heal);
    ClassDB::bind_method(D_METHOD("valid"), &NetwPredictJointPlan::valid);
    ClassDB::bind_method(
        D_METHOD("restore_count"),
        &NetwPredictJointPlan::restore_count
    );
    ClassDB::bind_method(
        D_METHOD("restore_slot", "index"),
        &NetwPredictJointPlan::restore_slot
    );
    ClassDB::bind_method(
        D_METHOD("restore_has", "index", "field"),
        &NetwPredictJointPlan::restore_has
    );
    ClassDB::bind_method(
        D_METHOD("restore_at", "index", "field"),
        &NetwPredictJointPlan::restore_at
    );
    ClassDB::bind_method(
        D_METHOD("step_count"),
        &NetwPredictJointPlan::step_count
    );
    ClassDB::bind_method(
        D_METHOD("step_slot", "index"),
        &NetwPredictJointPlan::step_slot
    );
    ClassDB::bind_method(
        D_METHOD("step_transition", "index"),
        &NetwPredictJointPlan::step_transition
    );
    ClassDB::bind_method(
        D_METHOD("step_command", "index"),
        &NetwPredictJointPlan::step_command
    );
    ClassDB::bind_method(
        D_METHOD("step_provenance", "index"),
        &NetwPredictJointPlan::step_provenance
    );
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
    const Ref<NetwPredictDeclaration> &p_declaration
) {
    if (!roster_open("open")) {
        return -1;
    }
    const int64_t slot = next_slot;
    next_slot += 1;
    predict::Slot row;
    row.open = true;
    row.reset_entry_history();
    if (p_declaration.is_valid()) {
        row.rewire(predict::compile(p_declaration->fields));
    }
    rows.insert(slot, row);
    return slot;
}

void NetwPredictionEngine::rewire(
    int64_t p_slot,
    const Ref<NetwPredictDeclaration> &p_declaration,
    const Ref<NetwPredictDeclaration> &p_input
) {
    if (!roster_open("rewire")) {
        return;
    }
    HashMap<int64_t, predict::Slot>::Iterator found = rows.find(p_slot);
    if (found == rows.end() || !found->value.open) {
        return;
    }
    found->value.rewire(
        p_declaration.is_valid() ? predict::compile(p_declaration->fields)
                                 : predict::Wiring()
    );
    found->value.input_codec.clear();
    if (p_input.is_valid()) {
        for (uint32_t at = 0; at < p_input->fields.size(); ++at) {
            found->value.input_codec.append(
                p_input->fields[at].key,
                p_input->fields[at].quantizer,
                p_input->fields[at].type
            );
        }
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
    row->owner_solves = p_owner->is_class("RigidBody2D")
        || p_owner->is_class("RigidBody3D");
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
    return p_row.port.resolve("prediction");
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

Dictionary NetwPredictionEngine::capture_state(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        ? capture_through(*row, row->wiring.codec, row->owner_has_state)
        : Dictionary();
}

Dictionary NetwPredictionEngine::capture_input(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        ? capture_through(*row, row->input_codec, row->owner_has_input)
        : Dictionary();
}

bool NetwPredictionEngine::apply_state(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        && apply_through(
            *row, row->wiring.codec, row->owner_has_state, p_payload
        );
}

bool NetwPredictionEngine::apply_input(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        && apply_through(
            *row, row->input_codec, row->owner_has_input, p_payload
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

void NetwPredictionEngine::set_order_key(
    int64_t p_slot,
    int64_t p_order_key
) {
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
    return p_role == int(Role::PREDICT)
        || p_role == int(Role::CONSUME)
        || p_role == int(Role::HOST_LOCAL);
}

bool steps_in_phase(const predict::Slot &p_row, int p_phase) {
    const int role = p_row.config.role;
    const int schedule = p_row.config.schedule;
    const bool tick = schedule == int(Schedule::TICK);
    const bool frame = schedule == int(Schedule::FRAME);
    const bool authoring_fallback = role == int(Role::REMOTE)
        && p_row.quarantine.latched;
    switch (p_phase) {
        case NetwPredictionEngine::PASS_ISLAND_TICK:
            return tick && authors_pass(role);
        case NetwPredictionEngine::PASS_ISLAND_FRAME:
            return frame && authors_pass(role);
        case NetwPredictionEngine::PASS_JOINT:
            return p_row.config.island == NetwPredictionEngine::ISLAND_JOINT
                && authors_pass(role);
        case NetwPredictionEngine::PASS_TICK:
            return tick || role == int(Role::PREDICT)
                || role == int(Role::HOST_LOCAL)
                || authoring_fallback;
        case NetwPredictionEngine::PASS_FRAME:
            return frame
                && (authors_pass(role) || authoring_fallback
                    || role == int(Role::SIMULATE));
        case NetwPredictionEngine::PASS_FINALIZE_FRAME:
            return frame && role == int(Role::PREDICT);
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
    apply_through(*row, row->input_codec, row->owner_has_input, p_input);
    const Callable step = row->simulate;
    depth += 1;
    if (step.is_valid()) {
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
    return NetwPredictionCore::environment_digest(p_epoch, samples);
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
        HashMap<int64_t, Ref<RefCounted>>::ConstIterator seated =
            entity_by_slot.find(p_slot);
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
    const int64_t slot = open(Ref<NetwPredictDeclaration>());
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
    HashMap<uint64_t, int64_t>::ConstIterator seated =
        slot_by_entity.find(p_entity->get_instance_id());
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
    int p_island,
    bool p_carry,
    bool p_witness
) const {
    (void)p_carry;
    (void)p_witness;
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

bool NetwPredictionEngine::configure(
    int64_t p_slot,
    int p_schedule,
    int p_role,
    int p_correction,
    int p_restore,
    int p_max_restore_ticks,
    int p_island,
    bool p_carry,
    bool p_witness,
    bool p_island_declared,
    bool p_island_approximate
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr
        || !supports(
            p_schedule,
            p_role,
            p_correction,
            p_restore,
            p_island,
            p_carry,
            p_witness
        )) {
        return false;
    }
    row->config.schedule = p_schedule;
    row->config.role = p_role;
    row->config.correction = p_correction;
    row->config.restore = p_restore;
    row->config.max_restore_ticks = std::max(0, p_max_restore_ticks);
    row->config.island = p_island;
    row->config.carry = p_carry;
    row->config.witness = p_witness;
    row->config.island_declared = p_island_declared;
    row->config.island_approximate = p_island_approximate;
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
        row->out_of_domain_until = NetwPredictionCore::window_after(
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

int NetwPredictionEngine::island_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->config.island : ISLAND_NONE;
}

Ref<NetwPredictConsumePlan> NetwPredictionEngine::plan_consume_input(
    int p_schedule,
    bool p_has_input,
    bool p_has_later_input,
    bool p_has_last_input,
    int p_missing_policy
) const {
    NETW_ERR_COND_V(
        p_schedule != int(Schedule::TICK) && p_schedule != int(Schedule::FRAME),
        Ref<NetwPredictConsumePlan>(),
        sys::PREDICTION,
        "A consume input plan requires a TICK or FRAME schedule."
    );
    NETW_ERR_COND_V(
        p_missing_policy < int(MissingInput::STALL)
            || p_missing_policy > int(MissingInput::REPEAT_LAST),
        Ref<NetwPredictConsumePlan>(),
        sys::PREDICTION,
        "The missing input policy is outside the declared enum."
    );
    Ref<NetwPredictConsumePlan> out;
    out.instantiate();
    out->plan = predict::plan_consume_input(
        Schedule(p_schedule),
        p_has_input,
        p_has_later_input,
        p_has_last_input,
        MissingInput(p_missing_policy)
    );
    return out;
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

void NetwPredictDrive::_bind_methods() {
    ClassDB::bind_method(D_METHOD("transition"), &NetwPredictDrive::transition);
    ClassDB::bind_method(D_METHOD("label"), &NetwPredictDrive::label);
    ClassDB::bind_method(D_METHOD("kind"), &NetwPredictDrive::kind);
    ClassDB::bind_method(D_METHOD("ran"), &NetwPredictDrive::ran);
    ClassDB::bind_method(D_METHOD("fresh"), &NetwPredictDrive::fresh);
    ClassDB::bind_method(D_METHOD("held"), &NetwPredictDrive::held);
    ClassDB::bind_method(D_METHOD("clamped"), &NetwPredictDrive::clamped);
}

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
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "state"),
        "",
        "get_state"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "input"),
        "",
        "get_input"
    );
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "delta"), "", "get_delta");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "label"), "", "get_label");
}

void NetwPredictCarryAttempt::_bind_methods() {
    ClassDB::bind_method(D_METHOD("value"), &NetwPredictCarryAttempt::value);
    ClassDB::bind_method(
        D_METHOD("evidence"),
        &NetwPredictCarryAttempt::evidence
    );
    ClassDB::bind_method(
        D_METHOD("same_type"),
        &NetwPredictCarryAttempt::same_type
    );
    ClassDB::bind_method(D_METHOD("finite"), &NetwPredictCarryAttempt::finite);
    ClassDB::bind_method(
        D_METHOD("within_envelope"),
        &NetwPredictCarryAttempt::within_envelope
    );
    ClassDB::bind_method(D_METHOD("pure"), &NetwPredictCarryAttempt::pure);
    ClassDB::bind_method(
        D_METHOD("faithful"),
        &NetwPredictCarryAttempt::faithful
    );
    ClassDB::bind_method(
        D_METHOD("residual"),
        &NetwPredictCarryAttempt::residual
    );
    ClassDB::bind_method(
        D_METHOD("tolerance"),
        &NetwPredictCarryAttempt::tolerance
    );
}

void NetwPredictReplayEntry::_bind_methods() {
    ClassDB::bind_method(D_METHOD("index"), &NetwPredictReplayEntry::index);
    ClassDB::bind_method(D_METHOD("label"), &NetwPredictReplayEntry::label);
    ClassDB::bind_method(D_METHOD("input"), &NetwPredictReplayEntry::input);
}

bool NetwPredictConsumePlan::eligible() const {
    return plan.eligible;
}

bool NetwPredictConsumePlan::missing() const {
    return plan.missing;
}

bool NetwPredictConsumePlan::run() const {
    return plan.run;
}

bool NetwPredictConsumePlan::use_last() const {
    return plan.use_last;
}

int NetwPredictConsumePlan::kind() const {
    return int(plan.kind);
}

void NetwPredictConsumePlan::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("eligible"),
        &NetwPredictConsumePlan::eligible
    );
    ClassDB::bind_method(D_METHOD("missing"), &NetwPredictConsumePlan::missing);
    ClassDB::bind_method(D_METHOD("run"), &NetwPredictConsumePlan::run);
    ClassDB::bind_method(
        D_METHOD("use_last"),
        &NetwPredictConsumePlan::use_last
    );
    ClassDB::bind_method(D_METHOD("kind"), &NetwPredictConsumePlan::kind);
}

void NetwPredictEvidence::fill(
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
}

void NetwPredictEvidence::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD(
            "fill",
            "pre_fp",
            "c_hash",
            "e_digest",
            "post_fp",
            "pre_pose_fp",
            "pre_momentum_fp",
            "pre_controller_fp",
            "post_pose_fp",
            "post_momentum_fp",
            "post_controller_fp",
            "topo_fp",
            "witness_fp",
            "raw_fp",
            "evidence_mask",
            "complete"
        ),
        &NetwPredictEvidence::fill
    );
}

#define NETW_JOURNAL_ROW_GETTER(m_name, m_type) \
    m_type NetwPredictJournalRow::m_name() const { \
        return m_name##_value; \
    }

NETW_JOURNAL_ROW_GETTER(transition, int64_t)
NETW_JOURNAL_ROW_GETTER(label, int64_t)
NETW_JOURNAL_ROW_GETTER(kind, int)
NETW_JOURNAL_ROW_GETTER(c_hash, int64_t)
NETW_JOURNAL_ROW_GETTER(e_digest, int64_t)
NETW_JOURNAL_ROW_GETTER(pre_fp, int64_t)
NETW_JOURNAL_ROW_GETTER(topo_fp, int64_t)
NETW_JOURNAL_ROW_GETTER(raw_fp, int64_t)
NETW_JOURNAL_ROW_GETTER(witness_fp, int64_t)
NETW_JOURNAL_ROW_GETTER(witness_class_bits, int)
NETW_JOURNAL_ROW_GETTER(aligned_error, double)
NETW_JOURNAL_ROW_GETTER(evidence_mask, int)
NETW_JOURNAL_ROW_GETTER(pre_families, PackedInt32Array)
NETW_JOURNAL_ROW_GETTER(post_fp, int64_t)
NETW_JOURNAL_ROW_GETTER(post_families, PackedInt32Array)
NETW_JOURNAL_ROW_GETTER(episode_id, int)
NETW_JOURNAL_ROW_GETTER(write_id, int)
NETW_JOURNAL_ROW_GETTER(basis, int64_t)
NETW_JOURNAL_ROW_GETTER(differing_family, int)
NETW_JOURNAL_ROW_GETTER(domain, int)
NETW_JOURNAL_ROW_GETTER(attribution, int)
NETW_JOURNAL_ROW_GETTER(flags, int)

#undef NETW_JOURNAL_ROW_GETTER

int NetwPredictJournalRow::op() const {
    return operator_value;
}

void NetwPredictJournalRow::_bind_methods() {
#define NETW_BIND_JOURNAL_ROW(m_name) \
    ClassDB::bind_method(D_METHOD(#m_name), &NetwPredictJournalRow::m_name)

    NETW_BIND_JOURNAL_ROW(transition);
    NETW_BIND_JOURNAL_ROW(label);
    NETW_BIND_JOURNAL_ROW(kind);
    NETW_BIND_JOURNAL_ROW(c_hash);
    NETW_BIND_JOURNAL_ROW(e_digest);
    NETW_BIND_JOURNAL_ROW(pre_fp);
    NETW_BIND_JOURNAL_ROW(topo_fp);
    NETW_BIND_JOURNAL_ROW(raw_fp);
    NETW_BIND_JOURNAL_ROW(witness_fp);
    NETW_BIND_JOURNAL_ROW(witness_class_bits);
    NETW_BIND_JOURNAL_ROW(aligned_error);
    NETW_BIND_JOURNAL_ROW(evidence_mask);
    NETW_BIND_JOURNAL_ROW(pre_families);
    NETW_BIND_JOURNAL_ROW(post_fp);
    NETW_BIND_JOURNAL_ROW(post_families);
    NETW_BIND_JOURNAL_ROW(episode_id);
    NETW_BIND_JOURNAL_ROW(write_id);
    NETW_BIND_JOURNAL_ROW(op);
    NETW_BIND_JOURNAL_ROW(basis);
    NETW_BIND_JOURNAL_ROW(differing_family);
    NETW_BIND_JOURNAL_ROW(domain);
    NETW_BIND_JOURNAL_ROW(attribution);
    NETW_BIND_JOURNAL_ROW(flags);

#undef NETW_BIND_JOURNAL_ROW
}

int64_t NetwPredictVerdict::transition() const {
    return ack_verdict ? ack.transition : state.transition;
}

int64_t NetwPredictVerdict::recv_tick() const {
    return ack_verdict ? -1 : state.recv_tick;
}

int NetwPredictVerdict::exact() const {
    return int(ack_verdict ? ack.exact : state.exact);
}

int NetwPredictVerdict::domain() const {
    return ack_verdict ? int(predict::Domain::OUT_OF_DOMAIN)
                       : int(state.domain);
}

int NetwPredictVerdict::attribution() const {
    return int(ack_verdict ? ack.attribution : state.attribution);
}

int NetwPredictVerdict::differing_family() const {
    return ack_verdict ? int(ack.differing_family)
                       : int(predict::DifferingFamily::NONE);
}

bool NetwPredictVerdict::compared() const {
    return ack_verdict ? ack.compared : state.compared;
}

bool NetwPredictVerdict::evidence_complete() const {
    return ack_verdict && ack.evidence_complete;
}

bool NetwPredictVerdict::corrected() const {
    return !ack_verdict && state.corrected;
}

bool NetwPredictVerdict::settled() const {
    return !ack_verdict && state.settled;
}

double NetwPredictVerdict::divergence() const {
    return ack_verdict ? 0.0 : state.divergence;
}

int NetwPredictVerdict::meter() const {
    return ack_verdict ? 0 : state.meter;
}

PackedFloat64Array NetwPredictVerdict::field_errors() const {
    PackedFloat64Array out;
    if (ack_verdict) {
        return out;
    }
    out.resize(int(state.field_errors.size()));
    double *values = out.ptrw();
    for (uint32_t at = 0; at < state.field_errors.size(); ++at) {
        values[at] = state.field_errors[at];
    }
    return out;
}

void NetwPredictVerdict::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("transition"),
        &NetwPredictVerdict::transition
    );
    ClassDB::bind_method(D_METHOD("recv_tick"), &NetwPredictVerdict::recv_tick);
    ClassDB::bind_method(D_METHOD("exact"), &NetwPredictVerdict::exact);
    ClassDB::bind_method(D_METHOD("domain"), &NetwPredictVerdict::domain);
    ClassDB::bind_method(
        D_METHOD("attribution"),
        &NetwPredictVerdict::attribution
    );
    ClassDB::bind_method(
        D_METHOD("differing_family"),
        &NetwPredictVerdict::differing_family
    );
    ClassDB::bind_method(D_METHOD("compared"), &NetwPredictVerdict::compared);
    ClassDB::bind_method(
        D_METHOD("evidence_complete"),
        &NetwPredictVerdict::evidence_complete
    );
    ClassDB::bind_method(D_METHOD("corrected"), &NetwPredictVerdict::corrected);
    ClassDB::bind_method(D_METHOD("settled"), &NetwPredictVerdict::settled);
    ClassDB::bind_method(
        D_METHOD("divergence"),
        &NetwPredictVerdict::divergence
    );
    ClassDB::bind_method(D_METHOD("meter"), &NetwPredictVerdict::meter);
    ClassDB::bind_method(
        D_METHOD("field_errors"),
        &NetwPredictVerdict::field_errors
    );
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

Ref<NetwPredictDrive> NetwPredictionEngine::open_drive(
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
    Ref<NetwPredictDrive> out;
    out.instantiate();
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

    out->record = row->open_drive(timing, p_topology, pre);
    return out;
}

Ref<NetwPredictDrive> NetwPredictionEngine::replay_drive(
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
    Ref<NetwPredictDrive> out;
    out.instantiate();
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

    out->record = row->replay_drive(
        timing,
        p_topology,
        p_transition,
        p_label,
        DriveKind(p_kind),
        pre,
        p_authoring
    );
    return out;
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
    episode.open(
        row->journal,
        p_transition,
        predict::Attribution(p_attribution)
    );
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
    return int(
        predict::trigger_shape(row->wiring, errors, p_fallback_epsilon)
    );
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

void NetwPredictionEngine::record_breach(
    int64_t p_slot,
    int64_t p_transition
) {
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
        !row->config.carry,
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
    if (row == nullptr || !row->config.carry
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

Ref<NetwPredictCarryAttempt> NetwPredictionEngine::attempt_carry(
    int64_t p_slot,
    const StringName &p_field,
    const Variant &p_acknowledged,
    int64_t p_basis,
    double p_teleport_default,
    double p_divergence_epsilon
) {
    NETW_ZONE_NC("predict attempt carry", colors::PREDICTION);
    Ref<NetwPredictCarryAttempt> out;
    out.instantiate();
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
    out->record.evidence = true;
    const bool angle = row->wiring.angle[uint32_t(field)] != 0;
    const Callable step = rule->value;

    replay_carry(
        out->record,
        p_slot,
        step,
        p_field,
        field,
        entries,
        angle,
        p_divergence_epsilon
    );
    if (!out->record.evidence || !out->record.probe.faithful) {
        return out;
    }

    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        out->record.evidence = false;
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
        out->record,
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
        out->record.evidence = false;
        return out;
    }
    const int32_t after = guarded
        ? compared_fingerprint(
              *row,
              capture_through(*row, row->wiring.codec, row->owner_has_state)
          )
        : 0;
    out->record.probe.pure = !guarded || after == before;
    return out;
}

TypedArray<NetwPredictReplayEntry> NetwPredictionEngine::replay_entries(
    int64_t p_slot,
    int64_t p_basis
) const {
    TypedArray<NetwPredictReplayEntry> out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const LocalVector<predict::ReplayEntry> entries
        = row->replay_entries(p_basis);
    out.resize(int(entries.size()));
    for (uint32_t at = 0; at < entries.size(); ++at) {
        Ref<NetwPredictReplayEntry> entry;
        entry.instantiate();
        entry->record = entries[at];
        out[int(at)] = entry;
    }
    return out;
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
        "predict",
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

Ref<NetwPredictVerdict> NetwPredictionEngine::admit_ack(
    int64_t p_slot,
    int64_t p_transition,
    const Ref<NetwPredictEvidence> &p_evidence,
    bool p_substituted
) {
    Ref<NetwPredictVerdict> out;
    out.instantiate();
    out->ack_verdict = true;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        const predict::EvidenceRow evidence
            = p_evidence.is_valid() ? p_evidence->row : predict::EvidenceRow();
        out->ack = row->admit_ack(p_transition, evidence, p_substituted);
    }
    return out;
}

Ref<NetwPredictVerdict> NetwPredictionEngine::compare_state(
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
    Ref<NetwPredictVerdict> out;
    out.instantiate();
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::EpisodeState previous_state = row->episode.state;
    const int previous_episode_id = row->episode.id;
    const int width = row->wiring.count();
    out->state = row->compare(
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
    const predict::Episode &episode = row->episode;
    const Ref<NetwPredictEpisodeReport> report = report_of(episode);
    if (episode.id != previous_episode_id && episode.active) {
        emit_signal(SIG_EPISODE_OPENED, p_slot, report);
        if (episode.state == predict::EpisodeState::FALLBACK) {
            emit_signal(SIG_EPISODE_FALLBACK, p_slot, report);
        }
    } else if (
        previous_state == predict::EpisodeState::OPEN
        && episode.state == predict::EpisodeState::CLOSED
    ) {
        emit_signal(SIG_EPISODE_CLOSED, p_slot, report);
    } else if (
        previous_state == predict::EpisodeState::OPEN
        && episode.state == predict::EpisodeState::FALLBACK
    ) {
        emit_signal(SIG_EPISODE_FALLBACK, p_slot, report);
    }
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

Ref<NetwPredictWritePlan> NetwPredictionEngine::recover(
    int64_t p_slot,
    const Ref<NetwPredictRecoveryRequest> &p_request
) {
    Ref<NetwPredictWritePlan> out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    NETW_ERR_COND_V(
        p_request.is_null(),
        out,
        "predict",
        "Recovery request is null."
    );
    out.instantiate();
    out->plan = row->recover(p_request->build(row->wiring.count()));
    return out;
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

Ref<NetwPredictEpisodeReport> NetwPredictionEngine::episode(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? report_of(row->episode)
                          : Ref<NetwPredictEpisodeReport>();
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

Ref<NetwPredictWritePlan> NetwPredictionEngine::quarantine_state(
    int64_t p_slot,
    int64_t p_tick,
    int64_t p_basis,
    const Array &p_payload,
    bool p_whole
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? plan_of(row->quarantine_state(
                                p_tick,
                                p_basis,
                                state_row(p_payload, row->wiring.count()),
                                p_whole
                            ))
                          : Ref<NetwPredictWritePlan>();
}

Ref<NetwPredictWritePlan> NetwPredictionEngine::quarantine_witness(
    int64_t p_slot,
    int64_t p_basis,
    int p_bits
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? plan_of(row->quarantine_witness(p_basis, p_bits))
                          : Ref<NetwPredictWritePlan>();
}

void NetwPredictionEngine::confirm_reseed_epoch(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->confirm_reseed_epoch();
    }
}

Ref<NetwPredictWritePlan> NetwPredictionEngine::align_reseed(
    int64_t p_slot,
    int64_t p_transition,
    const Array &p_payload,
    int64_t p_ignore_through
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr ? plan_of(row->align_reseed(
                                p_transition,
                                state_row(p_payload, row->wiring.count()),
                                p_ignore_through
                            ))
                          : Ref<NetwPredictWritePlan>();
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
        "predict",
        "Island commit requires an open slot with a declared island."
    );
    const int count = p_members.size();
    NETW_ERR_COND_V(
        p_order_keys.size() != count || p_distance_squared.size() != count
            || p_fidelities.size() != count || p_eligible.size() != count
            || p_contact.size() != count,
        out,
        "predict",
        "Island columns must have the same length."
    );
    NETW_ERR_COND_V(
        p_promotion < int(predict::Promotion::NONE)
            || p_promotion > int(predict::Promotion::ALL),
        out,
        "predict",
        "Island promotion %d is outside the declared range.",
        p_promotion
    );
    NETW_ERR_COND_V(
        p_promotion_count < 0 || p_promotion_meters < 0.0
            || !std::isfinite(p_promotion_meters),
        out,
        "predict",
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
            "predict",
            "Island member slot %d is invalid.",
            member_slot
        );
        NETW_ERR_COND_V(
            owner->config.island == ISLAND_JOINT
                && !rerunnable(member->config.schedule),
            out,
            "predict",
            "Island member slot %d is not a re-runnable participant.",
            member_slot
        );
        NETW_ERR_COND_V(
            p_fidelities[at] < int(predict::Fidelity::UNDECLARED)
                || p_fidelities[at] > int(predict::Fidelity::SIMULATED),
            out,
            "predict",
            "Island fidelity %d is outside the declared range.",
            p_fidelities[at]
        );
        NETW_ERR_COND_V(
            p_distance_squared[at] < 0.0
                || !std::isfinite(p_distance_squared[at]),
            out,
            "predict",
            "Island distance squared must be non-negative."
        );
        NETW_ERR_COND_V(
            order_key == p_owner_order_key,
            out,
            "predict",
            "Island order keys must be unique."
        );
        for (int prior = 0; prior < at; ++prior) {
            NETW_ERR_COND_V(
                p_members[prior] == member_slot
                    || p_order_keys[prior] == order_key,
                out,
                "predict",
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
        "predict",
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
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        "predict",
        "Joint record names a closed slot."
    );
    NETW_ERR_COND(
        p_transition < 0,
        "predict",
        "Joint record transition must be non-negative."
    );
    row->joint.record(
        p_transition,
        state_row(p_state, row->wiring.count()),
        p_command,
        predict::joint_cell(p_authored, p_relayed, p_predictor_valid)
    );
}

void NetwPredictionEngine::joint_note_basis(
    int64_t p_slot,
    int64_t p_basis,
    int p_source
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr || row->config.island == ISLAND_NONE,
        "predict",
        "Joint floor move requires an open slot with a declared island."
    );
    NETW_ERR_COND(
        p_basis < 0 || p_source < JOINT_FLOOR_ACK
            || p_source > JOINT_FLOOR_EPOCH,
        "predict",
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

Ref<NetwPredictJointPlan> NetwPredictionEngine::joint_pass(
    int64_t p_slot,
    int64_t p_present
) {
    NETW_ZONE_NC("NetwPredictionEngine joint pass", colors::PREDICTION);
    predict::JointPassPlan plan;
    predict::Slot *owner = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        owner == nullptr || owner->config.island != ISLAND_JOINT
            || !rerunnable(owner->config.schedule),
        joint_plan_of(plan),
        "predict",
        "Joint pass requires an open JOINT owner on a re-runnable schedule."
    );
    NETW_ERR_COND_V(
        p_present < 0,
        joint_plan_of(plan),
        "predict",
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
            "predict",
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
        return joint_plan_of(plan);
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
                "predict",
                "Joint pass slot=%d has no retained state for member=%d.",
                p_slot,
                members[at].slot
            );
            for (uint32_t clear_at = 0; clear_at < members.size(); ++clear_at) {
                members[clear_at].row->joint.clear_floors();
            }
            return joint_plan_of(plan);
        }
        predict::JointRestore restore;
        restore.slot = members[at].slot;
        restore.state = *state;
        plan.restores.push_back(restore);
        members[at].row->set_state(*state);
    }
    NETW_ASSERT(
        plan.restores.size() == members.size(),
        "predict",
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
        "predict",
        "Joint pass slot=%d floor=%d present=%d members=%d heal=%d.",
        p_slot,
        plan.floor,
        plan.present,
        stats.members,
        plan.heal ? 1 : 0
    );
    return joint_plan_of(plan);
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
    const Ref<NetwPredictVerdict> &p_verdict
) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_verdict.is_null()) {
        return out;
    }
    const predict::StateVerdict &verdict = p_verdict->state;
    for (uint32_t at = 0; at < verdict.all_field_errors.size(); ++at) {
        if (verdict.all_field_errors[at] < 0.0) {
            continue;
        }
        if (int(at) >= row->wiring.codec.count()) {
            break;
        }
        out[row->wiring.codec.keys[at]] = verdict.all_field_errors[at];
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
    int64_t p_slot,
    const Dictionary &p_witness_details
) const {
    Ref<NetwPredictJournal> out;
    out.instantiate();
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    out->adopt(row->journal, p_witness_details);
    return out;
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

Ref<NetwPredictJournalRow> NetwPredictionEngine::journal_row(
    int64_t p_slot,
    int64_t p_transition
) const {
    Ref<NetwPredictJournalRow> out;
    const predict::Slot *slot = row_of(p_slot);
    if (slot == nullptr) {
        return out;
    }
    const int at = slot->journal.index_of(p_transition);
    if (at < 0) {
        return out;
    }
    out.instantiate();
    out->transition_value = slot->journal.transition_at(at);
    out->label_value = slot->journal.label_at(at);
    out->kind_value = slot->journal.kind_at(at);
    out->c_hash_value = slot->journal.c_hash_at(at);
    out->e_digest_value = slot->journal.e_digest_at(at);
    out->pre_fp_value = slot->journal.pre_fp_at(at);
    out->topo_fp_value = slot->journal.topo_fp_at(at);
    out->raw_fp_value = slot->journal.raw_fp_at(at);
    out->witness_fp_value = slot->journal.witness_fp_at(at);
    out->witness_class_bits_value = slot->journal.witness_class_bits_at(at);
    out->aligned_error_value = slot->journal.aligned_error_at(at);
    out->evidence_mask_value = slot->journal.evidence_mask_at(at);
    const predict::FamilyFingerprints pre = slot->journal.pre_families_at(at);
    out->pre_families_value.push_back(pre.pose);
    out->pre_families_value.push_back(pre.momentum);
    out->pre_families_value.push_back(pre.controller);
    out->post_fp_value = slot->journal.post_fp_at(at);
    const predict::FamilyFingerprints post = slot->journal.post_families_at(at);
    out->post_families_value.push_back(post.pose);
    out->post_families_value.push_back(post.momentum);
    out->post_families_value.push_back(post.controller);
    out->episode_id_value = slot->journal.episode_id_at(at);
    out->write_id_value = slot->journal.write_id_at(at);
    out->operator_value = slot->journal.operator_at(at);
    out->basis_value = slot->journal.basis_at(at);
    out->differing_family_value = slot->journal.differing_family_at(at);
    out->domain_value = slot->journal.domain_at(at);
    out->attribution_value = slot->journal.attribution_at(at);
    out->flags_value = slot->journal.flags_at(at);
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

Ref<NetwTimeline> NetwPredictionEngine::entry_history(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->entry_history : Ref<NetwTimeline>();
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

bool NetwPredictionEngine::journal_has(int64_t p_slot, int64_t p_transition)
    const {
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

void NetwPredictionEngine::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("open", "declaration"),
        &NetwPredictionEngine::open
    );
    ClassDB::bind_method(
        D_METHOD("rewire", "slot", "declaration", "input"),
        &NetwPredictionEngine::rewire,
        DEFVAL(Ref<NetwPredictDeclaration>())
    );
    ClassDB::bind_method(
        D_METHOD("set_simulate", "slot", "callable"),
        &NetwPredictionEngine::set_simulate
    );
    ClassDB::bind_method(
        D_METHOD("set_witness", "slot", "callable"),
        &NetwPredictionEngine::set_witness
    );
    ClassDB::bind_method(
        D_METHOD("set_corridor", "slot", "callable"),
        &NetwPredictionEngine::set_corridor
    );
    ClassDB::bind_method(
        D_METHOD("set_sensor", "slot", "name", "callable"),
        &NetwPredictionEngine::set_sensor
    );
    ClassDB::bind_method(
        D_METHOD("set_carry", "slot", "field", "callable"),
        &NetwPredictionEngine::set_carry
    );
    ClassDB::bind_method(
        D_METHOD("set_order_key", "slot", "order_key"),
        &NetwPredictionEngine::set_order_key
    );
    ClassDB::bind_method(
        D_METHOD("order_key_of", "slot"),
        &NetwPredictionEngine::order_key_of
    );
    ClassDB::bind_method(
        D_METHOD("ordered_slots"),
        &NetwPredictionEngine::ordered_slots
    );
    ClassDB::bind_method(
        D_METHOD("pass_slots", "phase"),
        &NetwPredictionEngine::pass_slots
    );
    ClassDB::bind_method(
        D_METHOD("has_simulate", "slot"),
        &NetwPredictionEngine::has_simulate
    );
    ClassDB::bind_method(
        D_METHOD("run_step", "slot", "input", "delta", "tick", "fresh"),
        &NetwPredictionEngine::run_step
    );
    ClassDB::bind_method(
        D_METHOD("pass_depth"),
        &NetwPredictionEngine::pass_depth
    );
    ClassDB::bind_method(
        D_METHOD("mutations_refused_count"),
        &NetwPredictionEngine::mutations_refused_count
    );
    ClassDB::bind_method(
        D_METHOD("bind_owner", "slot", "owner"),
        &NetwPredictionEngine::bind_owner
    );
    ClassDB::bind_method(
        D_METHOD("unbind_owner", "slot"),
        &NetwPredictionEngine::unbind_owner
    );
    ClassDB::bind_method(
        D_METHOD("owner_bound", "slot"),
        &NetwPredictionEngine::owner_bound
    );
    ClassDB::bind_method(
        D_METHOD("owner_solves", "slot"),
        &NetwPredictionEngine::owner_solves
    );
    ClassDB::bind_method(
        D_METHOD("capture_state", "slot"),
        &NetwPredictionEngine::capture_state
    );
    ClassDB::bind_method(
        D_METHOD("capture_input", "slot"),
        &NetwPredictionEngine::capture_input
    );
    ClassDB::bind_method(
        D_METHOD("apply_state", "slot", "payload"),
        &NetwPredictionEngine::apply_state
    );
    ClassDB::bind_method(
        D_METHOD("apply_input", "slot", "payload"),
        &NetwPredictionEngine::apply_input
    );
    ClassDB::bind_method(
        D_METHOD("canonicalize_state", "slot", "payload"),
        &NetwPredictionEngine::canonicalize_state
    );
    ClassDB::bind_method(
        D_METHOD("canonicalize_input", "slot", "payload"),
        &NetwPredictionEngine::canonicalize_input
    );
    ClassDB::bind_method(
        D_METHOD("coast_command", "slot"),
        &NetwPredictionEngine::coast_command
    );
    ClassDB::bind_method(
        D_METHOD("sample_environment", "slot", "epoch"),
        &NetwPredictionEngine::sample_environment
    );
    ClassDB::bind_method(
        D_METHOD("sensor_samples", "slot"),
        &NetwPredictionEngine::sensor_samples
    );
    ClassDB::bind_method(
        D_METHOD("canonical_state_bytes", "slot", "payload"),
        &NetwPredictionEngine::canonical_state_bytes
    );
    ClassDB::bind_method(
        D_METHOD("canonical_input_bytes", "slot", "payload"),
        &NetwPredictionEngine::canonical_input_bytes
    );
    ClassDB::bind_method(
        D_METHOD("close", "slot"),
        &NetwPredictionEngine::close
    );

    ClassDB::bind_method(
        D_METHOD("slot_register", "entity"),
        &NetwPredictionEngine::slot_register
    );
    ClassDB::bind_method(
        D_METHOD("slot_of", "entity"),
        &NetwPredictionEngine::slot_of
    );
    ClassDB::bind_method(
        D_METHOD("slot_unregister", "entity"),
        &NetwPredictionEngine::slot_unregister
    );
    ClassDB::bind_method(
        D_METHOD("slot_registered"),
        &NetwPredictionEngine::slot_registered
    );
    ClassDB::bind_method(
        D_METHOD("slot_bind_owner", "entity", "owner"),
        &NetwPredictionEngine::slot_bind_owner
    );
    ClassDB::bind_method(
        D_METHOD("slot_unbind_owner", "entity"),
        &NetwPredictionEngine::slot_unbind_owner
    );
    ClassDB::bind_method(
        D_METHOD("is_open", "slot"),
        &NetwPredictionEngine::is_open
    );
    ClassDB::bind_method(
        D_METHOD("open_count"),
        &NetwPredictionEngine::open_count
    );
    ClassDB::bind_method(
        D_METHOD(
            "supports",
            "schedule",
            "role",
            "correction",
            "restore",
            "island",
            "carry",
            "witness"
        ),
        &NetwPredictionEngine::supports,
        DEFVAL(ISLAND_NONE),
        DEFVAL(false),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("field_count", "slot"),
        &NetwPredictionEngine::field_count
    );
    ClassDB::bind_method(
        D_METHOD("field_slot", "slot", "key"),
        &NetwPredictionEngine::field_slot
    );
    ClassDB::bind_method(
        D_METHOD("field_name", "slot", "field"),
        &NetwPredictionEngine::field_name
    );
    ClassDB::bind_method(
        D_METHOD("projection_of", "slot", "field"),
        &NetwPredictionEngine::projection_of
    );
    ClassDB::bind_method(
        D_METHOD("state_family_of", "slot", "field"),
        &NetwPredictionEngine::state_family_of
    );
    ClassDB::bind_method(
        D_METHOD("state_fingerprint", "slot", "payload"),
        &NetwPredictionEngine::state_fingerprint
    );
    ClassDB::bind_method(
        D_METHOD("state_family_fingerprints", "slot", "payload"),
        &NetwPredictionEngine::state_family_fingerprints
    );
    ClassDB::bind_method(
        D_METHOD("converge_rate_of", "slot", "field"),
        &NetwPredictionEngine::converge_rate_of
    );
    ClassDB::bind_method(
        D_METHOD("epsilon_of", "slot", "field"),
        &NetwPredictionEngine::epsilon_of
    );
    ClassDB::bind_method(
        D_METHOD("teleport_of", "slot", "field"),
        &NetwPredictionEngine::teleport_of
    );
    ClassDB::bind_method(
        D_METHOD("is_pose", "slot", "field"),
        &NetwPredictionEngine::is_pose
    );
    ClassDB::bind_method(
        D_METHOD("is_withheld", "slot", "field"),
        &NetwPredictionEngine::is_withheld
    );
    ClassDB::bind_method(
        D_METHOD("is_trigger_excluded", "slot", "field"),
        &NetwPredictionEngine::is_trigger_excluded
    );
    ClassDB::bind_method(
        D_METHOD("is_vote_excluded", "slot", "field"),
        &NetwPredictionEngine::is_vote_excluded
    );
    ClassDB::bind_method(
        D_METHOD("is_angle", "slot", "field"),
        &NetwPredictionEngine::is_angle
    );
    ClassDB::bind_method(
        D_METHOD("is_causal", "slot", "field"),
        &NetwPredictionEngine::is_causal
    );

    ClassDB::bind_static_method(
        "NetwPredictionEngine",
        D_METHOD("archetype_axes", "archetype"),
        &NetwPredictionEngine::archetype_axes
    );
    ClassDB::bind_static_method(
        "NetwPredictionEngine",
        D_METHOD("role_for_axes", "input_source", "sim_mode"),
        &NetwPredictionEngine::role_for_axes
    );
    ClassDB::bind_static_method(
        "NetwPredictionEngine",
        D_METHOD("correction_for_recovery_policy", "policy"),
        &NetwPredictionEngine::correction_for_recovery_policy
    );

    ClassDB::bind_method(
        D_METHOD(
            "configure",
            "slot",
            "schedule",
            "role",
            "correction",
            "restore",
            "max_restore_ticks",
            "island",
            "carry",
            "witness",
            "island_declared",
            "island_approximate"
        ),
        &NetwPredictionEngine::configure,
        DEFVAL(6),
        DEFVAL(ISLAND_NONE),
        DEFVAL(false),
        DEFVAL(false),
        DEFVAL(false),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("island_declared", "slot"),
        &NetwPredictionEngine::island_declared
    );
    ClassDB::bind_method(
        D_METHOD("island_approximate", "slot"),
        &NetwPredictionEngine::island_approximate
    );
    ClassDB::bind_method(
        D_METHOD("out_of_domain_until", "slot"),
        &NetwPredictionEngine::out_of_domain_until
    );
    ClassDB::bind_method(
        D_METHOD("out_of_domain_at", "slot", "label"),
        &NetwPredictionEngine::out_of_domain_at
    );
    ClassDB::bind_method(
        D_METHOD("open_out_of_domain_window", "slot", "label", "cooldown"),
        &NetwPredictionEngine::open_out_of_domain_window
    );
    ClassDB::bind_method(
        D_METHOD("clear_out_of_domain_window", "slot"),
        &NetwPredictionEngine::clear_out_of_domain_window
    );
    ClassDB::bind_method(
        D_METHOD("pending_provenance", "slot"),
        &NetwPredictionEngine::pending_provenance
    );
    ClassDB::bind_method(
        D_METHOD("set_pending_provenance", "slot", "provenance"),
        &NetwPredictionEngine::set_pending_provenance
    );
    ClassDB::bind_method(
        D_METHOD("stamp_pending_provenance", "slot", "transition"),
        &NetwPredictionEngine::stamp_pending_provenance
    );
    ClassDB::bind_method(
        D_METHOD("schedule_of", "slot"),
        &NetwPredictionEngine::schedule_of
    );
    ClassDB::bind_method(
        D_METHOD("role_of", "slot"),
        &NetwPredictionEngine::role_of
    );
    ClassDB::bind_method(
        D_METHOD("island_of", "slot"),
        &NetwPredictionEngine::island_of
    );
    ClassDB::bind_method(
        D_METHOD(
            "plan_consume_input",
            "schedule",
            "has_input",
            "has_later_input",
            "has_last_input",
            "missing_policy"
        ),
        &NetwPredictionEngine::plan_consume_input
    );
    ClassDB::bind_method(
        D_METHOD("record_input", "slot", "tick", "c_hash"),
        &NetwPredictionEngine::record_input
    );
    ClassDB::bind_method(
        D_METHOD(
            "open_drive",
            "slot",
            "topology",
            "tick",
            "frame",
            "ticktime",
            "quantum",
            "simulating",
            "pre_fp",
            "pre_pose_fp",
            "pre_momentum_fp",
            "pre_controller_fp",
            "raw_fp",
            "evidence_mask"
        ),
        &NetwPredictionEngine::open_drive,
        DEFVAL(1),
        DEFVAL(true),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD(
            "replay_drive",
            "slot",
            "topology",
            "transition",
            "label",
            "kind",
            "tick",
            "frame",
            "ticktime",
            "quantum",
            "pre_fp",
            "pre_pose_fp",
            "pre_momentum_fp",
            "pre_controller_fp",
            "raw_fp",
            "evidence_mask",
            "authoring"
        ),
        &NetwPredictionEngine::replay_drive,
        DEFVAL(1),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD(
            "close_drive",
            "slot",
            "transition",
            "post_fp",
            "post_pose_fp",
            "post_momentum_fp",
            "post_controller_fp"
        ),
        &NetwPredictionEngine::close_drive,
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD(
            "record_evidence",
            "slot",
            "transition",
            "environment_epoch",
            "environment",
            "topology",
            "contact_ids",
            "witness_classes",
            "realizations",
            "outside_boundary",
            "sleeping"
        ),
        &NetwPredictionEngine::record_evidence,
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD(
            "record_episode_decision",
            "slot",
            "operator",
            "basis",
            "eligible",
            "applied",
            "eligibility"
        ),
        &NetwPredictionEngine::record_episode_decision
    );
    ClassDB::bind_method(
        D_METHOD("open_episode", "slot", "transition", "attribution"),
        &NetwPredictionEngine::open_episode
    );
    ClassDB::bind_method(
        D_METHOD("stamp_episode_write_delta", "slot", "delta_fp"),
        &NetwPredictionEngine::stamp_episode_write_delta
    );
    ClassDB::bind_method(
        D_METHOD("record_episode_divergence", "slot", "transition"),
        &NetwPredictionEngine::record_episode_divergence
    );
    ClassDB::bind_method(
        D_METHOD(
            "escalation_field_of",
            "slot",
            "predicted",
            "authority",
            "field_errors",
            "fallback_epsilon"
        ),
        &NetwPredictionEngine::escalation_field_of
    );
    ClassDB::bind_method(
        D_METHOD(
            "trigger_shape_of", "slot", "field_errors", "fallback_epsilon"
        ),
        &NetwPredictionEngine::trigger_shape_of
    );
    ClassDB::bind_method(
        D_METHOD("record_episode_escalation", "slot", "trigger_shape"),
        &NetwPredictionEngine::record_episode_escalation
    );
    ClassDB::bind_method(
        D_METHOD(
            "record_episode_write",
            "slot",
            "operator",
            "basis",
            "delta_fp",
            "target",
            "ack_age",
            "trigger_shape",
            "evidence_free",
            "null_operator"
        ),
        &NetwPredictionEngine::record_episode_write,
        DEFVAL(false),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("episode_stats", "slot"),
        &NetwPredictionEngine::episode_stats
    );
    ClassDB::bind_method(
        D_METHOD("episode_active", "slot"),
        &NetwPredictionEngine::episode_active
    );
    ClassDB::bind_method(
        D_METHOD("episode_state", "slot"),
        &NetwPredictionEngine::episode_state
    );
    ClassDB::bind_method(
        D_METHOD("episode_budget_exhausted", "slot"),
        &NetwPredictionEngine::episode_budget_exhausted
    );
    ClassDB::bind_method(
        D_METHOD("episode_operator_pending", "slot", "operator"),
        &NetwPredictionEngine::episode_operator_pending
    );
    ClassDB::bind_method(
        D_METHOD("record_breach", "slot", "transition"),
        &NetwPredictionEngine::record_breach
    );
    ClassDB::bind_method(
        D_METHOD(
            "witness_row_clean",
            "slot",
            "transition",
            "require_peer"
        ),
        &NetwPredictionEngine::witness_row_clean
    );
    ClassDB::bind_method(
        D_METHOD("resolve_correction", "slot", "declared"),
        &NetwPredictionEngine::resolve_correction
    );
    ClassDB::bind_method(
        D_METHOD(
            "transport_admissible",
            "slot",
            "candidate",
            "basis_witness_clean",
            "recent_witness_clean",
            "non_pose_agrees",
            "below_teleport",
            "escalated",
            "observing"
        ),
        &NetwPredictionEngine::transport_admissible
    );
    ClassDB::bind_method(
        D_METHOD(
            "dissipate_admissible",
            "slot",
            "momentum_active",
            "other_active",
            "basis_witness_clean",
            "recent_witness_clean",
            "escalated",
            "observing",
            "meter"
        ),
        &NetwPredictionEngine::dissipate_admissible
    );
    ClassDB::bind_method(
        D_METHOD("dissipate_declared", "slot"),
        &NetwPredictionEngine::dissipate_declared
    );
    ClassDB::bind_method(
        D_METHOD("static_geometry", "collider"),
        &NetwPredictionEngine::static_geometry
    );
    ClassDB::bind_method(
        D_METHOD("witness_class", "collider", "declared_support"),
        &NetwPredictionEngine::witness_class
    );
    ClassDB::bind_method(
        D_METHOD(
            "judge_carry",
            "slot",
            "field",
            "same_type",
            "finite",
            "within_envelope",
            "pure",
            "faithful"
        ),
        &NetwPredictionEngine::judge_carry
    );
    ClassDB::bind_method(
        D_METHOD("decline_carry", "slot", "field"),
        &NetwPredictionEngine::decline_carry
    );
    ClassDB::bind_method(
        D_METHOD("carry_eligible", "slot", "field"),
        &NetwPredictionEngine::carry_eligible
    );
    ClassDB::bind_method(
        D_METHOD("carry_retired", "slot", "field"),
        &NetwPredictionEngine::carry_retired
    );
    ClassDB::bind_method(
        D_METHOD("carry_stats", "slot", "field"),
        &NetwPredictionEngine::carry_stats
    );
    ClassDB::bind_method(
        D_METHOD("mark_carry_dirty", "slot", "transition"),
        &NetwPredictionEngine::mark_carry_dirty
    );
    ClassDB::bind_method(
        D_METHOD("carry_judgeable", "slot", "transition"),
        &NetwPredictionEngine::carry_judgeable
    );
    ClassDB::bind_method(
        D_METHOD("replay_entries", "slot", "basis"),
        &NetwPredictionEngine::replay_entries
    );
    ClassDB::bind_method(
        D_METHOD("state_before", "slot", "transition"),
        &NetwPredictionEngine::state_before
    );
    ClassDB::bind_method(
        D_METHOD(
            "attempt_carry",
            "slot",
            "field",
            "acknowledged",
            "basis",
            "teleport_default",
            "divergence_epsilon"
        ),
        &NetwPredictionEngine::attempt_carry
    );
    ClassDB::bind_method(
        D_METHOD("mark_domain", "slot", "transition", "domain"),
        &NetwPredictionEngine::mark_domain
    );
    ClassDB::bind_method(
        D_METHOD("mark_chain_broken", "slot", "transition"),
        &NetwPredictionEngine::mark_chain_broken
    );
    ClassDB::bind_method(
        D_METHOD(
            "mark_provenance",
            "slot",
            "transition",
            "episode_id",
            "write_id",
            "operator",
            "basis"
        ),
        &NetwPredictionEngine::mark_provenance
    );
    ClassDB::bind_method(
        D_METHOD("mark_attribution", "slot", "transition", "attribution"),
        &NetwPredictionEngine::mark_attribution
    );
    ClassDB::bind_method(
        D_METHOD("mark_differing_family", "slot", "transition", "family"),
        &NetwPredictionEngine::mark_differing_family
    );
    ClassDB::bind_method(
        D_METHOD(
            "mark_solve",
            "slot",
            "transition",
            "topo_fp",
            "witness_fp",
            "evidence_mask",
            "witness_class_bits"
        ),
        &NetwPredictionEngine::mark_solve
    );
    ClassDB::bind_method(
        D_METHOD("witness_judged", "slot", "transition"),
        &NetwPredictionEngine::witness_judged
    );
    ClassDB::bind_method(
        D_METHOD("mark_witness_match", "slot", "transition", "matched"),
        &NetwPredictionEngine::mark_witness_match
    );
    ClassDB::bind_method(
        D_METHOD("declare_skipped", "slot", "transition", "label"),
        &NetwPredictionEngine::declare_skipped
    );
    ClassDB::bind_method(
        D_METHOD("mark_aligned_error", "slot", "transition", "error"),
        &NetwPredictionEngine::mark_aligned_error
    );
    ClassDB::bind_method(
        D_METHOD("acknowledge", "slot", "transition", "matched"),
        &NetwPredictionEngine::acknowledge
    );
    ClassDB::bind_method(
        D_METHOD("admit_ack", "slot", "transition", "evidence", "substituted"),
        &NetwPredictionEngine::admit_ack,
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD(
            "compare_state",
            "slot",
            "recv_tick",
            "transition",
            "predicted",
            "authority",
            "correction_tolerances",
            "meter_tolerances",
            "fallback_epsilon",
            "stream_reconstructed",
            "ack_domain_confirmed"
        ),
        &NetwPredictionEngine::compare_state,
        DEFVAL(true),
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD("set_state", "slot", "state"),
        &NetwPredictionEngine::set_state
    );
    ClassDB::bind_method(
        D_METHOD("state_has", "slot", "field"),
        &NetwPredictionEngine::state_has
    );
    ClassDB::bind_method(
        D_METHOD("state_at", "slot", "field"),
        &NetwPredictionEngine::state_at
    );
    ClassDB::bind_method(
        D_METHOD("recover", "slot", "request"),
        &NetwPredictionEngine::recover
    );
    ClassDB::bind_method(
        D_METHOD("open_recovery_window", "slot", "label", "cooldown"),
        &NetwPredictionEngine::open_recovery_window
    );
    ClassDB::bind_method(
        D_METHOD("suppress_recovery_until", "slot", "label", "cooldown"),
        &NetwPredictionEngine::suppress_recovery_until
    );
    ClassDB::bind_method(
        D_METHOD("escalation_pending", "slot"),
        &NetwPredictionEngine::escalation_pending
    );
    ClassDB::bind_method(
        D_METHOD("recovery_window_until", "slot"),
        &NetwPredictionEngine::recovery_window_until
    );
    ClassDB::bind_method(
        D_METHOD("recovery_cooldown_until", "slot"),
        &NetwPredictionEngine::recovery_cooldown_until
    );
    ClassDB::bind_method(
        D_METHOD("episode", "slot"),
        &NetwPredictionEngine::episode
    );
    ClassDB::bind_method(
        D_METHOD(
            "enter_quarantine",
            "slot",
            "transition",
            "stream_reconstructed",
            "attribution",
            "demoted"
        ),
        &NetwPredictionEngine::enter_quarantine,
        DEFVAL(false)
    );
    ClassDB::bind_method(
        D_METHOD(
            "quarantine_state",
            "slot",
            "tick",
            "basis",
            "payload",
            "whole"
        ),
        &NetwPredictionEngine::quarantine_state,
        DEFVAL(true)
    );
    ClassDB::bind_method(
        D_METHOD("quarantine_witness", "slot", "basis", "bits"),
        &NetwPredictionEngine::quarantine_witness
    );
    ClassDB::bind_method(
        D_METHOD("confirm_reseed_epoch", "slot"),
        &NetwPredictionEngine::confirm_reseed_epoch
    );
    ClassDB::bind_method(
        D_METHOD(
            "align_reseed",
            "slot",
            "transition",
            "payload",
            "ignore_through"
        ),
        &NetwPredictionEngine::align_reseed
    );
    ClassDB::bind_method(
        D_METHOD("admit_post_reseed", "slot", "basis"),
        &NetwPredictionEngine::admit_post_reseed
    );
    ClassDB::bind_method(
        D_METHOD("adopt_alignment", "slot", "transition", "ignore_through"),
        &NetwPredictionEngine::adopt_alignment
    );
    ClassDB::bind_method(
        D_METHOD("finish_probation", "slot", "corrected"),
        &NetwPredictionEngine::finish_probation
    );
    ClassDB::bind_method(
        D_METHOD("quarantine_latched", "slot"),
        &NetwPredictionEngine::quarantine_latched
    );
    ClassDB::bind_method(
        D_METHOD("quarantine_clean_run", "slot"),
        &NetwPredictionEngine::quarantine_clean_run
    );
    ClassDB::bind_method(
        D_METHOD("quarantine_target", "slot"),
        &NetwPredictionEngine::quarantine_target
    );
    ClassDB::bind_method(
        D_METHOD("reseed_align_pending", "slot"),
        &NetwPredictionEngine::reseed_align_pending
    );
    ClassDB::bind_method(
        D_METHOD("reseed_epoch_confirmed", "slot"),
        &NetwPredictionEngine::reseed_epoch_confirmed
    );
    ClassDB::bind_method(
        D_METHOD("probation_pending", "slot"),
        &NetwPredictionEngine::probation_pending
    );
    ClassDB::bind_method(
        D_METHOD("reseed_ignore_through", "slot"),
        &NetwPredictionEngine::reseed_ignore_through
    );
    ClassDB::bind_method(
        D_METHOD(
            "island_commit",
            "slot",
            "owner_order_key",
            "members",
            "order_keys",
            "distance_squared",
            "fidelities",
            "eligible",
            "contact",
            "promotion",
            "promotion_count",
            "promotion_meters",
            "frontier"
        ),
        &NetwPredictionEngine::island_commit
    );
    ClassDB::bind_method(
        D_METHOD("island_member_count", "slot"),
        &NetwPredictionEngine::island_member_count
    );
    ClassDB::bind_method(
        D_METHOD("island_promoted", "slot", "member"),
        &NetwPredictionEngine::island_promoted
    );
    ClassDB::bind_method(
        D_METHOD("tenure_begin", "slot"),
        &NetwPredictionEngine::tenure_begin
    );
    ClassDB::bind_method(
        D_METHOD("tenure_end", "slot"),
        &NetwPredictionEngine::tenure_end
    );
    ClassDB::bind_method(
        D_METHOD(
            "joint_record",
            "slot",
            "transition",
            "state",
            "command",
            "authored",
            "relayed",
            "predictor_valid"
        ),
        &NetwPredictionEngine::joint_record
    );
    ClassDB::bind_method(
        D_METHOD("joint_note_basis", "slot", "basis", "source"),
        &NetwPredictionEngine::joint_note_basis
    );
    ClassDB::bind_method(
        D_METHOD("joint_clear", "slot"),
        &NetwPredictionEngine::joint_clear
    );
    ClassDB::bind_method(
        D_METHOD("joint_command_at", "slot", "transition"),
        &NetwPredictionEngine::joint_command_at
    );
    ClassDB::bind_method(
        D_METHOD("joint_provenance_at", "slot", "transition"),
        &NetwPredictionEngine::joint_provenance_at
    );
    ClassDB::bind_method(
        D_METHOD("joint_pass", "slot", "present"),
        &NetwPredictionEngine::joint_pass
    );
    ClassDB::bind_method(
        D_METHOD("joint_stats", "slot"),
        &NetwPredictionEngine::joint_stats
    );
    ClassDB::bind_method(
        D_METHOD("journal_size", "slot"),
        &NetwPredictionEngine::journal_size
    );
    ClassDB::bind_method(
        D_METHOD("field_divergence", "slot", "verdict"),
        &NetwPredictionEngine::field_divergence
    );
    ClassDB::bind_method(
        D_METHOD("transition_span", "slot", "basis"),
        &NetwPredictionEngine::transition_span
    );
    ClassDB::bind_method(
        D_METHOD("journal_snapshot", "slot", "witness_details"),
        &NetwPredictionEngine::journal_snapshot
    );
    ClassDB::bind_method(
        D_METHOD("journal_epoch", "slot"),
        &NetwPredictionEngine::journal_epoch
    );
    ClassDB::bind_method(
        D_METHOD("journal_transitions", "slot"),
        &NetwPredictionEngine::journal_transitions
    );
    ClassDB::bind_method(
        D_METHOD("journal_row", "slot", "transition"),
        &NetwPredictionEngine::journal_row
    );
    ClassDB::bind_method(
        D_METHOD("journal_slot_of", "slot", "transition"),
        &NetwPredictionEngine::journal_slot_of
    );
    ClassDB::bind_method(
        D_METHOD("journal_transition_at", "slot", "index"),
        &NetwPredictionEngine::journal_transition_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_label_at", "slot", "index"),
        &NetwPredictionEngine::journal_label_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_kind_at", "slot", "index"),
        &NetwPredictionEngine::journal_kind_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_c_hash_at", "slot", "index"),
        &NetwPredictionEngine::journal_c_hash_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_pre_fp_at", "slot", "index"),
        &NetwPredictionEngine::journal_pre_fp_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_post_fp_at", "slot", "index"),
        &NetwPredictionEngine::journal_post_fp_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_environment_at", "slot", "index"),
        &NetwPredictionEngine::journal_environment_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_topology_at", "slot", "index"),
        &NetwPredictionEngine::journal_topology_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_raw_at", "slot", "index"),
        &NetwPredictionEngine::journal_raw_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_witness_at", "slot", "index"),
        &NetwPredictionEngine::journal_witness_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_witness_class_at", "slot", "index"),
        &NetwPredictionEngine::journal_witness_class_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_pre_families_at", "slot", "index"),
        &NetwPredictionEngine::journal_pre_families_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_post_families_at", "slot", "index"),
        &NetwPredictionEngine::journal_post_families_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_episode_id_at", "slot", "index"),
        &NetwPredictionEngine::journal_episode_id_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_write_id_at", "slot", "index"),
        &NetwPredictionEngine::journal_write_id_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_operator_at", "slot", "index"),
        &NetwPredictionEngine::journal_operator_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_basis_at", "slot", "index"),
        &NetwPredictionEngine::journal_basis_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_evidence_at", "slot", "index"),
        &NetwPredictionEngine::journal_evidence_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_flags_at", "slot", "index"),
        &NetwPredictionEngine::journal_flags_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_domain_at", "slot", "index"),
        &NetwPredictionEngine::journal_domain_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_attribution_at", "slot", "index"),
        &NetwPredictionEngine::journal_attribution_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_differing_family_at", "slot", "index"),
        &NetwPredictionEngine::journal_differing_family_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_aligned_error_at", "slot", "index"),
        &NetwPredictionEngine::journal_aligned_error_at
    );
    ClassDB::bind_method(
        D_METHOD("journal_last_closed", "slot"),
        &NetwPredictionEngine::journal_last_closed
    );
    ClassDB::bind_method(
        D_METHOD("journal_first_unmatched", "slot"),
        &NetwPredictionEngine::journal_first_unmatched
    );
    ClassDB::bind_method(
        D_METHOD("journal_first_chain_break", "slot"),
        &NetwPredictionEngine::journal_first_chain_break
    );
    ClassDB::bind_method(
        D_METHOD("journal_clear", "slot", "epoch"),
        &NetwPredictionEngine::journal_clear
    );
    ClassDB::bind_method(
        D_METHOD("tape_reset", "slot", "epoch"),
        &NetwPredictionEngine::tape_reset
    );
    ClassDB::bind_method(
        D_METHOD("build_ack_frame", "slot", "epoch", "ack", "owner_ack_floor"),
        &NetwPredictionEngine::build_ack_frame
    );
    ClassDB::bind_method(
        D_METHOD("tape_size", "slot"),
        &NetwPredictionEngine::tape_size
    );
    ClassDB::bind_method(
        D_METHOD("tape_span", "slot"),
        &NetwPredictionEngine::tape_span
    );
    ClassDB::bind_method(
        D_METHOD("tape_label_of", "slot", "index"),
        &NetwPredictionEngine::tape_label_of
    );
    ClassDB::bind_method(
        D_METHOD("tape_is_fresh", "slot", "index"),
        &NetwPredictionEngine::tape_is_fresh
    );
    ClassDB::bind_method(
        D_METHOD("tape_prepare_tick", "slot", "tick"),
        &NetwPredictionEngine::tape_prepare_tick
    );
    ClassDB::bind_method(
        D_METHOD("tape_author", "slot", "label", "fresh"),
        &NetwPredictionEngine::tape_author
    );
    ClassDB::bind_method(
        D_METHOD("bind_timeline", "slot", "timeline"),
        &NetwPredictionEngine::bind_timeline
    );
    ClassDB::bind_method(
        D_METHOD("entry_history", "slot"),
        &NetwPredictionEngine::entry_history
    );
    ClassDB::bind_method(
        D_METHOD("trim_history", "slot", "ack"),
        &NetwPredictionEngine::trim_history
    );
    ClassDB::bind_method(
        D_METHOD(
            "command_admit", "slot", "transition", "label", "fresh", "command"
        ),
        &NetwPredictionEngine::command_admit
    );
    ClassDB::bind_method(
        D_METHOD("command_has", "slot", "transition"),
        &NetwPredictionEngine::command_has
    );
    ClassDB::bind_method(
        D_METHOD("command_label_of", "slot", "transition"),
        &NetwPredictionEngine::command_label_of
    );
    ClassDB::bind_method(
        D_METHOD("command_is_fresh", "slot", "transition"),
        &NetwPredictionEngine::command_is_fresh
    );
    ClassDB::bind_method(
        D_METHOD("command_payload_of", "slot", "transition"),
        &NetwPredictionEngine::command_payload_of
    );
    ClassDB::bind_method(
        D_METHOD("command_depth_from", "slot", "cursor"),
        &NetwPredictionEngine::command_depth_from
    );
    ClassDB::bind_method(
        D_METHOD("command_transitions", "slot"),
        &NetwPredictionEngine::command_transitions
    );
    ClassDB::bind_method(
        D_METHOD("record_idle_drive", "slot", "label", "kind"),
        &NetwPredictionEngine::record_idle_drive
    );
    ClassDB::bind_method(
        D_METHOD("record_authoring_clamp", "slot"),
        &NetwPredictionEngine::record_authoring_clamp
    );
    ClassDB::bind_method(
        D_METHOD("record_speculation_hold", "slot"),
        &NetwPredictionEngine::record_speculation_hold
    );
    ClassDB::bind_method(
        D_METHOD("refresh_ack_age", "slot"),
        &NetwPredictionEngine::refresh_ack_age
    );
    ClassDB::bind_method(
        D_METHOD("mark_authority_ack", "slot", "transition"),
        &NetwPredictionEngine::mark_authority_ack
    );
    ClassDB::bind_method(
        D_METHOD("drive_stats", "slot"),
        &NetwPredictionEngine::drive_stats
    );
    ClassDB::bind_method(
        D_METHOD("journal_has", "slot", "transition"),
        &NetwPredictionEngine::journal_has
    );
    ClassDB::bind_method(
        D_METHOD("drive_cursors", "slot"),
        &NetwPredictionEngine::drive_cursors
    );
    ClassDB::bind_method(
        D_METHOD("compare_stats", "slot"),
        &NetwPredictionEngine::compare_stats
    );

    BIND_ENUM_CONSTANT(CURSOR_TAPE_EPOCH);
    BIND_ENUM_CONSTANT(CURSOR_NEXT_TAPE_ENTRY);
    BIND_ENUM_CONSTANT(CURSOR_LAST_DRIVEN_ENTRY);
    BIND_ENUM_CONSTANT(CURSOR_LATEST_INPUT_TICK);
    BIND_ENUM_CONSTANT(CURSOR_LAST_DRIVEN_INPUT_TICK);
    BIND_ENUM_CONSTANT(CURSOR_LAST_FRAME_TRANSITION_TICK);
    BIND_ENUM_CONSTANT(CURSOR_COUNT);
    BIND_ENUM_CONSTANT(STAT_DRIVE_SEQ);
    BIND_ENUM_CONSTANT(STAT_LAST_DRIVE_LABEL);
    BIND_ENUM_CONSTANT(STAT_LAST_DRIVE_KIND);
    BIND_ENUM_CONSTANT(STAT_TAPE_EPOCH);
    BIND_ENUM_CONSTANT(STAT_TAPE_INDEX);
    BIND_ENUM_CONSTANT(STAT_ACK_AGE_TICKS);
    BIND_ENUM_CONSTANT(STAT_AUTHORING_CLAMPED);
    BIND_ENUM_CONSTANT(STAT_SPECULATION_HELD);
    BIND_ENUM_CONSTANT(STAT_CHAIN_BREAKS);
    BIND_ENUM_CONSTANT(STAT_QUANTUM_STEPS);
    BIND_ENUM_CONSTANT(STAT_QUANTUM_DECLARED);
    BIND_ENUM_CONSTANT(STAT_QUANTUM_FAULTS);
    BIND_ENUM_CONSTANT(STAT_OWNER_LOST);
    BIND_ENUM_CONSTANT(STAT_COUNT);
    BIND_ENUM_CONSTANT(STAT_COMPARISONS_RAN);
    BIND_ENUM_CONSTANT(STAT_COMPARISONS_SKIPPED);
    BIND_ENUM_CONSTANT(STAT_FP_VERIFIED);
    BIND_ENUM_CONSTANT(STAT_FP_MISMATCHES);
    BIND_ENUM_CONSTANT(STAT_FIRST_DIVERGENT_TRANSITION);
    BIND_ENUM_CONSTANT(STAT_COMPARE_COUNT);
    BIND_ENUM_CONSTANT(PASS_ISLAND_TICK);
    BIND_ENUM_CONSTANT(PASS_ISLAND_FRAME);
    BIND_ENUM_CONSTANT(PASS_JOINT);
    BIND_ENUM_CONSTANT(PASS_TICK);
    BIND_ENUM_CONSTANT(PASS_FRAME);
    BIND_ENUM_CONSTANT(PASS_FINALIZE_FRAME);
    BIND_ENUM_CONSTANT(ISLAND_NONE);
    BIND_ENUM_CONSTANT(STAT_EPISODE_ID);
    BIND_ENUM_CONSTANT(STAT_EPISODE_STATE);
    BIND_ENUM_CONSTANT(STAT_EPISODE_OPENED);
    BIND_ENUM_CONSTANT(STAT_EPISODE_ATTRIBUTION);
    BIND_ENUM_CONSTANT(STAT_EPISODE_NON_CONTRACTION);
    BIND_ENUM_CONSTANT(STAT_EPISODE_WITHHELD_NC);
    BIND_ENUM_CONSTANT(STAT_EPISODE_EVIDENCE_FREE_NC);
    BIND_ENUM_CONSTANT(STAT_EPISODE_NO_TRIGGER_NC);
    BIND_ENUM_CONSTANT(STAT_EPISODE_MIXED_TRIGGER_NC);
    BIND_ENUM_CONSTANT(STAT_EPISODE_CLOSURE_USED);
    BIND_ENUM_CONSTANT(STAT_EPISODE_AGREEMENT_RUN);
    BIND_ENUM_CONSTANT(STAT_EPISODE_LAST_COMPARISON);
    BIND_ENUM_CONSTANT(STAT_EPISODE_AGREEMENT_WRITE_ID);
    BIND_ENUM_CONSTANT(STAT_EPISODE_LAST_WRITE_ID);
    BIND_ENUM_CONSTANT(STAT_EPISODE_EVIDENCE_DROPPED);
    BIND_ENUM_CONSTANT(STAT_EPISODE_WRITE_COUNT);
    BIND_ENUM_CONSTANT(STAT_EPISODE_COMPARISON_COUNT);
    BIND_ENUM_CONSTANT(STAT_EPISODE_ACTIVE);
    BIND_ENUM_CONSTANT(STAT_EPISODE_TRANSPORT_DECIDED);
    BIND_ENUM_CONSTANT(STAT_EPISODE_DISSIPATE_DECIDED);
    BIND_ENUM_CONSTANT(STAT_EPISODE_COUNT);
    BIND_ENUM_CONSTANT(CARRY_CARRIED);
    BIND_ENUM_CONSTANT(CARRY_DECLINED);
    BIND_ENUM_CONSTANT(CARRY_UNFAITHFUL);
    BIND_ENUM_CONSTANT(CARRY_RETIRED_ALREADY);
    BIND_ENUM_CONSTANT(CARRY_RETIRED_SCHEDULE);
    BIND_ENUM_CONSTANT(CARRY_RETIRED_IMPURE);
    BIND_ENUM_CONSTANT(CARRY_RETIRED_INFIDELITY);
    BIND_ENUM_CONSTANT(ISLAND_DECLARED);
    BIND_ENUM_CONSTANT(ISLAND_JOINT);
    BIND_ENUM_CONSTANT(JOINT_FLOOR_ACK);
    BIND_ENUM_CONSTANT(JOINT_FLOOR_STATE);
    BIND_ENUM_CONSTANT(JOINT_FLOOR_RELAY);
    BIND_ENUM_CONSTANT(JOINT_FLOOR_EPOCH);
    BIND_ENUM_CONSTANT(STAT_JOINT_PASSES);
    BIND_ENUM_CONSTANT(STAT_JOINT_MEMBERS);
    BIND_ENUM_CONSTANT(STAT_JOINT_CELLS_RELAYED);
    BIND_ENUM_CONSTANT(STAT_JOINT_CELLS_SUBSTITUTED);
    BIND_ENUM_CONSTANT(STAT_JOINT_HEAL_SNAPS);
    BIND_ENUM_CONSTANT(STAT_JOINT_LINGER_HELD);
    BIND_ENUM_CONSTANT(STAT_JOINT_FLOOR);
    BIND_ENUM_CONSTANT(STAT_JOINT_PRESENT);
    BIND_ENUM_CONSTANT(STAT_JOINT_MAX_DEPTH);
    BIND_ENUM_CONSTANT(STAT_JOINT_FLOOR_ACK_MOVES);
    BIND_ENUM_CONSTANT(STAT_JOINT_FLOOR_STATE_MOVES);
    BIND_ENUM_CONSTANT(STAT_JOINT_FLOOR_RELAY_MOVES);
    BIND_ENUM_CONSTANT(STAT_JOINT_FLOOR_EPOCH_MOVES);
    BIND_ENUM_CONSTANT(STAT_JOINT_COUNT);

    const PropertyInfo report(
        Variant::OBJECT,
        "report",
        PROPERTY_HINT_RESOURCE_TYPE,
        "NetwPredictEpisodeReport"
    );
    ADD_SIGNAL(MethodInfo(
        SIG_EPISODE_OPENED,
        PropertyInfo(Variant::INT, "slot"),
        report
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_EPISODE_CLOSED,
        PropertyInfo(Variant::INT, "slot"),
        report
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_EPISODE_FALLBACK,
        PropertyInfo(Variant::INT, "slot"),
        report
    ));
}

} // namespace netw

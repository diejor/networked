#include "netw/predict/engine.hpp"

#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction.hpp"
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
#include "netw/object_port.hpp"
#include "netw/predict/axes.hpp"
#include "netw/predict/frames.hpp"
#include "netw/predict/tick_work.hpp"
#include "netw/project.hpp"
#include "netw/profile.hpp"
#include "netw/store/port.hpp"

using namespace godot;

namespace netw {

using repl::PropertyBindingPtr;
using repl::PropertyColumn;
using repl::PropertySet;

namespace {

constexpr int ACK_WINDOW_MAX = 64;

String participant_key(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return String();
    }
    const String named = String(p_entity->get_name());
    if (!named.is_empty()) {
        return named;
    }
    const int64_t route = p_entity->get_route();
    if (route > 0) {
        return String("route:") + String::num_int64(route);
    }
    Node *root = p_entity->get_owner();
    if (root != nullptr) {
        return String("path:") + String(root->get_path());
    }
    return String();
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

bool recovery_request_in_range(
    int p_policy,
    int p_domain,
    int p_attribution,
    double p_tick_delta
) {
    NETW_ERR_COND_V(
        p_policy < int(RecoveryPolicy::REBASE_REPLAY)
            || p_policy > int(RecoveryPolicy::OBSERVE),
        false,
        sys::PREDICTION,
        "A correction was skipped because recovery policy %d is not a "
        "NetwPrediction.RecoveryPolicy value.",
        p_policy
    );
    NETW_ERR_COND_V(
        p_domain < int(predict::Domain::IN_DOMAIN)
            || p_domain > int(predict::Domain::OUT_OF_DOMAIN),
        false,
        sys::PREDICTION,
        "A correction was skipped because domain %d is not a "
        "NetwPrediction.Domain value.",
        p_domain
    );
    NETW_ERR_COND_V(
        p_attribution < int(predict::Attribution::UNKNOWN)
            || p_attribution > int(predict::Attribution::CLOSURE),
        false,
        sys::PREDICTION,
        "A correction was skipped because attribution %d is not a "
        "NetwPrediction.Attribution value.",
        p_attribution
    );
    NETW_ERR_COND_V(
        p_tick_delta <= 0.0,
        false,
        sys::PREDICTION,
        "A correction was skipped because the tick length is not positive."
    );
    return true;
}

} // namespace

predict::RecoveryRequest recovery_request(
    int p_width,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_authority,
    const predict::StateRow &p_current,
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
    if (!recovery_request_in_range(
            p_policy,
            p_domain,
            p_attribution,
            p_tick_delta
        )) {
        return out;
    }
    out.predicted = p_predicted;
    out.authority = p_authority;
    out.current = p_current;
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

NetwPredictionEngine::~NetwPredictionEngine() {
    for (predict::Slot *row : rows) {
        if (row != nullptr) {
            memdelete(row);
        }
    }
    rows.clear();
}

#if defined(NETW_PROFILING)
void NetwPredictionEngine::held_bytes(
    int64_t &r_transitions,
    int64_t &r_inputs,
    int64_t &r_journal,
    int64_t &r_slots
) const {
    r_slots += held::vector(rows) + held::vector(order_keys)
        + held::vector(order_slots) + held::map(slot_by_entity)
        + held::map(entity_by_slot);
    for (const predict::Slot *row : rows) {
        if (row == nullptr) {
            continue;
        }
        r_transitions += row->book.held_bytes();
        r_inputs += row->inputs.held_bytes();
        r_journal += row->journal.held_bytes() + row->tape.held_bytes();
        r_slots += int64_t(sizeof(predict::Slot))
            + held::vector(row->owner_has_state)
            + held::vector(row->owner_has_input)
            + held::vector(row->state_ports) + held::vector(row->input_ports)
            + held::map(row->sensors) + held::map(row->carry_rules)
            + row->command_scratch.held_bytes()
            + row->fingerprint_scratch.held_bytes()
            + held::vector(row->correction_tolerances)
            + held::vector(row->meter_tolerances)
            + held::vector(row->judged_errors)
            + held::vector(row->carry_attempts);
        for (const predict::StateRow *state :
             {&row->pulled,
              &row->compared,
              &row->received,
              &row->arrived,
              &row->command,
              &row->frame_command,
              &row->stall,
              &row->last,
              &row->live,
              &row->state}) {
            r_slots += held::vector(state->values)
                + held::vector(state->present);
        }
    }
}
#endif

const predict::Slot *NetwPredictionEngine::row_of(int64_t p_slot) const {
    const int64_t seat = ids.seat(p_slot);
    const predict::Slot *row = seat >= 0 && seat < int64_t(rows.size())
        ? rows[uint32_t(seat)]
        : nullptr;
    return row != nullptr && row->open ? row : nullptr;
}

predict::Slot *NetwPredictionEngine::mutable_row_of(int64_t p_slot) {
    const int64_t seat = ids.seat(p_slot);
    predict::Slot *row = seat >= 0 && seat < int64_t(rows.size())
        ? rows[uint32_t(seat)]
        : nullptr;
    return row != nullptr && row->open ? row : nullptr;
}

int64_t NetwPredictionEngine::open(
    const LocalVector<predict::FieldDecl> &p_declaration
) {
    if (!roster_open("open")) {
        return -1;
    }
    const int64_t slot = ids.acquire();
    const uint32_t seat = SlotIds::index_of(slot);
    predict::Slot *row = memnew(predict::Slot);
    row->open = true;
    const uint32_t depth = predict::book_depth_for(row->config.schedule);
    row->book.set_depth(depth);
    row->journal.set_capacity(int(depth));
    row->journal.arm_diagnostics(journal_readers);
    if (!p_declaration.is_empty()) {
        row->rewire(predict::compile(p_declaration));
    }
    while (rows.size() <= seat) {
        rows.push_back(nullptr);
    }
    rows[seat] = row;
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
    predict::Slot *found = mutable_row_of(p_slot);
    if (found == nullptr) {
        return;
    }
    found->rewire(predict::compile(p_declaration));
    found->input_codec.clear();
    for (uint32_t at = 0; at < p_input.size(); ++at) {
        found->input_codec.append(
            p_input[at].key,
            p_input[at].quantizer,
            p_input[at].type,
            p_input[at].column_type
        );
    }
    found->input_codec.seal();
    if (found->port.bound()) {
        resolve_reach(*found, resolve_owner(*found));
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
    Object *owner = const_cast<Object *>(p_owner);
    store::resolve_columns(owner, p_row.wiring.codec.keys, p_row.state_ports);
    store::resolve_columns(owner, p_row.input_codec.keys, p_row.input_ports);
    p_row.pulled.resize(p_row.wiring.codec.count());
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
    NETW_TRACE(
        sys::PREDICTION,
        "Predicted entity %d is bound to its node.",
        p_slot
    );
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
        row->state_ports.clear();
        row->input_ports.clear();
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

namespace {

bool reaches(const LocalVector<uint8_t> &p_reach, uint32_t p_field) {
    return p_field < p_reach.size() && p_reach[p_field] != 0;
}

void read_binding(
    const PropertyBindingPtr &p_binding,
    const predict::FieldCodec &p_codec,
    predict::StateRow &r_row
) {
    const int count = p_codec.count();
    r_row.resize(count);
    for (int at = 0; at < count; ++at) {
        r_row.values[uint32_t(at)] = Variant();
    }
    Node *bound = p_binding != nullptr ? p_binding->node() : nullptr;
    if (bound == nullptr) {
        return;
    }
    Variant value;
    for (int at = 0; at < count; ++at) {
        const StringName &key = p_codec.keys[uint32_t(at)];
        if (p_binding->field_of(key) != nullptr
            && p_binding->read_field(bound, key, value)) {
            r_row.set(at, value);
        }
    }
}

void project_row(
    const predict::Wiring &p_wiring,
    double p_age,
    predict::StateRow &r_row
) {
    if (p_age <= 0.0) {
        return;
    }
    const predict::StateRow source = r_row;
    const int count
        = std::min(p_wiring.count(), int(source.values.size()));
    for (int at = 0; at < count; ++at) {
        const int channel = p_wiring.projection[uint32_t(at)];
        if (channel < 0 || channel >= count || !source.has(at)
            || !source.has(channel)) {
            continue;
        }
        const Variant &value = source.values[uint32_t(at)];
        if (project::supports(int(value.get_type()))) {
            r_row.set(
                at,
                project::forward(
                    value,
                    source.values[uint32_t(channel)],
                    p_age
                )
            );
        }
    }
}

bool pull_fields(
    Object *p_owner,
    const predict::FieldCodec &p_codec,
    const LocalVector<store::Column> &p_ports,
    const LocalVector<uint8_t> &p_reach,
    predict::StateRow &r_row
) {
    const uint32_t count = uint32_t(p_codec.count());
    if (r_row.values.size() != count) {
        r_row.resize(int(count));
    }
    store::BodyState body;
    bool probed = false;
    for (uint32_t at = 0; at < count; ++at) {
        r_row.present[at] = 0;
        if (!reaches(p_reach, at)) {
            continue;
        }
        const StringName &key = p_codec.keys[at];
        const bool through_body
            = at < p_ports.size() && p_ports[at].port == store::Port::BODY;
        if (through_body && !probed) {
            store::body_probe(p_owner, body);
            store::body_read(body);
            probed = true;
        }
        const store::BodyKey part
            = through_body ? p_ports[at].body : store::BodyKey::NONE;
        r_row.set(
            int(at),
            through_body ? store::body_value(body, part, p_owner, key)
                         : store::object_value(p_owner, key)
        );
    }
    return true;
}

} // namespace

bool NetwPredictionEngine::pull_state(predict::Slot &p_row) {
    NETW_ZONE_LEAF(
        "Predict pull state",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    return pull_fields(
        owner,
        p_row.wiring.codec,
        p_row.state_ports,
        p_row.owner_has_state,
        p_row.pulled
    );
}

bool NetwPredictionEngine::push_state(predict::Slot &p_row) {
    NETW_ZONE_LEAF(
        "Predict push state",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    const predict::StateRow &values = p_row.pulled;
    const uint32_t count = std::min(
        uint32_t(p_row.wiring.codec.count()),
        values.values.size()
    );
    for (int rank = 0; rank < store::PUSH_RANKS; ++rank) {
        for (uint32_t at = 0; at < count; ++at) {
            const int held = at < p_row.state_ports.size()
                ? store::push_rank(p_row.state_ports[at])
                : 0;
            if (held == rank && reaches(p_row.owner_has_state, at)
                && values.has(int(at))) {
                port_set(owner, p_row.wiring.codec.keys[at], values.values[at]);
            }
        }
    }
    return true;
}

bool NetwPredictionEngine::push_payload(
    predict::Slot &p_row,
    const predict::FieldCodec &p_codec,
    const LocalVector<uint8_t> &p_reach,
    const Dictionary &p_payload
) {
    NETW_ZONE_LEAF(
        "Predict push payload",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    for (int at = 0; at < p_codec.count(); ++at) {
        const StringName &key = p_codec.keys[at];
        if (reaches(p_reach, uint32_t(at)) && p_payload.has(key)) {
            port_set(owner, key, p_payload[key]);
        }
    }
    return true;
}

bool NetwPredictionEngine::push_row(
    predict::Slot &p_row,
    const predict::FieldCodec &p_codec,
    const LocalVector<uint8_t> &p_reach,
    const predict::StateRow &p_values
) {
    NETW_ZONE_LEAF(
        "Predict push row",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    const int count = std::min(p_codec.count(), int(p_values.values.size()));
    for (int at = 0; at < count; ++at) {
        if (reaches(p_reach, uint32_t(at)) && p_values.has(at)) {
            port_set(owner, p_codec.keys[at], p_values.values[uint32_t(at)]);
        }
    }
    return true;
}

bool NetwPredictionEngine::capture_canonical(
    int64_t p_slot,
    predict::StateRow &r_row
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    if (row->port.bound()) {
        if (!pull_state(*row)) {
            r_row.resize(row->wiring.count());
            return false;
        }
        r_row = row->pulled;
    } else if (state_binding_in(*row) != nullptr) {
        read_binding(state_binding_in(*row), row->wiring.codec, r_row);
    } else {
        r_row.resize(row->wiring.count());
        return false;
    }
    predict::canonicalize_row(
        row->wiring.codec,
        r_row,
        row->recovering.scratch,
        row->recovering.codes
    );
    return true;
}

bool NetwPredictionEngine::capture_predicted(int64_t p_slot, int64_t p_key) {
    NETW_ZONE_LEAF(
        "Predict capture into book",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    if (!row->port.bound()) {
        capture_canonical(p_slot, row->pulled);
        return row->record_state_row(p_key, row->pulled);
    }
    return pull_state(*row) && row->record_state_row(p_key, row->pulled);
}

void NetwPredictionEngine::capture_into_book(int64_t p_slot, int64_t p_key) {
    capture_predicted(p_slot, p_key);
}

bool NetwPredictionEngine::read_predicted(
    int64_t p_slot,
    int64_t p_key,
    predict::StateRow &r_state
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        && row->book.read_row(row->wiring.codec, p_key, r_state);
}

bool NetwPredictionEngine::restore_predicted(int64_t p_slot, int64_t p_key) {
    NETW_ZONE_LEAF(
        "Predict restore from book",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->port.bound()
        || !row->book.read_row(row->wiring.codec, p_key, row->pulled)
        || !push_state(*row)) {
        return false;
    }
    port_sync_transform(row->port.resolve(sys::PREDICTION));
    return true;
}

bool NetwPredictionEngine::seat_restore(
    int64_t p_slot,
    int64_t p_key,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->port.bound() || p_key < 0) {
        return false;
    }
    predict::StateRow &seed = row->pulled;
    const int count = row->wiring.codec.count();
    if (!row->book.read_row(row->wiring.codec, p_key, seed)
        || int(seed.values.size()) != count) {
        seed.resize(count);
    }
    for (int at = 0; at < count; ++at) {
        const StringName &key = row->wiring.codec.keys[at];
        if (p_payload.has(key)) {
            seed.set(at, p_payload[key]);
        }
    }
    return seed.any() && row->record_state_row(p_key, seed);
}

bool NetwPredictionEngine::seat_restore_row(
    int64_t p_slot,
    int64_t p_key,
    const predict::StateRow &p_restore
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->port.bound() || p_key < 0) {
        return false;
    }
    predict::StateRow &seed = row->pulled;
    const int count = row->wiring.codec.count();
    if (!row->book.read_row(row->wiring.codec, p_key, seed)
        || int(seed.values.size()) != count) {
        seed.resize(count);
    }
    const int given = std::min(count, int(p_restore.values.size()));
    for (int at = 0; at < given; ++at) {
        if (p_restore.has(at)) {
            seed.set(at, p_restore.values[uint32_t(at)]);
        }
    }
    return seed.any() && row->record_state_row(p_key, seed);
}

Dictionary NetwPredictionEngine::capture_state(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !pull_state(*row)) {
        return Dictionary();
    }
    return payload_of(row->wiring.codec, row->pulled);
}

bool NetwPredictionEngine::hold_live_input(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && pull_command(*row, row->live);
}

bool NetwPredictionEngine::restore_live_input(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && push_command(*row, row->live);
}

bool NetwPredictionEngine::capture_command(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && pull_command(*row, row->command);
}

bool NetwPredictionEngine::apply_command(
    int64_t p_slot,
    const predict::StateRow &p_command
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && push_command(*row, p_command);
}

predict::StateRow NetwPredictionEngine::bound_input(int64_t p_slot) {
    predict::StateRow out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr && input_binding_in(*row) != nullptr
        && input_binding_in(*row)->node() != nullptr) {
        pull_command(*row, out);
    }
    return out;
}

bool NetwPredictionEngine::pull_command(
    predict::Slot &p_row,
    predict::StateRow &r_command
) {
    const int count = p_row.input_codec.count();
    if (int(r_command.values.size()) != count) {
        r_command.resize(count);
    }
    for (int at = 0; at < count; ++at) {
        r_command.values[uint32_t(at)] = Variant();
        r_command.present[uint32_t(at)] = 0;
    }
    repl::PropertyBinding *const binding = input_binding_in(p_row);
    if (binding != nullptr && binding->node() != nullptr) {
        read_binding(binding, p_row.input_codec, r_command);
        return true;
    }
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    return pull_fields(
        owner,
        p_row.input_codec,
        p_row.input_ports,
        p_row.owner_has_input,
        r_command
    );
}

int32_t NetwPredictionEngine::command_hash_of(
    predict::Slot &p_row,
    const predict::StateRow &p_command
) {
    bool any = false;
    const int32_t hashed = predict::fingerprint_row(
        p_row.input_codec,
        p_command,
        ~uint64_t(0),
        p_row.command_scratch,
        any
    );
    return any ? hashed : predict::fnv1a(nullptr, 0);
}

int64_t NetwPredictionEngine::command_hash_row(
    int64_t p_slot,
    const predict::StateRow &p_command
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return predict::fnv1a(nullptr, 0);
    }
    return command_hash_of(*row, p_command);
}

bool NetwPredictionEngine::author_command(int64_t p_slot, int64_t p_tick) {
    NETW_PREDICT_TICK_SCOPE();
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !pull_command(*row, row->command)) {
        return false;
    }
    predict::canonicalize_row(
        row->input_codec,
        row->command,
        row->command_scratch,
        row->command_codes
    );
    if (row->lane_open && row->config.schedule != int(Schedule::FRAME)) {
        row->record_authored_row(p_tick, p_tick, true, row->command);
    }
    row->cursor.latest_input_tick = p_tick;
    if (input_binding_in(*row) != nullptr) {
        input_binding_in(*row)->set_authored_tick(p_tick);
    }
    record_input(p_slot, p_tick, command_hash_of(*row, row->command));
    return true;
}

const predict::StateRow *NetwPredictionEngine::command_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->command : nullptr;
}

bool NetwPredictionEngine::read_command_at(
    int64_t p_slot,
    int64_t p_transition,
    predict::StateRow &r_command
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        && row->inputs.read_row(row->input_codec, p_transition, r_command);
}

bool NetwPredictionEngine::apply_state(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr
        && push_payload(
               *row,
               row->wiring.codec,
               row->owner_has_state,
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
        "Prediction refused to %s an entity while a simulation step was "
        "running. Add or remove predicted entities outside the simulate "
        "callback.",
        p_verb
    );
    return false;
}

int NetwPredictionEngine::pass_depth() const {
    return depth;
}

bool NetwPredictionEngine::is_replaying() const {
    return replay_steps > 0;
}

void NetwPredictionEngine::call_step(
    const Callable &p_step,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    Array arguments;
    arguments.push_back(p_delta);
    arguments.push_back(p_tick);
    arguments.push_back(p_fresh);
    const int stale = p_fresh ? 0 : 1;
    replay_steps += stale;
    p_step.callv(arguments);
    replay_steps -= stale;
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

void note_errors(
    predict::Slot &p_row,
    predict::FieldReadings &p_into,
    const LocalVector<double> &p_errors
) {
    p_into.resize(p_row.wiring.count());
    const uint32_t count
        = std::min(p_errors.size(), uint32_t(p_row.wiring.count()));
    for (uint32_t at = 0; at < count; ++at) {
        if (p_errors[at] >= 0.0) {
            p_into.note(int(at), p_errors[at]);
        }
    }
}

void fill_correction_tolerances(
    const predict::Slot &p_row,
    double p_fallback_epsilon,
    LocalVector<double> &r_out
) {
    const uint32_t count = uint32_t(p_row.wiring.count());
    if (r_out.size() != count) {
        r_out.resize(count);
    }
    for (uint32_t field = 0; field < count; ++field) {
        if (p_row.wiring.vote_exclude[field] != 0) {
            r_out[field] = -1.0;
            continue;
        }
        const double declared = p_row.wiring.epsilon[field];
        r_out[field] = declared >= 0.0 ? declared : p_fallback_epsilon;
    }
}

void fill_meter_tolerances(
    const predict::Slot &p_row,
    int p_domain,
    double p_fallback_epsilon,
    LocalVector<double> &r_out
) {
    const uint32_t count = uint32_t(p_row.wiring.count());
    if (r_out.size() != count) {
        r_out.resize(count);
    }
    for (uint32_t field = 0; field < count; ++field) {
        const double declared = p_row.wiring.epsilon[field];
        if (p_row.wiring.trigger_exclude[field] != 0) {
            r_out[field] = p_row.wiring.causal[field] != 0 && declared >= 0.0
                ? declared
                : -1.0;
            continue;
        }
        if (p_row.wiring.withheld[field] != 0 && declared >= 0.0) {
            r_out[field] = declared;
            continue;
        }
        if (p_domain == int(predict::Domain::OUT_OF_DOMAIN)) {
            r_out[field] = declared >= 0.0 ? declared : p_fallback_epsilon;
            continue;
        }
        const Ref<NetwQuantize> &quantizer
            = p_row.wiring.codec.quantizers[field];
        r_out[field] = quantizer.is_valid()
            ? quantizer->max_error(
                  static_cast<Variant::Type>(p_row.wiring.codec.types[field])
              )
            : 0.0;
    }
}

PackedFloat64Array packed(const LocalVector<double> &p_values) {
    PackedFloat64Array out;
    out.resize(int64_t(p_values.size()));
    for (uint32_t at = 0; at < p_values.size(); ++at) {
        out.set(int64_t(at), p_values[at]);
    }
    return out;
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

const predict::StateRow *NetwPredictionEngine::frame_command_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->frame_command : nullptr;
}

void NetwPredictionEngine::hold_frame_command(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->frame_command = row->command;
    }
}

const predict::StateRow *NetwPredictionEngine::stall_row_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->stall : nullptr;
}

const predict::StateRow *NetwPredictionEngine::last_row_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->last : nullptr;
}

void NetwPredictionEngine::forget_last_input(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->last.resize(0);
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
        row->topology_open = false;
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
    const Ref<NetwPrediction> &p_handle
) {
    NETW_ZONE_LEAF(
        "Predict witness sample",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    if (p_handle.is_null()) {
        NETW_TRACE(
            sys::PREDICTION,
            "Predicted entity %d has no handle, so its contacts are not "
            "sampled.",
            p_slot
        );
        return Dictionary();
    }
    const Callable sampler = p_handle->get_witness_contacts();
    if (!sampler.is_valid()) {
        NETW_TRACE(
            sys::PREDICTION,
            "Predicted entity %d declares no contact sampling.",
            p_slot
        );
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
            "NetwPrediction.witness_contacts returned an unusable value, "
            "so it was ignored. Return a Dictionary with 'colliders' as an "
            "Array of live nodes, 'sleeping' as a bool, and 'continuous' as a "
            "Dictionary when present."
        );
    }
    NETW_TRACE(
        sys::PREDICTION,
        "Predicted entity %d declared contact sampling that cannot be used.",
        p_slot
    );
    return Dictionary();
}

bool NetwPredictionEngine::predict_command(
    int64_t p_slot,
    const Ref<NetwEntity> &p_entity,
    int64_t p_tick
) {
    NETW_ZONE_LEAF(
        "Predict predicted command",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    if (core() != nullptr) {
        coast_command(p_slot, row->command);
    } else {
        row->command.resize(row->input_codec.count());
        NETW_TRACE(
            sys::PREDICTION,
            "Predicted entity %d has no session, so it starts from empty "
            "state.",
            p_slot
        );
    }
    const NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    const Callable predictor
        = handle != nullptr ? handle->get_predict_input() : Callable();
    if (!predictor.is_valid()) {
        return seal_predicted_command(p_slot);
    }
    const Variant predicted = predictor.call(p_entity, p_tick);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    if (predicted.get_type() == Variant::DICTIONARY) {
        const Dictionary overlay = predicted;
        for (int at = 0; at < row->input_codec.count(); ++at) {
            const StringName &key = row->input_codec.keys[uint32_t(at)];
            if (overlay.has(key)) {
                row->command.set(at, overlay[key]);
            }
        }
        return seal_predicted_command(p_slot);
    }
    if (!invalid_command_predictor_reported_of(p_slot)) {
        set_invalid_command_predictor_reported(p_slot, true);
        NETW_ERROR(
            sys::PREDICTION,
            "NetwPrediction.predict_input for '%s' did not return a "
            "Dictionary, so a zeroed input was used. Return a Dictionary "
            "of input property names to values.",
            p_entity.is_valid() ? p_entity->get_name() : StringName()
        );
    }
    NETW_TRACE(
        sys::PREDICTION,
        "Predicted entity %d declared an input predictor that cannot be used.",
        p_slot
    );
    return seal_predicted_command(p_slot);
}

bool NetwPredictionEngine::seal_predicted_command(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    predict::canonicalize_row(
        row->input_codec,
        row->command,
        row->command_scratch,
        row->command_codes
    );
    return true;
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
    return row == nullptr ? 0 : row->tape_epoch;
}

void NetwPredictionEngine::set_tape_epoch(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->tape_epoch = p_value;
    }
}

int64_t NetwPredictionEngine::next_tape_entry_index_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->next_tape_entry_index;
}

void NetwPredictionEngine::set_next_tape_entry_index(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->next_tape_entry_index = p_value;
    }
}

int64_t NetwPredictionEngine::last_driven_entry_index_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? -1 : row->last_driven_entry_index;
}

void NetwPredictionEngine::set_last_driven_entry_index(
    int64_t p_slot,
    int64_t p_value
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->last_driven_entry_index = p_value;
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
    return row == nullptr ? 1.0 / 60.0 : row->tick_delta;
}

void NetwPredictionEngine::set_tick_delta(int64_t p_slot, double p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->tick_delta = p_value;
    }
}

int64_t NetwPredictionEngine::frame_index_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 0 : row->frame_index;
}

void NetwPredictionEngine::set_frame_index(int64_t p_slot, int64_t p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->frame_index = p_value;
    }
}

int NetwPredictionEngine::declared_quantum_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? 1 : row->declared_quantum;
}

void NetwPredictionEngine::set_declared_quantum(int64_t p_slot, int p_value) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->declared_quantum = p_value;
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
    if (row == nullptr || state_binding_in(*row) == nullptr
        || state_binding_in(*row)->node() == nullptr
        || state_binding_in(*row)->get_set() == nullptr) {
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

int NetwPredictionEngine::simulation_subject_count(int64_t p_slot) const {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (host == nullptr || seated.is_null()) {
        return 0;
    }
    return int(host->sim_selection_count(seated->get_rid_handle()));
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
    LocalVector<int64_t> held;
    fill_pass_slots(p_phase, held);
    PackedInt64Array out;
    for (const int64_t slot : held) {
        out.push_back(slot);
    }
    return out;
}

void NetwPredictionEngine::fill_pass_slots(
    int p_phase,
    LocalVector<int64_t> &r_slots
) const {
    fill_ordered_slots();
    r_slots.clear();
    for (const int64_t slot : order_slots) {
        const predict::Slot *row = row_of(slot);
        if (row != nullptr && steps_in_phase(*row, p_phase)) {
            r_slots.push_back(slot);
        }
    }
}

PackedInt64Array NetwPredictionEngine::ordered_slots() const {
    fill_ordered_slots();
    PackedInt64Array out;
    out.resize(int(order_slots.size()));
    for (uint32_t at = 0; at < order_slots.size(); ++at) {
        out.set(int(at), order_slots[at]);
    }
    return out;
}

void NetwPredictionEngine::fill_ordered_slots() const {
    NETW_ZONE_NC("Predict ordered slots", colors::PREDICTION);
    LocalVector<int64_t> &keys = order_keys;
    LocalVector<int64_t> &slots = order_slots;
    keys.clear();
    slots.clear();
    for (uint32_t seat = 0; seat < rows.size(); ++seat) {
        const predict::Slot *row = rows[seat];
        if (row == nullptr) {
            continue;
        }
        keys.push_back(row->order_key < 0 ? INT64_MAX : row->order_key);
        slots.push_back(ids.id_at(seat));
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
}

bool NetwPredictionEngine::has_simulate(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->simulate.is_valid();
}

bool NetwPredictionEngine::push_command(
    predict::Slot &p_row,
    const predict::StateRow &p_command
) {
    const uint32_t count = std::min(
        uint32_t(p_row.input_codec.count()),
        p_command.values.size()
    );
    repl::PropertyBinding *const binding = input_binding_in(p_row);
    Node *bound = binding != nullptr ? binding->node() : nullptr;
    if (bound != nullptr) {
        for (uint32_t at = 0; at < count; ++at) {
            if (p_command.has(int(at))) {
                binding->write_field(
                    bound,
                    p_row.input_codec.keys[at],
                    p_command.values[at]
                );
            }
        }
        return true;
    }
    Object *owner = resolve_owner(p_row);
    if (owner == nullptr) {
        return false;
    }
    for (uint32_t at = 0; at < count; ++at) {
        if (reaches(p_row.owner_has_input, at) && p_command.has(int(at))) {
            port_set(owner, p_row.input_codec.keys[at], p_command.values[at]);
        }
    }
    return true;
}

bool NetwPredictionEngine::run_command(
    int64_t p_slot,
    const predict::StateRow &p_command,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    NETW_ZONE_LEAF(
        "Predict run step",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    {
        NETW_PREDICT_TICK_SCOPE();
        push_command(*row, p_command);
    }
    const Callable step = row->simulate;
    depth += 1;
    if (step.is_valid()) {
        NETW_ZONE_LEAF(
            "Predict game step",
            colors::PREDICTION,
            profile::SUBSYSTEM_PREDICTION
        );
        call_step(step, p_delta, p_tick, p_fresh);
    }
    depth -= 1;
    return mutable_row_of(p_slot) != nullptr;
}

Dictionary NetwPredictionEngine::canonicalize_state(
    int64_t p_slot,
    const Dictionary &p_payload
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? predict::canonicalize(row->wiring.codec, p_payload)
                          : p_payload.duplicate();
}

bool NetwPredictionEngine::coast_command(
    int64_t p_slot,
    predict::StateRow &r_command
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        r_command.resize(0);
        return false;
    }
    predict::zero_state_row(row->input_codec, r_command);
    return true;
}

int64_t NetwPredictionEngine::sample_environment(
    int64_t p_slot,
    int64_t p_epoch
) {
    NETW_ZONE_LEAF(
        "Predict sample environment",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
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
        predict::Slot *row = mutable_row_of(p_slot);
        if (row != nullptr) {
            unbind_store(p_slot);
            release_pins(*row);
            rows[SlotIds::index_of(p_slot)] = nullptr;
            ids.release(p_slot);
            memdelete(row);
        }
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
        NETW_ERR_V(
            -1,
            sys::PREDICTION,
            "Prediction cannot register a null entity."
        );
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
        "Predicted entity %d was added, %d in all.",
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
    for (const predict::Slot *row : rows) {
        count += row != nullptr && row->open ? 1 : 0;
    }
    return count;
}

void NetwPredictionEngine::bind_store(
    int64_t p_slot,
    const store::EntityRef &p_ref
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwMultiplayer *host = core();
    if (row == nullptr || host == nullptr) {
        return;
    }
    store::World &world = host->store();
    if (row->store != p_ref) {
        unbind_store(p_slot);
    }
    if (!world.holds(p_ref)) {
        return;
    }
    row->store = p_ref;
    world.bind_predict(p_ref.slot, p_slot);
}

void NetwPredictionEngine::unbind_store(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwMultiplayer *host = core();
    if (row == nullptr) {
        return;
    }
    const store::EntityRef held = row->store;
    row->store = store::EntityRef();
    if (host == nullptr) {
        return;
    }
    store::World &world = host->store();
    if (world.holds(held) && world.predict_of(held.slot) == p_slot) {
        world.bind_predict(held.slot, -1);
    }
}

store::EntityRef NetwPredictionEngine::store_ref_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->store : store::EntityRef();
}

void NetwPredictionEngine::seat_arrivals(
    int64_t p_slot,
    const predict::Feed &p_feed,
    ObjectID p_seat
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    row->state_listener = p_feed.state_listener;
    row->input_listener = p_feed.input_listener;
    row->arrival_seat = p_seat;
}

bool NetwPredictionEngine::takes_arrival(
    int64_t p_slot,
    const store::EntityRef &p_ref
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && p_ref.bound() && row->store == p_ref;
}

predict::Listener NetwPredictionEngine::listener_for(
    int64_t p_slot,
    const store::EntityRef &p_ref,
    const repl::PropertyBinding *p_binding
) const {
    const predict::Slot *row = row_of(p_slot);
    if (p_binding == nullptr || !takes_arrival(p_slot, p_ref)) {
        return predict::Listener::NONE;
    }
    if (p_binding->id == row->state_binding_id) {
        return row->state_listener;
    }
    if (p_binding->id == row->input_binding_id) {
        return row->input_listener;
    }
    return predict::Listener::NONE;
}

void NetwPredictionEngine::deliver(
    int64_t p_slot,
    predict::Listener p_listener,
    const predict::Arrival &p_arrival
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_listener == predict::Listener::NONE) {
        return;
    }
    NetwPrediction *seat = Object::cast_to<NetwPrediction>(
        gd::object_of(row->arrival_seat)
    );
    if (seat != nullptr) {
        seat->seat_arrival(p_listener, p_arrival);
    }
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
    const Ref<NetwPrediction> &p_handle
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_handle.is_null()) {
        return false;
    }
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    const RID entity = seated.is_valid() ? seated->get_rid_handle() : RID();
    const sim::Row *standing
        = host != nullptr ? host->sim_row_of(entity) : nullptr;
    const bool declared = standing != nullptr && standing->choice.declared();
    const bool selected = standing != nullptr
        && standing->selected_by.count() > 0 && !sim::leads(standing->mode);
    int seat = ISLAND_NONE;
    if (reconcile_of(p_slot) == int(NetwPrediction::RECONCILE_JOINT)) {
        seat = ISLAND_JOINT;
    } else if (declared || selected) {
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
        declared
            && !sim::claims_exact(
                standing->choice,
                p_handle->get_schedule()
                    == NetwSimulation::SCHEDULE_STEPPED
            ),
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
    const uint32_t depth = predict::book_depth_for(p_schedule);
    row->book.set_depth(depth);
    row->journal.set_capacity(int(depth));
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

PackedFloat64Array NetwPredictionEngine::divergence_columns(
    int64_t p_slot
) const {
    PackedFloat64Array row_out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || state_binding_in(*row) == nullptr
        || state_binding_in(*row)->get_set() == nullptr) {
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
        if (row->report.divergence.at(at, absent) <= epsilon) {
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
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !row->pending_provenance.held) {
        return out;
    }
    const predict::PendingProvenance &pending = row->pending_provenance;
    out[StringName("episode")] = pending.episode;
    out[StringName("write_id")] = pending.write_id;
    out[StringName("operator")] = pending.op;
    out[StringName("basis")] = pending.basis;
    return out;
}

void NetwPredictionEngine::set_pending_provenance(
    int64_t p_slot,
    const Dictionary &p_provenance
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    predict::PendingProvenance &pending = row->pending_provenance;
    pending = predict::PendingProvenance();
    if (p_provenance.is_empty()) {
        return;
    }
    pending.episode = int64_t(p_provenance.get(StringName("episode"), 0));
    pending.write_id = int64_t(p_provenance.get(StringName("write_id"), 0));
    pending.op = int64_t(p_provenance.get(StringName("operator"), 0));
    pending.basis = int64_t(p_provenance.get(StringName("basis"), -1));
    pending.held = true;
}

void NetwPredictionEngine::stamp_pending_provenance(
    int64_t p_slot,
    int64_t p_transition
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const predict::PendingProvenance staged = row->pending_provenance;
    const int write_id = staged.held ? int(staged.write_id) : 0;
    if (write_id <= 0) {
        return;
    }
    mark_provenance(
        p_slot,
        p_transition,
        int(staged.episode),
        write_id,
        int(staged.op),
        staged.basis
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
        "Input for schedule %d was not read, because only SCHEDULE_TICK and "
        "SCHEDULE_FRAME read input this way.",
        p_schedule
    );
    NETW_ERR_COND_V(
        p_missing_policy < int(MissingInput::STALL)
            || p_missing_policy > int(MissingInput::REPEAT_LAST),
        predict::ConsumeInputPlan(),
        sys::PREDICTION,
        "Input was not read, because missing input policy %d is not a "
        "NetwPrediction.MissingInput value.",
        p_missing_policy
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
        "Prediction got physics results for an entity it no longer predicts "
        "(slot %d).",
        p_slot
    );
    const int count = p_contact_ids.size();
    NETW_ERR_COND_V(
        p_witness_classes.size() != count || p_realizations.size() != count
            || p_outside_boundary.size() != count,
        false,
        sys::PREDICTION,
        "Prediction got contact lists of different lengths for one physics "
        "step."
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

sim::Facts NetwPredictionEngine::resolution_facts(
    int64_t p_slot,
    const Ref<NetwPrediction> &p_handle
) const {
    sim::Facts facts;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_handle.is_null()) {
        return facts;
    }
    facts.declared = true;
    facts.predicted = true;
    facts.input_rows = !row->axes.inputless;
    facts.session_authority_here = row->axes.authority;
    facts.controller_here = row->axes.controlled_locally;
    facts.fallback_latched = row->latches.fallback_latched;
    facts.delay_closed = p_handle->get_recovery_policy()
        == NetwPrediction::RECOVERY_POLICY_DELAY_CLOSED;
    facts.selection_count = uint32_t(simulation_subject_count(p_slot));
    return facts;
}

int NetwPredictionEngine::apply_mode(
    int64_t p_slot,
    const Ref<NetwPrediction> &p_handle,
    sim::Mode p_mode
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || p_handle.is_null()) {
        return int(NetwPrediction::ROLE_REMOTE);
    }
    const bool local = row->axes.controlled_locally;
    NetwPrediction::InputSource source = NetwPrediction::INPUT_SOURCE_NONE;
    NetwPrediction::SimMode simulation = NetwPrediction::SIM_MODE_DISPLAY;
    switch (p_mode) {
        case sim::Mode::AUTHORITY: {
            source = local ? NetwPrediction::INPUT_SOURCE_LOCAL
                           : NetwPrediction::INPUT_SOURCE_RECEIVED;
            simulation = NetwPrediction::SIM_MODE_AUTHORITATIVE;
        } break;
        case sim::Mode::PREDICT: {
            source = NetwPrediction::INPUT_SOURCE_LOCAL;
            simulation = NetwPrediction::SIM_MODE_SPECULATIVE;
        } break;
        case sim::Mode::ACTIVE: {
            source = NetwPrediction::INPUT_SOURCE_PREDICTED;
            simulation = NetwPrediction::SIM_MODE_SPECULATIVE;
        } break;
        case sim::Mode::NONE:
        case sim::Mode::PROXY: {
            source = local ? NetwPrediction::INPUT_SOURCE_LOCAL
                           : NetwPrediction::INPUT_SOURCE_NONE;
        } break;
    }
    if (row->axes.inputless) {
        source = NetwPrediction::INPUT_SOURCE_NONE;
    }
    p_handle->set_input_source(source);
    p_handle->set_sim_mode(simulation);
    return role_for_axes(source, simulation);
}

int NetwPredictionEngine::resolve_axes(
    int64_t p_slot,
    const Ref<NetwPrediction> &p_handle
) {
    return apply_mode(
        p_slot,
        p_handle,
        sim::resolve(resolution_facts(p_slot, p_handle))
    );
}

int NetwPredictionEngine::resolve_correction(
    int64_t p_slot,
    int p_declared
) const {
    const NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (p_declared == int(CorrectionMode::AUTO)) {
        if (handle != nullptr) {
            return handle->resolved_correction();
        }
        const predict::Slot *row = row_of(p_slot);
        const bool solves = row != nullptr && row->owner_solves;
        return solves ? int(CorrectionMode::SNAP) : int(CorrectionMode::REPLAY);
    }
    if (handle == nullptr) {
        return p_declared;
    }
    return predict::integrable_correction(
        p_declared,
        handle->get_schedule(),
        handle->body_solves()
    );
}

bool NetwPredictionEngine::recovery_replays(int64_t p_slot) const {
    const NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return true;
    }
    const int declared = predict::correction_for_recovery_policy(
        handle->get_recovery_policy()
    );
    return resolve_correction(p_slot, declared) == int(CorrectionMode::REPLAY);
}

int NetwPredictionEngine::settle_correction(int64_t p_slot, int p_declared) {
    const int correction = resolve_correction(p_slot, p_declared);
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || correction == p_declared
        || p_declared != int(CorrectionMode::REPLAY)
        || row->latches.integration_refusal_reported) {
        return correction;
    }
    row->latches.integration_refusal_reported = true;
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    const String named
        = seated.is_valid() ? String(seated->get_name()) : String();
    NETW_WARN(
        sys::PREDICTION,
        "'%s' is a physics body on SCHEDULE_FRAME, which cannot replay, so "
        "it uses NetwPrediction.RECOVERY_POLICY_REBASE_RECOVER. Use "
        "SCHEDULE_STEPPED with a stepper to get "
        "RECOVERY_POLICY_REBASE_REPLAY.",
        named.is_empty() ? "an entity" : named.utf8().get_data()
    );
    return correction;
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
    NETW_ZONE_NC("Predict demote for breach", colors::PREDICTION);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (!p_breached || handle == nullptr
        || role_of(p_slot) != NetwPrediction::ROLE_PREDICT
        || fallback_latched_of(p_slot)
        || handle->get_breach_response()
            != NetwPrediction::BREACH_RESPONSE_DEMOTE
        || !handle->get_witness_contacts().is_valid()) {
        return false;
    }
    if (journal_has(p_slot, p_transition)) {
        mark_domain(p_slot, p_transition, int(predict::Domain::OUT_OF_DOMAIN));
    }
    const bool opened
        = episode_state(p_slot) != NetwPrediction::EPISODE_STATE_OPEN;
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
        seated.is_valid() ? seated->get_name() : StringName(),
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
        "Prediction judged a correction for an entity it no longer predicts "
        "(slot %d).",
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
        "Prediction judged a correction for an entity it no longer predicts "
        "(slot %d).",
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
        return String("entity:") + participant_key(entity);
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
        return int(NetwPrediction::CONTACT_CLASS_UNPREDICTED_DYNAMIC);
    }
    if (!p_support.is_empty() && collider_identity(p_collider) == p_support) {
        return int(NetwPrediction::CONTACT_CLASS_DECLARED_SUPPORT);
    }
    if (p_collider->is_class("AnimatableBody2D")
        || p_collider->is_class("AnimatableBody3D")
        || p_collider->is_class("CharacterBody2D")
        || p_collider->is_class("CharacterBody3D")) {
        return int(NetwPrediction::CONTACT_CLASS_KINEMATIC_PROXY);
    }
    if (static_geometry(p_collider)) {
        return int(NetwPrediction::CONTACT_CLASS_OTHER_STATIC);
    }
    const bool rigid = p_collider->is_class("RigidBody2D")
        || p_collider->is_class("RigidBody3D");
    if (rigid && bool(p_collider->get(StringName("freeze")))) {
        return int(NetwPrediction::CONTACT_CLASS_KINEMATIC_PROXY);
    }
    const Ref<NetwEntity> entity = contact_entity(p_collider);
    if (entity.is_valid() && slot_of(entity) >= 0) {
        return int(NetwPrediction::CONTACT_CLASS_PREDICTED_DYNAMIC);
    }
    return int(NetwPrediction::CONTACT_CLASS_UNPREDICTED_DYNAMIC);
}

Array NetwPredictionEngine::input_keys(int64_t p_slot) const {
    Array out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || input_binding_in(*row) == nullptr) {
        return out;
    }
    const PropertySet *declared = input_binding_in(*row)->get_set();
    if (declared == nullptr) {
        return out;
    }
    for (const PropertyColumn &field : declared->columns) {
        if (field.get_lane() == int(PropertySet::VOLATILE)) {
            out.push_back(field.get_key());
        }
    }
    return out;
}

const SchemaRecord *NetwPredictionEngine::input_schema(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || input_binding_in(*row) == nullptr
        || input_binding_in(*row)->get_set() == nullptr) {
        return nullptr;
    }
    return &input_binding_in(*row)->get_set()->get_volatile_schema();
}

bool NetwPredictionEngine::decode_command_frame(
    int64_t p_slot,
    const PackedByteArray &p_payload,
    predict::CommandFrameRecord &r_frame
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || input_binding_in(*row) == nullptr
        || input_binding_in(*row)->get_set() == nullptr) {
        return false;
    }
    return predict::CommandFrameRecord::decode(
        input_binding_in(*row)->get_set()->get_volatile_schema(),
        p_payload,
        r_frame
    );
}

namespace {

LocalVector<int> command_fields(
    const predict::FieldCodec &p_codec,
    const Array &p_keys
) {
    LocalVector<int> out;
    out.resize(uint32_t(p_keys.size()));
    for (int position = 0; position < p_keys.size(); ++position) {
        const StringName key = p_keys[position];
        out[uint32_t(position)] = -1;
        for (int field = 0; field < p_codec.count(); ++field) {
            if (p_codec.keys[uint32_t(field)] == key) {
                out[uint32_t(position)] = field;
                break;
            }
        }
    }
    return out;
}

} // namespace

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
    const LocalVector<int> fields
        = command_fields(row_of(p_slot)->input_codec, p_keys);
    predict::StateRow command;
    command.resize(row_of(p_slot)->input_codec.count());
    int fresh_index = 0;
    for (int at = 0; at < p_frame.transition_count(); ++at) {
        const predict::TransitionWire &transition = p_frame.transition_at(at);
        const int64_t index = transition.index;
        if (!transition.fresh) {
            continue;
        }
        const int payload = fresh_index;
        fresh_index += 1;
        const int64_t floor = newest_matrix_transition_of(p_slot)
            - predict::TAPE_HISTORY_LIMIT + 1;
        if (index < floor) {
            tally[RELAY_DROPPED_LATE] += 1;
            continue;
        }
        const int standing = joint_provenance_at(p_slot, index);
        if (standing == int(predict::CellProvenance::RELAYED)
            || relayed_at(p_slot, index)) {
            continue;
        }
        p_frame.read_payload(payload, fields, command);
        const bool displaced
            = standing == int(predict::CellProvenance::SUBSTITUTED);
        set_newest_matrix_transition(
            p_slot,
            std::max(newest_matrix_transition_of(p_slot), index)
        );
        if (index >= 0) {
            joint_record(
                p_slot,
                index,
                predict::StateRow(),
                command,
                false,
                true,
                false
            );
            note_relayed(p_slot, index);
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
    if (!authors_commands || input_binding_in(*row) == nullptr
        || input_binding_in(*row)->get_set() == nullptr) {
        return PackedByteArray();
    }
    PropertySet *declared = input_binding_in(*row)->get_set();
    return build_command_frame(
        p_slot,
        declared->get_volatile_schema(),
        input_keys(p_slot),
        int(MAX(int64_t(1), declared->get_window() + 2))
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

namespace {

NetwPredictStats::Book *facts_in(Object *p_handle) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(p_handle);
    return handle != nullptr ? handle->fact_book() : nullptr;
}

} // namespace

int64_t NetwPredictionEngine::held_fact_of(int64_t p_slot, int p_fact) const {
    const NetwPredictStats::Book *counters = facts_in(handle_of(p_slot));
    return counters != nullptr ? counters->get_int_fact(p_fact) : 0;
}

void NetwPredictionEngine::set_held_fact(
    int64_t p_slot,
    int p_fact,
    int64_t p_value
) {
    NetwPredictStats::Book *counters = facts_in(handle_of(p_slot));
    if (counters != nullptr) {
        counters->set_int_fact(p_fact, p_value);
    }
}

void NetwPredictionEngine::add_held_fact(
    int64_t p_slot,
    int p_fact,
    int64_t p_delta
) {
    NetwPredictStats::Book *counters = facts_in(handle_of(p_slot));
    if (counters != nullptr) {
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
    const PackedInt64Array counts = receive_relayed_command_frame(
        p_slot,
        p_payload,
        reconcile_of(p_slot) == int(NetwPrediction::RECONCILE_JOINT)
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
    if (!input_lane_open(p_slot)) {
        return 0;
    }
    return newest_authored_input(p_slot) - next_input_tick_of(p_slot) + 1;
}

void NetwPredictionEngine::resync_input_if_stranded(
    int64_t p_slot,
    int64_t p_ceiling,
    int64_t p_buffer
) {
    if (p_ceiling <= 0 || queued_span(p_slot) <= p_ceiling) {
        return;
    }
    if (!input_lane_open(p_slot)) {
        return;
    }
    const int64_t standing = next_input_tick_of(p_slot);
    const int64_t target = newest_authored_input(p_slot) - p_buffer;
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
    const int64_t controller = seated->get_controller();
    host->notify(
        NetwMultiplayer::PREDICT_OWNER_DIVERGENCE,
        controller,
        p_transition,
        attribution
    );
}

Dictionary NetwPredictionEngine::capture_state_raw(int64_t p_slot) {
    if (core() != nullptr && owner_bound(p_slot)) {
        return capture_state(p_slot);
    }
    const PropertyBindingPtr binding = state_binding_of(p_slot);
    return binding != nullptr ? binding->snapshot_payload() : Dictionary();
}

Dictionary NetwPredictionEngine::canonical_state(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    if (core() != nullptr) {
        return p_slot >= 0 ? canonicalize_state(p_slot, p_payload)
                           : p_payload.duplicate();
    }
    const PropertyBindingPtr binding = state_binding_of(p_slot);
    return binding != nullptr ? binding->canonicalize_payload(p_payload)
                              : Dictionary();
}

Dictionary NetwPredictionEngine::capture_canonical_state(int64_t p_slot) {
    NETW_ZONE_LEAF(
        "Predict capture canonical state",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    return canonical_state(p_slot, capture_state_raw(p_slot));
}

void NetwPredictionEngine::record_command_input(
    int64_t p_slot,
    int64_t p_tick,
    const predict::StateRow &p_command
) {
    if (core() == nullptr) {
        return;
    }
    record_input(p_slot, p_tick, command_hash_row(p_slot, p_command));
}

void NetwPredictionEngine::run_replay_step(
    int64_t p_slot,
    const predict::StateRow &p_command,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    if (core() != nullptr && owner_bound(p_slot)) {
        run_command(p_slot, p_command, p_delta, p_tick, p_fresh);
        return;
    }
    apply_command(p_slot, p_command);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    const Callable step = handle->get_simulate();
    if (step.is_valid()) {
        call_step(step, p_delta, p_tick, p_fresh);
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
    predict::Slot *row = mutable_row_of(p_slot);
    int kind = int(DriveKind::REPEAT);
    bool applied_fresh = false;
    if (fresh) {
        if (row->inputs.read_row(row->input_codec, cursor, row->command)
            && row->command.any()) {
            row->last = row->command;
            kind = int(DriveKind::FRESH);
            applied_fresh = true;
        } else {
            add_held_fact(p_slot, NetwPredictStats::FACT_MISSING, 1);
            kind = int(DriveKind::MISSING);
        }
    }
    if (!applied_fresh) {
        row->command = row->last;
    }
    if (!row->command.any()) {
        row->command = row->stall;
    }
    record_drive_hashed(
        p_slot,
        cursor,
        label,
        kind,
        command_hash_row(p_slot, row->command),
        p_tick,
        true
    );
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    run_replay_step(p_slot, row->command, p_delta, label, applied_fresh);
    set_ack(p_slot, cursor);
    set_replay_cursor(p_slot, cursor + 1);
    set_last_replayed_label(p_slot, label);
    set_last_replayed_fresh(p_slot, fresh);
    add_held_fact(p_slot, NetwPredictStats::FACT_CONSUMED, 1);
    return true;
}

void NetwPredictionEngine::prepare_tick_tape(int64_t p_slot, int64_t p_tick) {
    if (core() == nullptr) {
        return;
    }
    const PackedInt64Array span = tape_span(p_slot);
    const int64_t newest = span.size() > 1 ? span[1] : -1;
    tape_prepare_tick(p_slot, p_tick);
    if (newest >= 0 && newest != p_tick - 1) {
        clear_witness_details(p_slot);
        drop_deferred_operator(p_slot);
        set_ack_of_acks(p_slot, -1);
        set_ack_domain_confirmed(p_slot, false);
    }
}

namespace {

String schedule_label(NetwSimulation::Schedule p_schedule) {
    switch (p_schedule) {
        case NetwSimulation::SCHEDULE_TICK:
            return "TICK";
        case NetwSimulation::SCHEDULE_FRAME:
            return "FRAME";
        case NetwSimulation::SCHEDULE_STEPPED:
            return "STEPPED";
        case NetwSimulation::SCHEDULE_AUTO:
            return "AUTO";
        default:
            return String::num_int64(p_schedule);
    }
}

String refusal_row(
    const Ref<NetwEntity> &p_member,
    const NetwPrediction *p_handle,
    const char *p_suffix
) {
    const String tier = p_handle != nullptr
        ? schedule_label(p_handle->get_schedule())
        : String("unknown");
    const StringName named
        = p_member.is_valid() ? p_member->get_name() : StringName();
    return String(named) + String(p_suffix) + tier + String(" tier");
}

} // namespace

void NetwPredictionEngine::refresh_simulation_gate(
    int64_t p_slot,
    bool p_force_release
) {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (host == nullptr || seated.is_null()) {
        return;
    }
    const RID entity = seated->get_rid_handle();
    const bool solver = handle != nullptr
        && handle->get_archetype()
            == int(NetwPrediction::ARCHETYPE_SOLVER_BODY);
    const sim::Row *row = host->sim_row_of(entity);
    const bool leads = row != nullptr && sim::leads(row->mode);
    host->simulation_gate_set(entity, !p_force_release && solver && leads);
}

void NetwPredictionEngine::host_local_step(
    int64_t p_slot,
    double p_delta,
    int64_t p_tick
) {
    if (row_of(p_slot) == nullptr) {
        return;
    }
    capture_command(p_slot);
    record_authored_command(p_slot, p_tick, p_tick, true, *command_of(p_slot));
    record_drive_hashed(
        p_slot,
        p_tick,
        p_tick,
        int(DriveKind::FRESH),
        command_hash_row(p_slot, *command_of(p_slot)),
        p_tick,
        false
    );
    if (row_of(p_slot) == nullptr) {
        return;
    }
    run_replay_step(p_slot, *command_of(p_slot), p_delta, p_tick, true);
    const PropertyBindingPtr binding = state_binding_of(p_slot);
    if (binding != nullptr) {
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
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    const int requested = handle != nullptr
        ? int(handle->get_reconcile_mode())
        : int(NetwPrediction::RECONCILE_INDEPENDENT);
    if (requested != int(NetwPrediction::RECONCILE_JOINT)) {
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
            Object::cast_to<NetwPrediction>(handle_of(seat)),
            " on a "
        ));
    }
    if (refusals.is_empty()) {
        return int(NetwPrediction::RECONCILE_JOINT);
    }
    if (!joint_refusal_reported_of(p_slot)) {
        set_joint_refusal_reported(p_slot, true);
        NETW_ERROR(
            sys::PREDICTION,
            "NetwPrediction.RECONCILE_JOINT needs every entity in the group to "
            "replay a tick, and these cannot (%s), so the group runs "
            "NetwPrediction.RECONCILE_INDEPENDENT. Give each one a "
            "NetwSimulation.step on SCHEDULE_TICK, or use "
            "SCHEDULE_STEPPED with a stepper.",
            String(", ").join(refusals)
        );
    }
    return int(NetwPrediction::RECONCILE_INDEPENDENT);
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
        p_mode == int(NetwPrediction::RECONCILE_JOINT)
    );
}

void NetwPredictionEngine::admit_reconcile_mode(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    row->reconcile_own = admitted_reconcile_mode(p_slot);
    const bool joined
        = row->reconcile_own == int(NetwPrediction::RECONCILE_JOINT);
    reconfigure_from(
        p_slot,
        Ref<NetwPrediction>(
            Object::cast_to<NetwPrediction>(handle_of(p_slot))
        )
    );
    const TypedArray<NetwEntity> members
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < members.size(); ++at) {
        const Ref<NetwEntity> member = members[at];
        if (member.is_null()) {
            continue;
        }
        const int64_t seat = sibling_slot(member);
        predict::Slot *held = mutable_row_of(seat);
        if (held == nullptr || held->reconcile_joined == joined) {
            continue;
        }
        const int before = reconcile_of(seat);
        held->reconcile_joined = joined;
        const int after = reconcile_of(seat);
        if (after != before) {
            follow_relay_subscription(seat, after);
        }
        reconfigure_from(
            seat,
            Ref<NetwPrediction>(
                Object::cast_to<NetwPrediction>(handle_of(seat))
            )
        );
    }
}

int NetwPredictionEngine::reconcile_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return int(NetwPrediction::RECONCILE_INDEPENDENT);
    }
    const bool joint = row->reconcile_joined
        || row->reconcile_own == int(NetwPrediction::RECONCILE_JOINT);
    return joint ? int(NetwPrediction::RECONCILE_JOINT)
                 : int(NetwPrediction::RECONCILE_INDEPENDENT);
}

void NetwPredictionEngine::set_reconcile_own(int64_t p_slot, int p_mode) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->reconcile_own = p_mode;
    }
}

void NetwPredictionEngine::leave_joint(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->reconcile_joined) {
        return;
    }
    const int before = reconcile_of(p_slot);
    row->reconcile_joined = false;
    const int after = reconcile_of(p_slot);
    if (after != before) {
        follow_relay_subscription(p_slot, after);
    }
    reconfigure_from(
        p_slot,
        Ref<NetwPrediction>(
            Object::cast_to<NetwPrediction>(handle_of(p_slot))
        )
    );
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

int64_t NetwPredictionEngine::replay_basis(
    const LocalVector<int64_t> &p_ticks
) {
    return p_ticks.is_empty() ? -1 : p_ticks[0] - 1;
}

bool NetwPredictionEngine::open_space_replay(
    int64_t p_slot,
    int64_t p_basis,
    SpaceReplay &r_replay
) {
    const predict::Slot *row = row_of(p_slot);
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (row == nullptr || host == nullptr || seated.is_null() || p_basis < 0
        || row->config.schedule != int(Schedule::STEPPED)) {
        return false;
    }
    r_replay.space = host->entity_space_of(seated).space;
    r_replay.stepper = host->predict_get_stepper(r_replay.space);
    if (r_replay.stepper.is_null()) {
        return false;
    }
    LocalVector<RID> replayed;
    replayed.push_back(seated->get_rid_handle());
    host->sim_hold_poses(r_replay.space, replayed, r_replay.bystanders);
    r_replay.stepper->restore(r_replay.space, p_basis);
    return true;
}

void NetwPredictionEngine::step_space_replay(
    SpaceReplay &r_replay,
    double p_delta,
    int64_t p_tick
) {
    if (r_replay.stepper.is_null()) {
        return;
    }
    r_replay.stepper->step(r_replay.space, p_delta);
    r_replay.stepper->snapshot(r_replay.space, p_tick);
}

void NetwPredictionEngine::close_space_replay(SpaceReplay &r_replay) {
    sim::restore_poses(r_replay.bystanders);
    r_replay.bystanders.clear();
}

int NetwPredictionEngine::resolved_schedule(int64_t p_slot, int p_declared) {
    if (p_declared != int(NetwSimulation::SCHEDULE_STEPPED)) {
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
            "'%s' uses NetwSimulation.SCHEDULE_STEPPED but physics "
            "space %d has no stepper, so it runs SCHEDULE_FRAME. Install one "
            "with NetwMultiplayer.predict_stepper_install, or choose another "
            "schedule.",
            seated.is_valid() ? String(seated->get_name())
                              : String("an entity"),
            int64_t(space.get_id())
        );
    }
    return int(NetwSimulation::SCHEDULE_FRAME);
}

bool NetwPredictionEngine::slot_is_steppable(int64_t p_slot) const {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr
        || handle->get_sim_mode() == NetwPrediction::SIM_MODE_DISPLAY
        || !handle->get_simulate().is_valid()) {
        return false;
    }
    if (handle->get_schedule() == NetwSimulation::SCHEDULE_TICK) {
        return true;
    }
    return handle->get_schedule() == NetwSimulation::SCHEDULE_STEPPED
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
    const int64_t age = drive_stat(p_slot, STAT_ACK_AGE_TICKS);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (age >= 0 && handle != nullptr) {
        handle->set_ack_age_ticks(int(age));
    }
    return age;
}

bool NetwPredictionEngine::speculation_horizon_full(int64_t p_slot) {
    refresh_owner_ack_age(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    return handle != nullptr
        && handle->get_ack_age_ticks() >= NetwPrediction::ACK_AGE_MAX;
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

    const bool coded = predict::codes_agree(
        row->input_codec,
        frame.row_plan(),
        p_schema,
        p_keys
    );
    const predict::StateRow &stall = row->stall;
    const LocalVector<int> fields = command_fields(row->input_codec, p_keys);
    predict::StateRow authored;
    LocalVector<Variant> values;
    values.resize(uint32_t(p_keys.size()));
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
        if (coded && row->holds_authored(index)
            && row->inputs.coded_whole(index)
            && frame.append_codes(row->inputs.image_of(index))) {
            continue;
        }
        row->unacknowledged_row(index, authored);
        for (int at = 0; at < p_keys.size(); ++at) {
            const int field = fields[uint32_t(at)];
            const predict::StateRow &source
                = field >= 0 && authored.has(field) ? authored : stall;
            values[uint32_t(at)] = field >= 0 && source.has(field)
                ? source.values[uint32_t(field)]
                : Variant();
        }
        frame.append_values(values);
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
        forget_last_input(p_slot);
    }
    set_owner_ack_floor(
        p_slot,
        std::max(owner_ack_floor_of(p_slot), p_frame.ack_of_acks())
    );

    record_owner_claims(p_slot, p_frame);

    const bool ticked = row->config.schedule == int(Schedule::TICK);
    const bool laned = row->lane_open;
    const LocalVector<int> fields = command_fields(row->input_codec, p_keys);
    predict::StateRow command;
    command.resize(row->input_codec.count());
    int fresh_seen = 0;
    for (int at = 0; at < p_frame.transition_count(); ++at) {
        const predict::TransitionWire &transition = p_frame.transition_at(at);
        const int64_t index = transition.index;
        if (index < 0) {
            continue;
        }
        const bool fresh = transition.fresh;
        const int64_t label = transition.label;
        if (fresh) {
            if (fresh_seen >= p_frame.payload_count()) {
                break;
            }
            p_frame.read_payload(fresh_seen, fields, command);
            fresh_seen += 1;
        } else {
            command.resize(row->input_codec.count());
        }
        if (!command_admit(p_slot, index, label, fresh, command)) {
            continue;
        }
        set_arrivals_this_frame(p_slot, arrivals_this_frame_of(p_slot) + 1);
        if (ticked && fresh && label >= 0 && laned
            && next_input_tick_of(p_slot) < 0) {
            set_next_input_tick(p_slot, label);
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
        ? row->last_driven_entry_index
        : row->cursor.latest_input_tick;
}

namespace {

const StringName &signal_recovered() {
    static const StringName name("recovered");
    return name;
}

void blank_row(predict::StateRow &r_row, int p_width) {
    r_row.resize(p_width);
    for (uint32_t at = 0; at < r_row.values.size(); ++at) {
        r_row.values[at] = Variant();
    }
}

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
    const predict::StateRow &p_state,
    int p_source
) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    set_joint_basis(p_slot, MAX(joint_basis_of(p_slot), p_basis));
    if (handle->get_schedule() == NetwSimulation::SCHEDULE_TICK) {
        if (predict::Slot *row = mutable_row_of(p_slot)) {
            row->record_state_row(p_basis + 1, p_state);
        }
        mark_carry_dirty(p_slot, p_basis + 1);
    }
    if (p_basis >= 0 && island_of(p_slot) != ISLAND_NONE) {
        joint_admit_state(p_slot, p_basis, p_state);
        joint_note_basis(p_slot, p_basis, p_source);
    }
}

void NetwPredictionEngine::joint_admit_state(
    int64_t p_slot,
    int64_t p_transition,
    const predict::StateRow &p_state
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const int count = row->wiring.count();
    predict::StateRow arrived;
    if (!row->joint_state(p_transition, arrived)
        || int(arrived.values.size()) != count) {
        arrived.resize(count);
    }
    const int span = std::min(count, int(p_state.values.size()));
    for (int at = 0; at < span; ++at) {
        if (p_state.has(at)) {
            arrived.set(at, p_state.values[uint32_t(at)]);
        }
    }
    if (arrived.any()) {
        row->record_state_row(p_transition + 1, arrived);
    }
}

void NetwPredictionEngine::admit_simulated_state(
    int64_t p_slot,
    const predict::Arrival &p_arrival
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    LocalVector<StringName> &keys = row->recovering.install_keys;
    LocalVector<Variant> &values = row->recovering.install_values;
    keys.clear();
    values.clear();
    const store::Table *table = p_arrival.table;
    if (table != nullptr && p_arrival.row < table->row_count()) {
        const int columns = table->schema.column_count();
        for (int column = 0; column < columns; ++column) {
            keys.push_back(table->schema.at(column)->key);
            values.push_back(
                table->read_value(p_arrival.row, uint32_t(column))
            );
        }
    }
    admit_simulated_rows(p_slot, p_arrival.tick, p_arrival.whole);
}

void NetwPredictionEngine::admit_simulated_rows(
    int64_t p_slot,
    int64_t p_recv_tick,
    bool p_whole
) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    const PropertyBindingPtr binding = state_binding_of(p_slot);
    predict::Slot *row = mutable_row_of(p_slot);
    if (handle == nullptr || host == nullptr || seated.is_null()
        || binding == nullptr || row == nullptr) {
        return;
    }
    const RID entity = seated->get_rid_handle();
    if (!p_whole && !stream_reconstructed_of(p_slot)) {
        return;
    }
    set_stream_reconstructed(p_slot, true);
    row = mutable_row_of(p_slot);
    if (row == nullptr || row->recovering.install_keys.is_empty()) {
        return;
    }
    const int64_t recv_tick = p_recv_tick;
    if (reconcile_of(p_slot) != int(NetwPrediction::RECONCILE_JOINT)
        && host->sim_fenced(entity, recv_tick)) {
        return;
    }
    predict::RecoveryRows &installing = row->recovering;
    const LocalVector<StringName> &keys = installing.install_keys;
    int64_t age = -1;
    host->sim_install_target_row(
        binding,
        recv_tick,
        keys,
        installing.install_values,
        row->config.restore == int(RestoreMode::EXTRAPOLATED)
            ? sim::Restore::EXTRAPOLATED
            : sim::Restore::EXACT,
        int64_t(row->config.max_restore_ticks),
        installing.install_target,
        age
    );
    capture_canonical(p_slot, installing.live);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const LocalVector<Variant> &target = installing.install_target;
    row->report.divergence.resize(row->wiring.count());
    double divergence = 0.0;
    for (uint32_t at = 0; at < keys.size(); ++at) {
        const int field = row->wiring.fields.index_of(keys[at]);
        double error = std::numeric_limits<double>::infinity();
        if (field >= 0 && installing.live.has(field)) {
            error = predict::value_error(
                installing.live.values[uint32_t(field)],
                target[at],
                row->wiring.angle[uint32_t(field)] != 0
            );
        }
        row->report.divergence.note(field, error);
        divergence = std::max(divergence, error);
    }
    note_reconciling(p_slot, true);
    NetwPredictStats::Book *counters = handle->fact_book();
    if (counters != nullptr) {
        counters->set_int_fact(
            NetwPredictStats::FACT_CORRECTIONS,
            counters->get_int_fact(NetwPredictStats::FACT_CORRECTIONS) + 1
        );
    }
    if (reconcile_of(p_slot) == int(NetwPrediction::RECONCILE_JOINT)) {
        predict::StateRow arrived;
        arrived.resize(row->wiring.count());
        for (uint32_t at = 0; at < keys.size(); ++at) {
            const int field = row->wiring.fields.index_of(keys[at]);
            if (field >= 0) {
                arrived.set(field, installing.install_values[at]);
            }
        }
        note_joint_basis(p_slot, recv_tick - 1, arrived, JOINT_FLOOR_STATE);
    } else if (host->sim_write_install_row(
                   entity,
                   binding,
                   keys,
                   target,
                   age,
                   installing.moved_keys,
                   installing.moved
               )) {
        if (predict::Slot *held = mutable_row_of(p_slot)) {
            port_sync_transform(held->port.resolve(sys::PREDICTION));
        }
        if (row_of(p_slot) != nullptr) {
            mark_carry_dirty(p_slot, drive_frontier(p_slot) + 1);
        }
    }
    note_reconciling(p_slot, false);
    note_verdict_reason(p_slot, predict::VERDICT_REASON_NONE);
    handle->emit_signal(
        StringName("state_evaluated"),
        recv_tick,
        int64_t(-1),
        divergence,
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
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return 0;
    }
    if (handle->get_schedule() == NetwSimulation::SCHEDULE_FRAME) {
        return replay_authored_entries(
            p_slot,
            p_ack,
            p_sample,
            p_participants,
            p_demote
        );
    }
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !row->lane_open
        || input_binding_of(p_slot) == nullptr) {
        return 0;
    }
    LocalVector<int64_t> ticks;
    row->authored_ticks(p_ack + 1, latest_input_tick_of(p_slot), ticks);
    hold_live_input(p_slot);
    const double delta = row->tick_delta;
    SpaceReplay space;
    const bool stepped = open_space_replay(p_slot, replay_basis(ticks), space);
    predict::StateRow command;
    for (const int64_t tick : ticks) {
        const predict::Slot *held = row_of(p_slot);
        if (held == nullptr) {
            break;
        }
        held->authored_row(tick, command);
        run_command(p_slot, command, delta, tick, false);
        if (stepped) {
            step_space_replay(space, delta, tick);
        }
        capture_predicted(p_slot, tick + 1);
    }
    if (stepped) {
        close_space_replay(space);
    }
    restore_live_input(p_slot);
    return int(ticks.size());
}

void NetwPredictionEngine::finish_reseed_alignment(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_ack,
    const predict::StateRow &p_payload,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return;
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    predict::StateRow seed;
    const bool projected = advanced_seed(
        p_slot,
        p_payload,
        p_ack,
        handle->get_snap_restore(),
        handle->get_max_restore_ticks(),
        handle->get_teleport_threshold(),
        handle->get_divergence_epsilon(),
        seed
    );
    row = mutable_row_of(p_slot);
    const bool seated_basis = !projected && row != nullptr
        && row->config.correction == int(CorrectionMode::REPLAY)
        && seat_restore_row(p_slot, p_ack + 1, seed);
    if (apply_restore_row(
            p_slot,
            seed,
            int(predict::Operator::RESEED_ALIGN),
            p_ack,
            p_slot,
            seated.is_valid() ? seated->get_name() : StringName(),
            handle->get_ack_age_ticks(),
            handle->get_divergence_epsilon(),
            true,
            false,
            seated_basis ? p_ack + 1 : -1
        )) {
        sync_episode(p_slot);
    }
    if (!seated_basis
        && handle->get_schedule() != NetwSimulation::SCHEDULE_FRAME) {
        if (predict::Slot *held = mutable_row_of(p_slot)) {
            held->record_state_row(p_ack + 1, p_payload);
        }
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
        handle->get_schedule() == NetwSimulation::SCHEDULE_FRAME
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
    const predict::StateRow &p_command,
    double p_delta,
    int64_t p_tick,
    bool p_fresh
) {
    if (owner_bound(p_slot)) {
        run_command(p_slot, p_command, p_delta, p_tick, p_fresh);
        return;
    }
    apply_command(p_slot, p_command);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle != nullptr && handle->get_simulate().is_valid()) {
        call_step(handle->get_simulate(), p_delta, p_tick, p_fresh);
    }
}

void NetwPredictionEngine::consume_one(int64_t p_slot, double p_delta) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr || !input_lane_open(p_slot)) {
        return;
    }
    const int64_t next = next_input_tick_of(p_slot);
    const bool has_input = has_authored_input(p_slot, next);
    predict::Slot *row = mutable_row_of(p_slot);
    const predict::ConsumeInputPlan plan = plan_consume_input(
        int(Schedule::TICK),
        has_input,
        newest_authored_input(p_slot) > next,
        row->last.any(),
        handle->get_missing_policy()
    );
    if (!plan.eligible) {
        return;
    }
    if (has_input) {
        row->authored_row(next, row->command);
    } else {
        row->command = plan.use_last ? row->last : row->stall;
    }
    if (!plan.missing) {
        row->last = row->command;
    }
    record_drive_hashed(
        p_slot,
        next,
        next,
        int(plan.kind),
        command_hash_row(p_slot, row->command),
        next,
        false,
        false,
        false
    );
    row = mutable_row_of(p_slot);
    if (plan.run && row != nullptr) {
        run_input(p_slot, row->command, p_delta, next, true);
    }
    NetwPredictStats::Book *counters = handle->fact_book();
    if (plan.missing) {
        if (counters != nullptr) {
            counters->set_int_fact(
                NetwPredictStats::FACT_MISSING,
                counters->get_int_fact(NetwPredictStats::FACT_MISSING) + 1
            );
        }
    } else {
        if (counters != nullptr) {
            counters->set_int_fact(
                NetwPredictStats::FACT_CONSUMED,
                counters->get_int_fact(NetwPredictStats::FACT_CONSUMED) + 1
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
    if (row == nullptr || !row->lane_open) {
        return 0;
    }
    LocalVector<int64_t> ticks;
    row->authored_ticks(p_from, p_to, ticks);
    note_replay_depth(p_slot, int(ticks.size()));
    hold_live_input(p_slot);
    const double delta = row->tick_delta;
    SpaceReplay space;
    const bool stepped = open_space_replay(p_slot, replay_basis(ticks), space);
    predict::StateRow command;
    for (const int64_t tick : ticks) {
        const predict::Slot *held = row_of(p_slot);
        if (held == nullptr) {
            break;
        }
        held->authored_row(tick, command);
        run_command(p_slot, command, delta, tick, false);
        if (stepped) {
            step_space_replay(space, delta, tick);
        }
        capture_into_book(p_slot, tick + 1);
        const Dictionary solve
            = seal_transition(p_slot, tick, p_sample, p_participants);
        if (!solve.is_empty() && p_demote.is_valid()) {
            Array charged;
            charged.push_back(tick);
            charged.push_back(solve);
            p_demote.callv(charged);
        }
    }
    if (stepped) {
        close_space_replay(space);
    }
    restore_live_input(p_slot);
    return int(ticks.size());
}

bool NetwPredictionEngine::run_recovery_ladder(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_ack_label,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_payload,
    int p_meter,
    int p_domain,
    int p_attribution,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote,
    const Callable &p_seam
) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return false;
    }
    const int window = open_recovery_rows(
        p_slot,
        p_ack,
        p_predicted,
        p_payload,
        p_meter,
        p_domain
    );
    if (window == RECOVERY_DISSIPATED) {
        return false;
    }
    bool skip = false;
    bool seated = false;
    if (window != RECOVERY_TRANSPORTED) {
        const PlannedRows planned = plan_recovery_rows(
            p_slot,
            p_ack,
            p_ack_label,
            p_predicted,
            p_payload,
            escalation_pending(p_slot),
            p_domain,
            p_attribution,
            p_seam
        );
        predict::Slot *row = mutable_row_of(p_slot);
        if (row == nullptr || planned.pool == nullptr) {
            return false;
        }
        const bool pool_planned = !planned.answered;
        const predict::WritePlan &plan
            = pool_planned ? *planned.pool : row->recovering.answer;
        if (planned.pool->escalated) {
            if (!pool_planned) {
                record_episode_escalation(
                    p_slot,
                    trigger_shape(p_slot, handle->get_divergence_epsilon())
                );
                sync_episode(p_slot);
            }
            set_cooldown_until_tick(
                p_slot,
                latest_input_tick_of(p_slot)
                    + handle->get_collision_cooldown_ticks()
            );
        }
        skip = plan.skip;
        seated = apply_plan_rows(p_slot, plan, p_ack, pool_planned);
    }
    close_recovery_rows(
        p_slot,
        p_ack,
        p_payload,
        skip,
        p_attribution,
        p_sample,
        p_participants,
        p_demote,
        seated
    );
    return true;
}

bool NetwPredictionEngine::ask_recover_seam(
    int64_t p_slot,
    const Dictionary &p_context,
    const Callable &p_seam
) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr || !p_seam.is_valid()) {
        return false;
    }
    Array asked;
    asked.push_back(recovery_carried_of(p_slot));
    asked.push_back(handle->resolved_recovery_policy());
    asked.push_back(handle->get_snap_restore());
    asked.push_back(recovery_projection_of(p_slot));
    asked.push_back(recovery_before_of(p_slot));
    asked.push_back(recovery_tier_errors_of(p_slot));
    asked.push_back(p_context);
    asked.push_back(tick_delta_of(p_slot));
    return plan_answered(
        Ref<NetwPredictRecovery>(p_seam.callv(asked)),
        p_slot
    );
}

bool NetwPredictionEngine::plan_answered(
    const Ref<NetwPredictRecovery> &p_answered,
    int64_t p_slot
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (p_answered.is_valid() && row != nullptr) {
        predict::WritePlan &answer = row->recovering.answer;
        predict::read_payload(
            row->wiring,
            p_answered->restore(),
            answer.restore
        );
        predict::read_payload(row->wiring, p_answered->write(), answer.write);
        answer.teleport = p_answered->teleport();
        answer.skip = p_answered->skip();
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

predict::Judged NetwPredictionEngine::judge_state(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_domain,
    int64_t p_row_flags,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_payload,
    const Callable &p_seam
) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    const double epsilon
        = handle != nullptr ? handle->get_divergence_epsilon() : 0.0;
    const predict::StateVerdict *verdict = compare_rows(
        p_slot,
        p_ack,
        p_predicted,
        p_payload,
        int(p_domain),
        epsilon
    );
    predict::Slot *row = mutable_row_of(p_slot);
    predict::Judged out;
    if (verdict != nullptr && !p_seam.is_valid()) {
        note_errors(*row, row->report.divergence, verdict->all_field_errors);
        out.divergence = verdict->divergence;
        out.corrected = verdict->corrected;
        out.settled = verdict->settled;
        out.meter = verdict->meter;
    } else {
        const int exact_verdict = (p_row_flags & predict::ROW_ACKED) == 0
            ? int(NetwPrediction::EXACT_VERDICT_UNJUDGED)
            : ((p_row_flags & predict::ROW_MATCHED) != 0
                   ? int(NetwPrediction::EXACT_VERDICT_EQUAL)
                   : int(NetwPrediction::EXACT_VERDICT_UNEQUAL));
        Judgement judged;
        bool answered = false;
        if (p_seam.is_valid() && row != nullptr) {
            Dictionary seam_divergence;
            Array asked;
            asked.push_back(p_domain);
            asked.push_back(exact_verdict);
            asked.push_back(predict::payload_of(row->wiring, p_predicted));
            asked.push_back(predict::payload_of(row->wiring, p_payload));
            asked.push_back(seam_divergence);
            answered = judgement_answered(
                Ref<NetwPredictJudgement>(p_seam.callv(asked)),
                p_slot,
                judged
            );
            if (answered) {
                note_divergence(p_slot, seam_divergence);
                out.meter
                    = meter_of(p_slot, seam_divergence, int(p_domain), epsilon);
            }
        }
        if (!answered && row != nullptr) {
            bool diverged = true;
            fill_correction_tolerances(
                *row,
                row->config.epsilon,
                row->correction_tolerances
            );
            judged.divergence = predict::judge_rows(
                row->wiring,
                p_predicted,
                p_payload,
                row->correction_tolerances,
                row->judged_errors,
                diverged
            );
            const bool exact = p_domain == int64_t(predict::Domain::IN_DOMAIN)
                && exact_verdict != int(NetwPrediction::EXACT_VERDICT_UNJUDGED);
            judged.corrected = exact
                ? exact_verdict == int(NetwPrediction::EXACT_VERDICT_UNEQUAL)
                : diverged;
            note_errors(*row, row->report.divergence, row->judged_errors);
            fill_meter_tolerances(
                *row,
                int(p_domain),
                epsilon,
                row->meter_tolerances
            );
            out.meter
                = predict::measure(row->judged_errors, row->meter_tolerances);
        } else if (!answered) {
            judged.divergence = std::numeric_limits<double>::infinity();
            judged.corrected = true;
        }
        out.divergence = judged.divergence;
        out.corrected = judged.corrected;
        out.settled = p_domain == int64_t(predict::Domain::OUT_OF_DOMAIN)
            || exact_verdict != int(NetwPrediction::EXACT_VERDICT_UNJUDGED);
    }
    if (wants_report(p_slot, EventPlane::PREDICT_EVALUATE)) {
        NETW_NOTE_SCRIPT_DICTIONARY();
        Dictionary detail;
        detail[StringName("transition")] = p_ack;
        detail[StringName("divergence")] = out.divergence;
        detail[StringName("corrected")] = out.corrected;
        report_predict(
            p_slot,
            EventPlane::PREDICT_EVALUATE,
            detail,
            Dictionary()
        );
    }
    if (out.corrected && out.meter == 0) {
        out.meter = 1;
    } else if (!out.corrected) {
        out.meter = 0;
    }
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
    const Callable &p_demote,
    bool p_seated
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    predict::StateRow payload;
    predict::read_payload(row->wiring, p_payload, payload);
    predict::read_payload(row->wiring, p_before, row->recovering.before);
    predict::read_payload(
        row->wiring,
        p_correction_write,
        row->recovering.write
    );
    close_recovery_rows(
        p_slot,
        p_ack,
        payload,
        p_plan.skip,
        p_attribution,
        p_sample,
        p_participants,
        p_demote,
        p_seated
    );
}

void NetwPredictionEngine::close_recovery_rows(
    int64_t p_slot,
    int64_t p_ack,
    const predict::StateRow &p_payload,
    bool p_skip,
    int64_t p_attribution,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants,
    const Callable &p_demote,
    bool p_seated
) {
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return;
    }
    const int schedule = handle->get_schedule();
    const bool frame_scheduled
        = schedule == NetwSimulation::SCHEDULE_FRAME;
    if (schedule == NetwSimulation::SCHEDULE_TICK) {
        predict::Slot *row = mutable_row_of(p_slot);
        if (!p_seated && row != nullptr) {
            row->record_state_row(p_ack + 1, p_payload);
        }
        mark_carry_dirty(p_slot, p_ack + 1);
    }
    const bool replays
        = !p_skip && correction_of(p_slot) == int(CorrectionMode::REPLAY);
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
    announce_recovered_rows(p_slot, p_ack, p_attribution);
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
    hold_live_input(p_slot);
    const double delta = row->tick_delta;
    predict::StateRow command;
    for (uint32_t at = 0; at < entries.size(); ++at) {
        const predict::ReplayEntry &step = entries[at];
        const predict::Slot *held = row_of(p_slot);
        if (held == nullptr) {
            break;
        }
        held->authored_row(step.input_key, command);
        run_command(p_slot, command, delta, step.label, false);
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
    restore_live_input(p_slot);
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
    if (owner_bound(p_slot)) {
        capture_into_book(p_slot, p_index + 1);
    } else {
        record_predicted(p_slot, p_index + 1, predict::StateRow());
    }
    return seal_transition(p_slot, p_index, p_sample, p_participants);
}

Dictionary NetwPredictionEngine::seal_transition(
    int64_t p_slot,
    int64_t p_transition,
    const Dictionary &p_sample,
    const PackedStringArray &p_participants
) {
    NETW_ZONE_LEAF(
        "Predict seal transition",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return Dictionary();
    }
    if (p_transition >= 0 && joint_provenance_at(p_slot, p_transition) < 0) {
        predict::StateRow coast;
        predict::zero_state_row(row->input_codec, coast);
        joint_record(
            p_slot,
            p_transition,
            predict::StateRow(),
            coast,
            false,
            false,
            false
        );
    }
    const int seat = journal_slot_of(p_slot, p_transition);
    if (seat < 0
        || (journal_flags_at(p_slot, seat) & predict::ROW_CLOSED) != 0) {
        return Dictionary();
    }
    const int32_t post_fp = book_fingerprint(p_slot, p_transition + 1);
    const predict::FamilyFingerprints families
        = book_families(p_slot, p_transition + 1);
    if (p_sample.is_empty()) {
        roster_clear(p_slot, ROSTER_REALIZED_CONTACT);
        record_evidence(
            p_slot,
            p_transition,
            declared_epoch_of(p_slot),
            sensor_samples(p_slot),
            open_topology_of(p_slot),
            PackedStringArray(),
            PackedInt32Array(),
            PackedInt32Array(),
            PackedByteArray(),
            false
        );
        close_drive(
            p_slot,
            p_transition,
            post_fp,
            families.pose,
            families.momentum,
            families.controller
        );
        predict::Slot *held = mutable_row_of(p_slot);
        if (held != nullptr) {
            held->witness_details.forget(
                p_transition,
                predict::TAPE_HISTORY_LIMIT
            );
        }
        return Dictionary();
    }
    const Dictionary solve = solve_evidence(
        p_slot,
        open_topology_of(p_slot),
        p_transition,
        p_sample,
        p_participants
    );
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
        outside.append(bool(contact[StringName("outside_boundary")]) ? 1 : 0);
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
        families.pose,
        families.momentum,
        families.controller
    );
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

void NetwPredictionEngine::ledger_note_rows(
    int64_t p_slot,
    const predict::StateRow &p_deltas
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const int64_t frontier = drive_frontier(p_slot);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const int count
        = std::min(row->wiring.count(), int(p_deltas.values.size()));
    for (int at = 0; at < count; ++at) {
        if (!p_deltas.has(at) || row->wiring.causal[uint32_t(at)] == 0) {
            continue;
        }
        row->recovery_ledger.bump_repaired(at);
        if (row->report.divergence.has(at)) {
            row->contraction_ledger.arm(
                at,
                row->report.divergence.at(at, INFINITY),
                frontier
            );
        }
    }
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
    bool p_pool_planned,
    int64_t p_entry
) {
    if (p_basis < -1 || p_operator < int(predict::Operator::NONE)
        || p_operator > int(predict::Operator::JOINT_REBASE)) {
        return false;
    }
    if (row_of(p_slot) == nullptr) {
        return false;
    }
    if (p_entry < 0 || !restore_predicted(p_slot, p_entry)) {
        if (!apply_state(p_slot, p_payload)) {
            const PropertyBindingPtr binding = state_binding_of(p_slot);
            if (binding != nullptr) {
                binding->apply_payload(p_payload);
            }
        }
        if (predict::Slot *row = mutable_row_of(p_slot)) {
            port_sync_transform(row->port.resolve(sys::PREDICTION));
        }
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

bool NetwPredictionEngine::apply_restore_row(
    int64_t p_slot,
    const predict::StateRow &p_restore,
    int p_operator,
    int64_t p_basis,
    int64_t p_provenance_slot,
    const StringName &p_target,
    int p_ack_age_ticks,
    double p_divergence_epsilon,
    bool p_evidence_free,
    bool p_pool_planned,
    int64_t p_entry
) {
    if (p_basis < -1 || p_operator < int(predict::Operator::NONE)
        || p_operator > int(predict::Operator::JOINT_REBASE)) {
        return false;
    }
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    if (p_entry < 0 || !restore_predicted(p_slot, p_entry)) {
        row = mutable_row_of(p_slot);
        if (row != nullptr
            && !push_row(
                *row,
                row->wiring.codec,
                row->owner_has_state,
                p_restore
            )) {
            const PropertyBindingPtr binding = state_binding_of(p_slot);
            if (binding != nullptr) {
                binding->apply_payload(
                    predict::payload_of(row->wiring, p_restore)
                );
            }
        }
        if (predict::Slot *held = mutable_row_of(p_slot)) {
            port_sync_transform(held->port.resolve(sys::PREDICTION));
        }
    }
    record_restore_row(
        p_slot,
        p_restore,
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

bool NetwPredictionEngine::write_deltas_rows(
    int64_t p_slot,
    const predict::StateRow &p_before,
    const predict::StateRow &p_staged,
    predict::StateRow &r_deltas
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    const int count = row->wiring.count();
    predict::StateRow &after = row->recovering.live;
    if (!pull_state(*row)) {
        after.resize(count);
    } else {
        after = row->pulled;
    }
    const int staged = std::min(count, int(p_staged.values.size()));
    for (int at = 0; at < staged; ++at) {
        if (p_staged.has(at)) {
            after.set(at, p_staged.values[uint32_t(at)]);
        }
    }
    r_deltas.resize(count);
    bool any = false;
    const int before = std::min(count, int(p_before.values.size()));
    for (int at = 0; at < count; ++at) {
        r_deltas.values[uint32_t(at)] = Variant();
        if (at >= before || !after.has(at) || !p_before.has(at)) {
            continue;
        }
        const Variant delta = prediction_core::pose_delta(
            after.values[uint32_t(at)],
            p_before.values[uint32_t(at)],
            row->wiring.angle[uint32_t(at)] != 0
        );
        if (delta.get_type() == Variant::NIL || delta_negligible(delta)) {
            continue;
        }
        r_deltas.set(at, delta);
        any = true;
    }
    return any;
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
        = p_recovery_policy == int(NetwPrediction::RECOVERY_POLICY_OBSERVE);
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
    NETW_ZONE_LEAF(
        "Predict solve evidence",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
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
        contact_classes.append(int(NetwPrediction::CONTACT_CLASS_NONE));
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
    if (!p_participants.has(participant_key(entity))) {
        return true;
    }
    const Ref<NetwPrediction> declared = entity->prediction_if_minted();
    return declared.is_null()
        || declared->get_sim_mode() == int(NetwPrediction::SIM_MODE_DISPLAY);
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
        "Prediction carried a value for an entity it no longer predicts "
        "(slot %d).",
        p_slot
    );
    NETW_ERR_COND_V(
        row->carry_rules.is_empty(),
        CARRY_DECLINED,
        sys::PREDICTION,
        "Prediction tried to carry a value on an entity with no "
        "NetwPropertyConfig.carry_along or carry_step property."
    );
    const int field = row->wiring.fields.index_of(p_field);
    NETW_ERR_COND_V(
        field < 0,
        CARRY_DECLINED,
        sys::PREDICTION,
        "Prediction tried to carry '%s', which is not a carried property.",
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
        "Prediction declined a carried value for an entity it no longer "
        "predicts (slot %d).",
        p_slot
    );
    const int field = row->wiring.fields.index_of(p_field);
    NETW_ERR_COND_V(
        field < 0,
        CARRY_DECLINED,
        sys::PREDICTION,
        "Prediction declined to carry '%s', which is not a carried "
        "property.",
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

int32_t NetwPredictionEngine::book_fingerprint(
    int64_t p_slot,
    int64_t p_key
) const {
    NETW_ZONE_LEAF(
        "Predict book fingerprint",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    bool any = false;
    return row->book.fingerprint(
        row->wiring.codec,
        p_key,
        predict::causal_scope(row->wiring),
        any
    );
}

predict::FamilyFingerprints NetwPredictionEngine::book_families(
    int64_t p_slot,
    int64_t p_key
) const {
    predict::FamilyFingerprints out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const predict::StateFamily families[] = {
        predict::StateFamily::POSE,
        predict::StateFamily::MOMENTUM,
        predict::StateFamily::CONTROLLER,
    };
    int32_t *into[] = { &out.pose, &out.momentum, &out.controller };
    for (int at = 0; at < 3; ++at) {
        bool any = false;
        const int32_t hash = row->book.fingerprint(
            row->wiring.codec,
            p_key,
            predict::family_scope(row->wiring, families[at]),
            any
        );
        *into[at] = any ? hash : 0;
    }
    return out;
}

int32_t NetwPredictionEngine::live_fingerprint(
    int64_t p_slot,
    predict::FamilyFingerprints *r_families
) {
    NETW_ZONE_LEAF(
        "Predict live fingerprint",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    static const predict::StateRow NONE;
    const predict::StateRow &live = pull_state(*row) ? row->pulled : NONE;
    bool any = false;
    const int32_t out = predict::fingerprint_row(
        row->wiring.codec,
        live,
        predict::causal_scope(row->wiring),
        row->fingerprint_scratch,
        any
    );
    if (r_families == nullptr) {
        return out;
    }
    const predict::StateFamily families[] = {
        predict::StateFamily::POSE,
        predict::StateFamily::MOMENTUM,
        predict::StateFamily::CONTROLLER,
    };
    int32_t *into[] = {
        &r_families->pose,
        &r_families->momentum,
        &r_families->controller,
    };
    for (int at = 0; at < 3; ++at) {
        const int32_t hash = predict::fingerprint_row(
            row->wiring.codec,
            live,
            predict::family_scope(row->wiring, families[at]),
            row->fingerprint_scratch,
            any
        );
        *into[at] = any ? hash : 0;
    }
    return out;
}

int32_t NetwPredictionEngine::payload_fingerprint(
    int64_t p_slot,
    const Dictionary &p_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return 0;
    }
    predict::StateRow payload;
    predict::read_payload(row->wiring, p_payload, payload);
    bool any = false;
    return predict::fingerprint_row(
        row->wiring.codec,
        payload,
        ~uint64_t(0),
        row->fingerprint_scratch,
        any
    );
}

Variant NetwPredictionEngine::call_carry(
    int64_t p_slot,
    const Callable &p_rule,
    const Variant &p_value,
    const predict::ReplayEntry &p_entry,
    int64_t p_state_key
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return Variant();
    }
    Ref<NetwPredictCarryContext> context;
    context.instantiate();
    predict::StateRow read;
    context->state_at
        = row->book.read_row(row->wiring.codec, p_state_key, read)
        ? predict::payload_of(row->wiring.codec, read)
        : Dictionary();
    row->authored_row(p_entry.input_key, read);
    context->input_at = predict::payload_of(row->input_codec, read);
    context->authored_label = p_entry.label;
    context->step_delta = row->tick_delta;

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
    predict::StateRow from;
    predict::StateRow reached;
    for (uint32_t at = 0; at < p_entries.size(); ++at) {
        const predict::Slot *row = row_of(p_slot);
        if (row == nullptr) {
            break;
        }
        const int64_t index = p_entries[at].index;
        if (!row->carry_judgeable(index)) {
            continue;
        }
        if (!row->book.read_row(row->wiring.codec, index, from)
            || !row->book.read_row(row->wiring.codec, index + 1, reached)
            || !from.has(p_field_slot) || !reached.has(p_field_slot)) {
            continue;
        }
        const double tolerance
            = row->wiring.epsilon[uint32_t(p_field_slot)] >= 0.0
            ? row->wiring.epsilon[uint32_t(p_field_slot)]
            : p_divergence_epsilon;
        const Variant &target = reached.values[uint32_t(p_field_slot)];
        const Variant stepped = call_carry(
            p_slot,
            p_rule,
            from.values[uint32_t(p_field_slot)],
            p_entries[at],
            index
        );
        if (stepped.get_type() != target.get_type()) {
            r_attempt.probe.faithful = false;
            return;
        }
        const double residual = predict::value_error(stepped, target, p_angle);
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
        const int64_t index = p_entries[at].index;
        if (!row->book.carries_fields(index)) {
            r_attempt.evidence = false;
            return;
        }
        const Variant stepped
            = call_carry(p_slot, p_rule, value, p_entries[at], index);
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
    NETW_ZONE_LEAF(
        "Predict attempt carry",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
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
    const int32_t before = guarded ? live_fingerprint(p_slot) : 0;

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
    const int32_t after = guarded ? live_fingerprint(p_slot) : 0;
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
        "Prediction recorded a tick with domain %d, which is not a "
        "NetwPrediction.Domain value.",
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
        "Prediction marked a gap in the history of an entity it no longer "
        "predicts (slot %d).",
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
        "Prediction recorded a tick with operator %d, which is not a "
        "NetwPrediction.Operator value.",
        p_operator
    );
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Prediction recorded where a tick came from for an entity it no "
        "longer predicts (slot %d).",
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
        "Prediction blamed a misprediction on an entity it no longer "
        "predicts (slot %d).",
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
        "Prediction recorded an unknown kind of differing state, %d.",
        p_family
    );
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Prediction recorded which state differed for an entity it no longer "
        "predicts (slot %d).",
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
        "Prediction got physics results for an entity it no longer predicts "
        "(slot %d).",
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
        "Prediction checked the contacts of an entity it no longer predicts "
        "(slot %d).",
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
    counts[ACK_RUN_AGE] = drive_stat(p_slot, STAT_ACK_AGE_TICKS);
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

const predict::StateVerdict *NetwPredictionEngine::compare_rows(
    int64_t p_slot,
    int64_t p_transition,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_authority,
    int p_domain,
    double p_divergence_epsilon
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !journal_has(p_slot, p_transition)) {
        return nullptr;
    }
    fill_correction_tolerances(
        *row,
        p_divergence_epsilon,
        row->correction_tolerances
    );
    fill_meter_tolerances(
        *row,
        p_domain,
        p_divergence_epsilon,
        row->meter_tolerances
    );
    return &row->compare(
        p_transition,
        p_transition,
        p_predicted,
        p_authority,
        row->correction_tolerances,
        row->meter_tolerances,
        p_divergence_epsilon,
        row->latches.stream_reconstructed,
        row->lane_cursor.ack_domain_confirmed
    );
}

const predict::StateRow &NetwPredictionEngine::compared_row(
    int64_t p_slot
) const {
    static const predict::StateRow NONE;
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->compared : NONE;
}

const predict::StateRow &NetwPredictionEngine::received_row(
    int64_t p_slot
) const {
    static const predict::StateRow NONE;
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->received : NONE;
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
    bool p_suppressed,
    const RecoveryPlan *p_answer
) {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return predict::WritePlan();
    }
    predict::StateRow predicted;
    predict::StateRow authority;
    predict::StateRow current;
    predict::read_payload(row->wiring, p_predicted, predicted);
    predict::read_payload(row->wiring, p_authority, authority);
    predict::read_payload(row->wiring, p_current, current);
    predict::RecoveryRequest request = recovery_request(
        row->wiring.count(),
        predicted,
        authority,
        current,
        tolerance_columns_of(p_slot, p_tier_errors),
        p_basis,
        p_current_label,
        p_policy,
        p_divergence_epsilon,
        p_teleport_threshold,
        p_max_restore_ticks,
        p_ack_age_ticks,
        p_collision_cooldown_ticks,
        row->tick_delta,
        p_domain,
        p_attribution,
        out_of_domain_at(p_slot, p_current_label),
        p_suppressed,
        !has_pose_fields(p_slot)
    );
    if (p_answer != nullptr) {
        request.answered = true;
        request.answered_teleport = p_answer->teleport;
        request.answered_skip = p_answer->skip;
    }
    return recover(p_slot, request);
}

const predict::WritePlan *NetwPredictionEngine::recover_rows(
    int64_t p_slot,
    int64_t p_basis,
    int64_t p_current_label,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_authority,
    const predict::StateRow &p_current,
    const LocalVector<double> &p_tier_errors,
    int p_policy,
    double p_divergence_epsilon,
    double p_teleport_threshold,
    int p_max_restore_ticks,
    int p_ack_age_ticks,
    int p_collision_cooldown_ticks,
    int p_domain,
    int p_attribution,
    bool p_suppressed,
    const predict::WritePlan *p_answer
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return nullptr;
    }
    const bool contact = out_of_domain_at(p_slot, p_current_label);
    const bool unmeasured = !has_pose_fields(p_slot);
    predict::RecoveryRequest &request = row->recovering.request;
    if (!recovery_request_in_range(
            p_policy,
            p_domain,
            p_attribution,
            row->tick_delta
        )) {
        request = predict::RecoveryRequest();
    } else {
        const int width = row->wiring.count();
        request.predicted = p_predicted;
        request.authority = p_authority;
        request.current = p_current;
        request.field_errors.resize(uint32_t(width));
        for (int at = 0; at < width; ++at) {
            request.field_errors[uint32_t(at)]
                = at < int(p_tier_errors.size()) ? p_tier_errors[uint32_t(at)]
                                                 : -1.0;
        }
        request.basis = p_basis;
        request.current_label = p_current_label;
        request.policy = p_policy;
        request.fallback_epsilon = p_divergence_epsilon;
        request.fallback_teleport = p_teleport_threshold;
        request.max_restore_ticks = p_max_restore_ticks;
        request.ack_age_ticks = p_ack_age_ticks;
        request.collision_cooldown_ticks = p_collision_cooldown_ticks;
        request.tick_delta = row->tick_delta;
        request.domain = predict::Domain(p_domain);
        request.attribution = predict::Attribution(p_attribution);
        request.contact_window = contact;
        request.suppressed = p_suppressed;
        request.pose_unmeasured = unmeasured;
        request.answered = false;
        request.answered_teleport = false;
        request.answered_skip = true;
    }
    if (p_answer != nullptr) {
        request.answered = true;
        request.answered_teleport = p_answer->teleport;
        request.answered_skip = p_answer->skip;
    }
    return &row->recover(request);
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
    const predict::StateRow &p_payload,
    bool p_whole
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return predict::WritePlan();
    }
    return row->quarantine_state(p_tick, p_basis, p_payload, p_whole);
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
    const predict::StateRow &p_payload,
    int64_t p_ignore_through
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return predict::WritePlan();
    }
    return row->align_reseed(p_transition, p_payload, p_ignore_through);
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

Ref<NetwEntity> NetwPredictionEngine::owner_entity(int64_t p_slot) const {
    const HashMap<int64_t, Ref<RefCounted>>::ConstIterator seated
        = entity_by_slot.find(p_slot);
    return seated != entity_by_slot.end()
        ? Ref<NetwEntity>(Object::cast_to<NetwEntity>(seated->value.ptr()))
        : Ref<NetwEntity>();
}

TypedArray<NetwEntity> NetwPredictionEngine::live_participants(int64_t p_slot) {
    const TypedArray<NetwEntity> seated
        = roster_list(p_slot, ROSTER_ISLAND_MEMBERS);
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    if (!seated.is_empty() || host == nullptr || owner.is_null()) {
        return seated;
    }
    return host->sim_named(owner->get_rid_handle());
}

PackedStringArray NetwPredictionEngine::live_participant_ids(int64_t p_slot) {
    NETW_ZONE_LEAF(
        "Predict live participant ids",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    const TypedArray<NetwEntity> seated = live_participants(p_slot);
    PackedStringArray ids;
    ids.resize(seated.size());
    String *names = ids.ptrw();
    for (int at = 0; at < seated.size(); ++at) {
        names[at] = participant_key(Ref<NetwEntity>(seated[at]));
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
        ids.push_back(participant_key(Ref<NetwEntity>(p_roster[at])));
    }
    ids.sort();
    return ids;
}

} // namespace

void NetwPredictionEngine::publish_island_roster(int64_t p_slot) {
    Object *held = handle_of(p_slot);
    NetwPrediction *handle = Object::cast_to<NetwPrediction>(held);
    if (handle == nullptr) {
        return;
    }
    NetwPredictStats::Book *counters = handle->fact_book();
    if (counters == nullptr) {
        return;
    }
    counters->set_names_fact(
        NetwPredictStats::FACT_ISLAND_MEMBERS,
        sorted_ids(roster_list(p_slot, ROSTER_ISLAND_MEMBERS))
    );
    counters->set_names_fact(
        NetwPredictStats::FACT_SIMULATED_MEMBERS,
        sorted_ids(roster_list(p_slot, ROSTER_SIMULATED))
    );
}

void NetwPredictionEngine::refresh_selection(
    int64_t p_slot,
    bool p_joint,
    const Callable &p_admit_reconcile
) {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    const RID subject = owner.is_valid() ? owner->get_rid_handle() : RID();
    if (host == nullptr || !host->sim_chooses(subject)) {
        clear_island_promotions(p_slot);
        roster_clear(p_slot, ROSTER_ISLAND_MEMBERS);
        publish_topology_roster(p_slot);
        return;
    }
    const TypedArray<NetwEntity> members = host->sim_candidates(owner);
    TypedArray<NetwEntity> admitted;
    for (int at = 0; at < members.size(); ++at) {
        const Ref<NetwEntity> member = members[at];
        const int64_t seat = slot_of(member);
        if (seat < 0 || joint_admits(p_slot, seat)) {
            admitted.push_back(member);
            continue;
        }
        NETW_ERROR_ONCE(
            sys::PREDICTION,
            "'%s' cannot replay a tick, so its NetwPrediction.RECONCILE_JOINT "
            "group leaves it out. Give it a NetwSimulation.step on "
            "SCHEDULE_TICK, or use SCHEDULE_STEPPED with a stepper.",
            String(member->get_name()).utf8().get_data()
        );
    }
    const TypedArray<NetwEntity> promoted = host->sim_select(
        owner,
        admitted,
        roster_list(p_slot, ROSTER_REALIZED_CONTACT),
        drive_frontier(p_slot)
    );
    if (p_joint) {
        const sim::Row *standing = host->sim_row_of(subject);
        sim::Selection joint;
        if (standing != nullptr) {
            joint.owner_order_key = standing->selection.owner_order_key;
            for (const sim::Selected &member : standing->selection.members) {
                const int64_t seat = slot_of(
                    host->entity_get_view(host->sim_entity_of_key(member.key))
                );
                if (seat < 0) {
                    continue;
                }
                sim::Selected held = member;
                held.key = seat;
                joint.members.push_back(held);
            }
        }
        joint_adopt(p_slot, joint);
        TypedArray<NetwEntity> joined;
        for (int at = 0; at < promoted.size(); ++at) {
            if (slot_of(Ref<NetwEntity>(promoted[at])) >= 0) {
                joined.push_back(promoted[at]);
            }
        }
        if (apply_island_promotions(p_slot, joined)) {
            publish_island_roster(p_slot);
        }
    } else {
        clear_joint_roster(p_slot);
    }

    const bool changed
        = !same_roster(members, roster_list(p_slot, ROSTER_ISLAND_MEMBERS));
    if (island_roster_seeded_of(p_slot) && changed) {
        NetwPrediction *handle
            = Object::cast_to<NetwPrediction>(handle_of(p_slot));
        open_out_of_domain_window(
            p_slot,
            latest_input_tick_of(p_slot),
            handle != nullptr ? handle->get_collision_cooldown_ticks() : 0
        );
    }
    set_island_roster_seeded(p_slot, true);
    roster_assign(p_slot, ROSTER_ISLAND_MEMBERS, members);
    publish_topology_roster(p_slot);
    if (p_admit_reconcile.is_valid()) {
        p_admit_reconcile.call();
    }
    if (changed) {
        publish_island_roster(p_slot);
    }
}

void NetwPredictionEngine::clear_island_promotions(int64_t p_slot) {
    clear_joint_roster(p_slot);
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> owner = owner_entity(p_slot);
    if (host != nullptr && owner.is_valid()) {
        host->sim_drop_selections(owner->get_rid_handle());
    }
}

void NetwPredictionEngine::clear_joint_roster(int64_t p_slot) {
    const int64_t frontier = drive_frontier(p_slot);
    const TypedArray<NetwEntity> simulated
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < simulated.size(); ++at) {
        close_tenure(p_slot, simulated[at], frontier);
    }
    roster_clear(p_slot, ROSTER_SIMULATED);
    if (predict::Slot *row = mutable_row_of(p_slot)) {
        row->joint_roster.members.clear();
    }
}

bool NetwPredictionEngine::apply_island_promotions(
    int64_t p_slot,
    const TypedArray<NetwEntity> &p_promoted
) {
    const int64_t frontier = drive_frontier(p_slot);
    const TypedArray<NetwEntity> standing
        = roster_list(p_slot, ROSTER_SIMULATED);
    for (int at = 0; at < standing.size(); ++at) {
        const Ref<NetwEntity> member = standing[at];
        if (!p_promoted.has(member)) {
            close_tenure(p_slot, member, frontier);
        }
    }
    for (int at = 0; at < p_promoted.size(); ++at) {
        const Ref<NetwEntity> member = p_promoted[at];
        if (!standing.has(member)) {
            open_tenure(p_slot, member, frontier + 1);
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

void NetwPredictionEngine::publish_topology_roster(int64_t p_slot) {
    set_island_participants(p_slot, live_participant_ids(p_slot));
}

bool NetwPredictionEngine::contact_is_equivalent(
    int64_t p_slot,
    bool p_has_witness
) {
    const TypedArray<NetwEntity> seated = live_participants(p_slot);
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
        const Ref<NetwPrediction> handle
            = participant->prediction_if_minted();
        if (handle.is_null()
            || handle->get_sim_mode() == NetwPrediction::SIM_MODE_DISPLAY) {
            return false;
        }
    }
    return true;
}

void NetwPredictionEngine::notify_contact(
    int64_t p_slot,
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
        && live_participants(p_slot).is_empty()) {
        row->latches.island_gap_reported = true;
        const Ref<NetwEntity> seated = owner_entity(p_slot);
        NETW_WARN(
            sys::PREDICTION,
            "'%s' touched a body while it selects no entities, so the contact "
            "cannot be predicted. Select what it touches with "
            "NetwSimulation.simulate_within or simulate.",
            String(seated.is_valid() ? seated->get_name() : StringName())
                .utf8()
                .get_data()
        );
    }
    if (!contact_is_equivalent(p_slot, p_has_witness)) {
        open_out_of_domain_window(p_slot, authored, p_cooldown_ticks);
    }
}

void NetwPredictionEngine::joint_adopt(
    int64_t p_slot,
    const sim::Selection &p_selection
) {
    predict::Slot *owner = mutable_row_of(p_slot);
    if (owner == nullptr) {
        return;
    }
    owner->joint_roster = p_selection;
    for (const sim::Selected &selected : owner->joint_roster.members) {
        if (predict::Slot *member = mutable_row_of(selected.key)) {
            member->joint.tenure = selected.tenure;
        }
    }
    NETW_TRACE(
        sys::PREDICTION,
        "Group %d took %d members (%d promoted, %d lingering).",
        p_slot,
        owner->joint_roster.present_count(),
        owner->joint_roster.promoted_count(),
        owner->joint_roster.lingering_count()
    );
}

bool NetwPredictionEngine::joint_admits(
    int64_t p_slot,
    int64_t p_member
) const {
    const predict::Slot *owner = row_of(p_slot);
    const predict::Slot *member = row_of(p_member);
    if (owner == nullptr || member == nullptr || p_member == p_slot) {
        return false;
    }
    return owner->config.island != ISLAND_JOINT
        || rerunnable(member->config.schedule);
}

int NetwPredictionEngine::joint_member_count(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->joint_roster.present_count() : 0;
}

bool NetwPredictionEngine::joint_promoted(
    int64_t p_slot,
    int64_t p_member
) const {
    const predict::Slot *row = row_of(p_slot);
    const sim::Selected *member
        = row != nullptr ? row->joint_roster.member(p_member) : nullptr;
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
    const predict::StateRow &p_state,
    const predict::StateRow &p_command,
    bool p_authored,
    bool p_relayed,
    bool p_predictor_valid
) {
    NETW_ZONE_LEAF(
        "Predict joint record",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Prediction recorded a group tick for an entity it no longer "
        "predicts."
    );
    NETW_ERR_COND(
        p_transition < 0,
        sys::PREDICTION,
        "Prediction recorded a group tick with a negative tick number."
    );
    if (p_state.any()) {
        row->record_state_row(p_transition + 1, p_state);
    }
    row->record_joint_command(
        p_transition,
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
        sys::PREDICTION,
        "Prediction moved the oldest replay tick of an entity that is not in "
        "a group."
    );
    NETW_ERR_COND(
        p_basis < 0 || p_source < JOINT_FLOOR_ACK
            || p_source > JOINT_FLOOR_EPOCH,
        sys::PREDICTION,
        "Prediction moved the oldest replay tick of a group to a negative "
        "tick or from an unknown source."
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
        row->clear_joint_cells();
    }
}

int NetwPredictionEngine::joint_provenance_at(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->inputs.origin_of(p_transition) : -1;
}

predict::JointPassPlan NetwPredictionEngine::joint_pass(
    int64_t p_slot,
    int64_t p_present
) {
    NETW_ZONE_NC("Predict joint group pass", colors::PREDICTION);
    predict::JointPassPlan plan;
    predict::Slot *owner = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        owner == nullptr || owner->config.island != ISLAND_JOINT
            || !rerunnable(owner->config.schedule),
        plan,
        sys::PREDICTION,
        "Prediction tried a group replay for an entity that does not lead a "
        "NetwPrediction.RECONCILE_JOINT group able to replay."
    );
    NETW_ERR_COND_V(
        p_present < 0,
        plan,
        sys::PREDICTION,
        "Prediction tried a group replay up to a negative tick."
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
    subject.order_key = owner->joint_roster.owner_order_key;
    members.push_back(subject);
    for (const sim::Selected &selected : owner->joint_roster.members) {
        if (!selected.promoted && !selected.lingering) {
            continue;
        }
        predict::Slot *member = mutable_row_of(selected.key);
        if (member == nullptr) {
            NETW_DEBUG(
                sys::PREDICTION,
                "Group member %d closed before its group let it go.",
                selected.key
            );
            continue;
        }
        PassMember pass_member;
        pass_member.row = member;
        pass_member.slot = selected.key;
        pass_member.order_key = selected.order_key;
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
        history_floor = std::max(
            history_floor,
            track.history_floor(
                members[at].row->joint_oldest_state(),
                members[at].row->inputs.oldest_key()
            )
        );
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
        owner->joint_roster.release_lingering(p_floor);
        plan.floor = p_floor;
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
    predict::StateRow held;
    if (!decision.heal) {
        for (uint32_t at = 0; at < members.size(); ++at) {
            if (!members[at].row->joint_state(decision.floor, held)) {
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
        const predict::Slot &member = *members[at].row;
        const int64_t source = decision.heal
            ? member.joint_newest_state(p_present)
            : decision.floor;
        const bool found = source >= 0 && member.joint_state(source, held);
        const predict::StateRow *state = found ? &held : nullptr;
        if (state == nullptr) {
            NETW_DEBUG(
                sys::PREDICTION,
                "Group replay %d stopped, because member %d has no saved "
                "state at that tick.",
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
        restore.entry = source + 1;
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
                const predict::Slot &member = *members[at].row;
                const int origin = member.inputs.origin_of(transition);
                predict::JointStep step;
                step.slot = members[at].slot;
                step.transition = transition;
                if (origin >= 0) {
                    member.inputs.read_row(
                        member.input_codec,
                        transition,
                        step.command
                    );
                    step.provenance = predict::CellProvenance(origin);
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

    stats.linger_held = owner->joint_roster.lingering_count();
    for (uint32_t at = 0; at < members.size(); ++at) {
        members[at].row->joint.clear_floors();
    }
    NETW_TRACE(
        sys::PREDICTION,
        "Group %d replayed from tick %d with %d of %d members present (heal "
        "%d).",
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


int NetwPredictionEngine::transition_span(
    int64_t p_slot,
    int64_t p_basis
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->transition_span(p_basis) : 0;
}

void NetwPredictionEngine::journal_snapshot(
    int64_t p_slot,
    predict::JournalSnapshot &r_snapshot
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        r_snapshot.adopt(predict::Journal(), Dictionary());
        return;
    }
    r_snapshot.adopt(row->journal, row->witness_details.rows());
}

void NetwPredictionEngine::attach_journal_readers(bool p_attached) {
    journal_readers = p_attached;
    for (predict::Slot *row : rows) {
        if (row != nullptr) {
            row->journal.arm_diagnostics(p_attached);
        }
    }
}

void NetwPredictionEngine::attach_journal_reader(
    int64_t p_slot,
    bool p_attached
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->journal.arm_diagnostics(p_attached || journal_readers);
    }
}

bool NetwPredictionEngine::journal_diagnostics_armed(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->journal.diagnostics_armed();
}

uint32_t NetwPredictionEngine::book_depth(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->book.capacity() : 0;
}

int NetwPredictionEngine::journal_capacity(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->journal.capacity_limit() : 0;
}

void NetwPredictionEngine::bind_session(NetwMultiplayer *p_core) {
    core_id = gd::instance_id(p_core);
    binding_book
        = p_core != nullptr ? &p_core->property_binding_book() : nullptr;
}

void NetwPredictionEngine::attach_bindings(repl::BindingBook *p_book) {
    binding_book = p_book;
}

repl::PropertyBinding *NetwPredictionEngine::binding_by_id(
    uint64_t p_id
) const {
    return binding_book != nullptr ? binding_book->get(p_id) : nullptr;
}

NetwMultiplayer *NetwPredictionEngine::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

bool NetwPredictionEngine::wants_report(
    int64_t p_slot,
    int64_t p_event
) const {
    NetwMultiplayer *host = core();
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    return host != nullptr && seated.is_valid()
        && host->event_wants(p_event, host->liveness_route_of(seated.ptr()));
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
        seated->get_name(),
        0,
        OK,
        p_model
    );
}

Object *NetwPredictionEngine::handle_of(int64_t p_slot) const {
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    return seated.is_valid() ? seated->get_prediction().ptr() : nullptr;
}

int NetwPredictionEngine::episode_change(int64_t p_event) {
    if (p_event == EventPlane::EPISODE_OPEN) {
        return NetwPrediction::EPISODE_CHANGE_OPENED;
    }
    if (p_event == EventPlane::EPISODE_CLOSE) {
        return NetwPrediction::EPISODE_CHANGE_CLOSED;
    }
    if (p_event == EventPlane::EPISODE_FALLBACK) {
        return NetwPrediction::EPISODE_CHANGE_FALLBACK;
    }
    return -1;
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
    const int change = episode_change(p_event);
    if (change < 0) {
        return;
    }
    const Dictionary report = episode_record(p_slot);
    Object *held = handle_of(p_slot);
    if (held != nullptr) {
        held->emit_signal(StringName("episode_changed"), change, report);
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
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    predict::read_payload(row->wiring, p_before, row->recovering.before);
    predict::read_payload(
        row->wiring,
        p_correction_write,
        row->recovering.write
    );
    return announce_recovered_rows(p_slot, p_transition, p_attribution);
}

bool NetwPredictionEngine::announce_recovered_rows(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_attribution
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    predict::RecoveryRows &recovering = row->recovering;
    if (!write_deltas_rows(
            p_slot,
            recovering.before,
            recovering.write,
            recovering.deltas
        )) {
        return false;
    }
    ledger_note_rows(p_slot, recovering.deltas);
    Object *held = handle_of(p_slot);
    const StringName &recovered = signal_recovered();
    const bool heard = held != nullptr && held->has_connections(recovered);
    if (!heard && !wants_report(p_slot, EventPlane::RECOVERY)) {
        return true;
    }
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return true;
    }
    const Dictionary deltas
        = predict::payload_of(row->wiring, row->recovering.deltas);
    const bool teleported = last_correction_teleported_of(p_slot);
    if (heard) {
        held->emit_signal(
            recovered,
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
    if (!wants_report(p_slot, EventPlane::DIVERGENCE)) {
        return;
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

bool broadcast_set_of(const Ref<NetwEntity> &p_entity, PropertySet &r_set) {
    const Ref<Script> script = declaring_script(p_entity);
    return script.is_valid()
        && property_set_builder::compile_script(
               r_set,
               script,
               PropertySet::RECORD_BROADCAST,
               nullptr
        );
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
    const PropertyBindingPtr &p_binding
) {
    LocalVector<predict::FieldDecl> out;
    const PropertySet *declared
        = p_binding != nullptr ? p_binding->get_set() : nullptr;
    if (declared == nullptr) {
        return out;
    }
    Node *node = p_binding->node();
    for (const PropertyColumn &column : declared->columns) {
        const StringName key = column.get_key();
        out.push_back(field_decl(
            key,
            int(column.property_class),
            p_binding->carry_channel_of(key),
            p_binding->converge_stiffness_of(key),
            p_binding->teleport_only_of(key),
            p_binding->reconcile_only_of(key),
            p_binding->epsilon_override_of(key),
            p_binding->teleport_at_of(key),
            declared_angle(node, key),
            column.get_quantizer(),
            int(declared_type(node, key))
        ));
        out[out.size() - 1].column_type = column.shape.type;
    }
    return out;
}

} // namespace

int64_t NetwPredictionEngine::current_tick() const {
    NetwMultiplayer *host = core();
    return host != nullptr ? host->clock_engine().get_tick() : -1;
}

void NetwPredictionEngine::report_quantum_declaration() {
    NetwMultiplayer *host = core();
    if (host != nullptr) {
        host->warn_pacing_rate_mismatch_once();
    }
}

int64_t NetwPredictionEngine::register_rewind(
    const Ref<NetwEntity> &p_entity
) {
    NetwMultiplayer *host = core();
    if (host == nullptr || p_entity.is_null()) {
        return -1;
    }
    return host->register_rewind(p_entity);
}

int NetwPredictionEngine::adopt_declaration(
    const Ref<NetwEntity> &p_entity,
    const PropertyBindingPtr &p_state,
    const PropertyBindingPtr &p_input,
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
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(slot));
    if (node != nullptr && handle != nullptr) {
        bind_owner(slot, node);
        NetwMultiplayer *host = core();
        const int64_t route
            = host != nullptr ? host->liveness_route_of(p_entity.ptr()) : -1;
        set_order_key(slot, route > 0 ? route : -1);
        set_simulate(slot, handle->get_simulate());
        set_witness(slot, handle->get_witness_contacts());
        set_corridor(slot, handle->get_correction_path_clear());
        const Dictionary sensors = handle->get_sensors();
        const Array named = sensors.keys();
        for (int at = 0; at < named.size(); ++at) {
            set_sensor(slot, named[at], sensors[named[at]]);
        }
    }

    const int correction = settle_correction(slot, p_declared_correction);
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

Dictionary NetwPredictionEngine::recovery_before_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        ? predict::payload_of(row->wiring, row->recovering.before)
        : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_write_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        ? predict::payload_of(row->wiring, row->recovering.write)
        : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_projection_of(int64_t p_slot) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const LocalVector<int> &projection = row->recovering.projection;
    const int count = std::min(row->wiring.count(), int(projection.size()));
    for (int at = 0; at < count; ++at) {
        const int channel = projection[uint32_t(at)];
        if (channel >= 0) {
            out[row->wiring.fields.name_at(at)]
                = row->wiring.fields.name_at(channel);
        }
    }
    return out;
}

Dictionary NetwPredictionEngine::recovery_carried_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr
        ? predict::payload_of(row->wiring, row->recovering.carried)
        : Dictionary();
}

Dictionary NetwPredictionEngine::recovery_tier_errors_of(int64_t p_slot) const {
    Dictionary out;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    const LocalVector<double> &errors = row->recovering.tier_errors;
    const int count = std::min(row->wiring.count(), int(errors.size()));
    for (int at = 0; at < count; ++at) {
        if (errors[uint32_t(at)] >= 0.0) {
            out[row->wiring.fields.name_at(at)] = errors[uint32_t(at)];
        }
    }
    return out;
}

Dictionary NetwPredictionEngine::recovery_context(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_ack_label,
    int p_domain,
    int p_attribution,
    bool p_escalated
) {
    Dictionary context;
    const predict::Slot *row = row_of(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return context;
    }
    context[StringName("domain")] = p_domain;
    context[StringName("attribution")] = p_attribution;
    context[StringName("contact_window")]
        = out_of_domain_at(p_slot, p_ack_label)
        || row->recovery.window_contains(p_ack);
    context[StringName("suppressed")] = handle->get_sleeping()
        || probation_pending(p_slot)
        || latest_input_tick_of(p_slot) < cooldown_until_tick_of(p_slot)
        || row->recovery.suppressed_at(p_ack_label);
    context[StringName("pose_unmeasured")]
        = p_escalated || !has_pose_fields(p_slot);
    context[StringName("ack_age_ticks")] = handle->get_ack_age_ticks();
    return context;
}

PlannedRecovery NetwPredictionEngine::plan_recovery(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_ack_label,
    const Dictionary &p_predicted,
    const Dictionary &p_payload,
    bool p_escalated,
    int p_domain,
    int p_attribution,
    const Callable &p_seam
) {
    PlannedRecovery out;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    predict::StateRow predicted;
    predict::StateRow payload;
    predict::read_payload(row->wiring, p_predicted, predicted);
    predict::read_payload(row->wiring, p_payload, payload);
    const PlannedRows planned = plan_recovery_rows(
        p_slot,
        p_ack,
        p_ack_label,
        predicted,
        payload,
        p_escalated,
        p_domain,
        p_attribution,
        p_seam
    );
    if (planned.pool != nullptr) {
        out.pool = *planned.pool;
    }
    out.answered = planned.answered;
    row = mutable_row_of(p_slot);
    if (planned.answered && row != nullptr) {
        out.answer = recovery_of(p_slot, row->recovering.answer);
    }
    return out;
}

PlannedRows NetwPredictionEngine::plan_recovery_rows(
    int64_t p_slot,
    int64_t p_ack,
    int64_t p_ack_label,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_payload,
    bool p_escalated,
    int p_domain,
    int p_attribution,
    const Callable &p_seam
) {
    PlannedRows out;
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return out;
    }
    const double epsilon = handle->get_divergence_epsilon();
    const double teleport = handle->get_teleport_threshold();
    const int ack_age = handle->get_ack_age_ticks();

    guard_projection_rows(p_slot, ack_age);
    carry_rows(p_slot, p_payload, p_ack, teleport, epsilon);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    LocalVector<double> &tier_errors = row->recovering.tier_errors;
    if (p_escalated) {
        tier_errors.resize(uint32_t(row->wiring.count()));
        for (uint32_t at = 0; at < tier_errors.size(); ++at) {
            tier_errors[at] = -1.0;
        }
    } else {
        pose_errors_rows(p_slot, p_payload, ack_age, tier_errors);
    }
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return out;
    }
    note_errors(*row, row->report.tier_error, row->recovering.tier_errors);

    const bool suppressed = handle->get_sleeping() || probation_pending(p_slot)
        || latest_input_tick_of(p_slot) < cooldown_until_tick_of(p_slot);
    if (p_seam.is_valid()) {
        out.answered = ask_recover_seam(
            p_slot,
            recovery_context(
                p_slot,
                p_ack,
                p_ack_label,
                p_domain,
                p_attribution,
                p_escalated
            ),
            p_seam
        );
        row = mutable_row_of(p_slot);
        if (row == nullptr) {
            return out;
        }
    }
    out.pool = recover_rows(
        p_slot,
        p_ack,
        p_ack_label,
        p_predicted,
        row->recovering.carried,
        row->recovering.before,
        row->recovering.tier_errors,
        handle->resolved_recovery_policy(),
        epsilon,
        teleport,
        handle->get_max_restore_ticks(),
        ack_age,
        handle->get_collision_cooldown_ticks(),
        p_domain,
        p_attribution,
        suppressed,
        out.answered ? &row->recovering.answer : nullptr
    );
    return out;
}

void NetwPredictionEngine::guard_projection_rows(
    int64_t p_slot,
    int p_ack_age_ticks
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const int count = row->wiring.count();
    LocalVector<int> &projection = row->recovering.projection;
    projection.resize(uint32_t(count));
    const bool measured = !row->report.divergence.empty();
    const double span
        = double(std::clamp(p_ack_age_ticks, 0, row->config.max_restore_ticks))
        * row->tick_delta;
    for (int at = 0; at < count; ++at) {
        projection[uint32_t(at)] = -1;
        const int channel = row->wiring.projection[uint32_t(at)];
        if (channel < 0) {
            continue;
        }
        if (!measured) {
            projection[uint32_t(at)] = channel;
            continue;
        }
        const double declared = row->wiring.epsilon[uint32_t(at)];
        const double limit = declared >= 0.0 ? declared : row->config.epsilon;
        const double projected_error
            = row->report.divergence.at(channel, 0.0) * span;
        if (projected_error < limit) {
            projection[uint32_t(at)] = channel;
        }
    }
}

void NetwPredictionEngine::carry_rows(
    int64_t p_slot,
    const predict::StateRow &p_payload,
    int64_t p_basis,
    double p_teleport_default,
    double p_divergence_epsilon
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    row->carry_attempts.clear();
    row->recovering.carried = p_payload;
    if (row->carry_rules.is_empty()) {
        return;
    }
    const Array fields = carry_rule_fields(p_slot);
    for (int at = 0; at < fields.size(); ++at) {
        const StringName field = fields[at];
        row = mutable_row_of(p_slot);
        if (row == nullptr) {
            return;
        }
        const int slot_field = row->wiring.fields.index_of(field);
        if (slot_field < 0 || !p_payload.has(slot_field)) {
            continue;
        }
        predict::CarryAttempt charged;
        charged.field = field;
        if (!carry_eligible(p_slot, field)) {
            charged.verdict = decline_carry(p_slot, field);
            mutable_row_of(p_slot)->carry_attempts.push_back(charged);
            ledger_seed(p_slot, field);
            continue;
        }
        const predict::CarryAttempt attempt = attempt_carry(
            p_slot,
            field,
            p_payload.values[uint32_t(slot_field)],
            p_basis,
            p_teleport_default,
            p_divergence_epsilon
        );
        row = mutable_row_of(p_slot);
        if (row == nullptr) {
            return;
        }
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
        if (charged.verdict == CARRY_CARRIED) {
            row->recovering.carried.set(slot_field, attempt.value);
        }
    }
    report_carry_retirements(p_slot);
}

void NetwPredictionEngine::pose_errors_rows(
    int64_t p_slot,
    const predict::StateRow &p_payload,
    int p_ack_age_ticks,
    LocalVector<double> &r_errors
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const int count = row->wiring.count();
    r_errors.resize(uint32_t(count));
    for (uint32_t at = 0; at < r_errors.size(); ++at) {
        r_errors[at] = -1.0;
    }
    if (!has_pose_fields(p_slot)) {
        return;
    }
    const int span
        = std::clamp(p_ack_age_ticks, 0, row->config.max_restore_ticks);
    const double age = double(span) * row->tick_delta;
    predict::StateRow &live = row->recovering.live;
    capture_canonical(p_slot, live);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const int given = std::min(count, int(p_payload.values.size()));
    for (int at = 0; at < given; ++at) {
        const uint32_t field = uint32_t(at);
        if (row->wiring.pose[field] == 0 || !live.has(at)
            || !p_payload.has(at)) {
            continue;
        }
        Variant target = p_payload.values[field];
        const int channel = row->wiring.projection[field];
        if (age > 0.0 && channel >= 0 && channel < given
            && p_payload.has(channel)
            && project::supports(int(target.get_type()))) {
            target = project::forward(
                target,
                p_payload.values[uint32_t(channel)],
                age
            );
        }
        r_errors[field] = predict::value_error(
            live.values[field],
            target,
            row->wiring.angle[field] != 0
        );
    }
}

bool NetwPredictionEngine::apply_recovery_plan(
    int64_t p_slot,
    const RecoveryPlan &p_plan,
    int64_t p_ack,
    bool p_pool_planned
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    predict::WritePlan &staged = row->recovering.staged;
    predict::read_payload(row->wiring, p_plan.restore, staged.restore);
    predict::read_payload(row->wiring, p_plan.write, staged.write);
    staged.teleport = p_plan.teleport;
    staged.skip = p_plan.skip;
    return apply_plan_rows(p_slot, staged, p_ack, p_pool_planned);
}

bool NetwPredictionEngine::apply_plan_rows(
    int64_t p_slot,
    const predict::WritePlan &p_plan,
    int64_t p_ack,
    bool p_pool_planned
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return false;
    }
    bool projected = false;
    for (const int channel : row->recovering.projection) {
        projected = projected || channel >= 0;
    }
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
        note_verdict_reason(p_slot, predict::VERDICT_REASON_DECLINED);
        return false;
    }
    const bool seated_basis
        = row->config.correction == int(CorrectionMode::REPLAY)
        && seat_restore_row(p_slot, p_ack + 1, p_plan.restore);
    row = mutable_row_of(p_slot);
    int op = int(predict::Operator::REBASE_EXACT);
    if (p_plan.teleport) {
        op = int(predict::Operator::FULL_CLOSURE);
    } else if (
        row->config.correction == int(CorrectionMode::SNAP)
        && handle->get_snap_restore()
            == NetwSimulation::RESTORE_EXTRAPOLATED
        && projected
    ) {
        op = int(predict::Operator::REBASE_PROJECTED);
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    if (apply_restore_row(
            p_slot,
            p_plan.restore,
            op,
            p_ack,
            p_slot,
            seated.is_valid() ? seated->get_name() : StringName(),
            handle->get_ack_age_ticks(),
            handle->get_divergence_epsilon(),
            false,
            p_pool_planned,
            seated_basis ? p_ack + 1 : -1
        )) {
        if (!p_pool_planned) {
            sync_episode(p_slot);
        }
    } else {
        NETW_ERROR(
            sys::PREDICTION,
            "Prediction could not start a correction it had planned."
        );
    }
    row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->recovering.write = p_plan.write;
    }
    return seated_basis;
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
    if (row == nullptr) {
        return RECOVERY_PLAN;
    }
    predict::StateRow predicted;
    predict::StateRow payload;
    predict::read_payload(row->wiring, p_predicted, predicted);
    predict::read_payload(row->wiring, p_payload, payload);
    return open_recovery_rows(
        p_slot,
        p_ack,
        predicted,
        payload,
        p_meter,
        p_domain
    );
}

int NetwPredictionEngine::open_recovery_rows(
    int64_t p_slot,
    int64_t p_ack,
    const predict::StateRow &p_predicted,
    const predict::StateRow &p_payload,
    int p_meter,
    int p_domain
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (row == nullptr || handle == nullptr) {
        return RECOVERY_PLAN;
    }
    row->config.correction = settle_correction(
        p_slot,
        predict::correction_for_recovery_policy(handle->get_recovery_policy())
    );
    capture_canonical(p_slot, row->recovering.before);
    row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return RECOVERY_PLAN;
    }
    blank_row(row->recovering.write, row->wiring.count());

    const Ref<NetwEntity> seated = owner_entity(p_slot);
    const double epsilon = handle->get_divergence_epsilon();
    const int policy = handle->resolved_recovery_policy();
    const bool escalated = escalation_pending(p_slot);

    Dictionary carried;
    const PackedInt64Array stats = episode_stats(p_slot);
    if (handle->get_correction_path_clear().is_valid()
        && stats[STAT_EPISODE_ACTIVE] != 0
        && stats[STAT_EPISODE_TRANSPORT_DECIDED] == 0) {
        carried = try_transport(
            p_slot,
            predict::payload_of(row->wiring, p_predicted),
            predict::payload_of(row->wiring, p_payload),
            predict::payload_of(row->wiring, row->recovering.before),
            p_ack,
            escalated,
            handle->get_correction_path_clear(),
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
            policy == NetwPrediction::RECOVERY_POLICY_OBSERVE,
            seated.is_valid() ? seated->get_name() : StringName("body"),
            handle->get_ack_age_ticks()
        );
        if (decision != DISSIPATE_UNDECIDED) {
            sync_episode(p_slot);
        }
        if (decision == DISSIPATE_APPLIED) {
            note_verdict_reason(p_slot, predict::VERDICT_REASON_DISSIPATED);
            return RECOVERY_DISSIPATED;
        }
    }

    note_reconciling(p_slot, true);
    NetwPredictStats::Book *counters = handle->fact_book();
    if (counters != nullptr) {
        counters->set_int_fact(
            NetwPredictStats::FACT_CORRECTIONS,
            counters->get_int_fact(NetwPredictStats::FACT_CORRECTIONS) + 1
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
            seated.is_valid() ? seated->get_name() : StringName(),
            handle->get_ack_age_ticks(),
            epsilon,
            false,
            false
        )) {
        sync_episode(p_slot);
    } else {
        NETW_ERROR(
            sys::PREDICTION,
            "Prediction could not start a correction it had planned."
        );
    }
    row = mutable_row_of(p_slot);
    if (row != nullptr) {
        predict::read_payload(row->wiring, restore_row, row->recovering.write);
    }
    return RECOVERY_TRANSPORTED;
}

predict::Feed NetwPredictionEngine::role_feed(
    int p_role,
    bool p_fallback_latched
) const {
    predict::Feed feed;
    switch (p_role) {
        case int(Role::PREDICT):
            feed.input_volatile_external = true;
            feed.state_write_gate = false;
            feed.state_listener = predict::Listener::STATE;
            break;
        case int(Role::CONSUME):
            feed.input_write_gate = false;
            feed.input_listener = predict::Listener::INPUT;
            break;
        case int(Role::SIMULATE):
            feed.input_volatile_external = true;
            feed.state_write_gate = false;
            feed.state_listener = predict::Listener::SIMULATED;
            break;
        case int(Role::REMOTE):
            if (p_fallback_latched) {
                feed.input_volatile_external = true;
                feed.state_listener = predict::Listener::QUARANTINE;
            }
            break;
        default:
            break;
    }
    return feed;
}

void NetwPredictionEngine::push_carry_rules(int64_t p_slot) {
    const PropertyBindingPtr binding = state_binding_of(p_slot);
    Node *node = binding != nullptr ? binding->node() : nullptr;
    const PropertySet *declared
        = binding != nullptr ? binding->get_set() : nullptr;
    if (node == nullptr || declared == nullptr) {
        return;
    }
    for (const PropertyColumn &column : declared->columns) {
        const StringName key = column.get_key();
        const Callable step
            = netw::script::model::get_node_property_carry(node, key);
        if (!step.is_valid()) {
            continue;
        }
        if (projection_of(p_slot, field_slot(p_slot, key)) >= 0) {
            NETW_ERROR(
                sys::PREDICTION,
                "'%s' declares both NetwPropertyConfig.carry_along and "
                "carry_step, so carry_step is ignored. Keep only one of them.",
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
    PropertySet compiled;
    const PropertySet *broadcast
        = broadcast_set_of(seated, compiled) ? &compiled : nullptr;
    if (broadcast != nullptr && !broadcast->columns.is_empty()) {
        PackedStringArray keys;
        for (const PropertyColumn &column : broadcast->columns) {
            keys.push_back(String(column.get_key()));
        }
        NETW_WARN(
            sys::PREDICTION,
            "'%s' is predicted but declares NetwPropertyConfig.broadcast "
            "properties [%s], which cannot be corrected. Declare them with "
            "state, or turn off prediction for this entity.",
            String(seated->get_name()).utf8().get_data(),
            String(", ").join(keys).utf8().get_data()
        );
    }
    const PropertyBindingPtr input = input_binding_of(p_slot);
    const PropertySet *declared
        = input != nullptr ? input->get_set() : nullptr;
    if (declared == nullptr || declared->columns.is_empty()) {
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
            "'%s' has NetwPropertyConfig.input properties without a type, so "
            "its input is not sent. Give each one a type, or quantize it.",
            String(seated->get_name()).utf8().get_data()
        );
    }
}

int64_t NetwPredictionEngine::property_class_report_hash(
    int64_t p_slot,
    const PropertySet *p_set
) const {
    PackedStringArray parts;
    for (const PropertyColumn &column : p_set->columns) {
        Array row;
        row.push_back(String(column.get_key()));
        row.push_back(column.property_class);
        row.push_back(column.get_quantizer().is_valid() ? 1 : 0);
        row.push_back(column.epsilon_override);
        row.push_back(column.explicit_teleport_only ? 1 : 0);
        row.push_back(column.explicit_reconcile_only ? 1 : 0);
        row.push_back(String(column.carry_channel));
        row.push_back(has_carry_rule(p_slot, column.get_key()) ? 1 : 0);
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
    PropertySet compiled;
    if (broadcast_set_of(seated, compiled)) {
        const PropertySet *broadcast = &compiled;
        for (const PropertyColumn &column : broadcast->columns) {
            parts.push_back(String("broadcast:") + String(column.get_key()));
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
    const predict::StateRow &p_payload
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

bool NetwPredictionEngine::release_deferred_operator(
    int64_t p_slot,
    predict::StateRow &r_payload
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->deferred_operator.held()) {
        r_payload.resize(0);
        return false;
    }
    r_payload = row->deferred_operator.payload;
    row->deferred_operator.release();
    return true;
}

int NetwPredictionEngine::defer_operator_for_witness(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_basis,
    const predict::StateRow &p_payload,
    bool p_has_corridor,
    bool p_observing
) {
    const int verdict
        = defer_verdict(p_slot, p_basis, p_has_corridor, p_observing);
    if (verdict == DEFER_HELD) {
        hold_deferred_operator(p_slot, p_basis, p_recv_tick, p_payload);
    }
    return verdict;
}

int NetwPredictionEngine::defer_verdict(
    int64_t p_slot,
    int64_t p_basis,
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

bool NetwPredictionEngine::open_state_stamp(
    int64_t p_slot,
    bool p_raw_enabled,
    predict::StateStamp &r_stamp
) {
    r_stamp = predict::StateStamp();
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->port.bound()) {
        return false;
    }
    r_stamp.fp = int32_t(live_fingerprint(p_slot, &r_stamp.families));
    if (p_raw_enabled) {
        r_stamp.raw_fp = int32_t(prediction_core::raw_state_fingerprint(
            causal_only(*row, capture_state(p_slot))
        ));
        r_stamp.evidence_mask = predict::EVIDENCE_RAW;
    }
    return true;
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
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return PackedFloat64Array();
    }
    LocalVector<double> values;
    fill_correction_tolerances(*row, p_fallback_epsilon, values);
    return packed(values);
}

PackedFloat64Array NetwPredictionEngine::meter_tolerances(
    int64_t p_slot,
    int p_domain,
    double p_fallback_epsilon
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return PackedFloat64Array();
    }
    LocalVector<double> values;
    fill_meter_tolerances(*row, p_domain, p_fallback_epsilon, values);
    return packed(values);
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
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
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
            predict::VERDICT_REASON_PROBATION_REQUARANTINE
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
        if (episode_state(p_slot) == NetwPrediction::EPISODE_STATE_OPEN) {
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
    if (refusal == predict::VERDICT_REASON_NONE) {
        return SETTLE_PROCEED;
    }
    if (refusal == predict::VERDICT_REASON_EVIDENCE_EXHAUSTED) {
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
    if (p_previous_state == NetwPrediction::EPISODE_STATE_OPEN
        && state == NetwPrediction::EPISODE_STATE_CLOSED) {
        close_episode(p_slot);
        return;
    }
    sync_episode(p_slot);
    if (p_previous_state != NetwPrediction::EPISODE_STATE_OPEN
        && state == NetwPrediction::EPISODE_STATE_OPEN) {
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

bool NetwPredictionEngine::advanced_seed(
    int64_t p_slot,
    const predict::StateRow &p_payload,
    int64_t p_basis,
    int p_snap_restore,
    int p_max_restore_ticks,
    double p_teleport_threshold,
    double p_divergence_epsilon,
    predict::StateRow &r_seed
) {
    r_seed = p_payload;
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr || !p_payload.any()) {
        return false;
    }
    carry_rows(
        p_slot,
        p_payload,
        p_basis,
        p_teleport_threshold,
        p_divergence_epsilon
    );
    row = row_of(p_slot);
    if (row == nullptr) {
        return false;
    }
    for (const predict::CarryAttempt &charged : row->carry_attempts) {
        const int field = row->wiring.fields.index_of(charged.field);
        if (charged.verdict == CARRY_CARRIED && field >= 0) {
            r_seed.set(field, charged.value);
        }
    }
    bool projects = false;
    for (const int channel : row->wiring.projection) {
        projects = projects || channel >= 0;
    }
    if (p_snap_restore != int(NetwSimulation::RESTORE_EXTRAPOLATED)
        || !projects) {
        return false;
    }
    const int64_t span = CLAMP(
        transition_span(p_slot, p_basis),
        int64_t(0),
        int64_t(p_max_restore_ticks)
    );
    if (span <= 0) {
        return false;
    }
    project_row(row->wiring, double(span) * row->tick_delta, r_seed);
    return true;
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
    const PropertySet *p_set,
    const Callable &p_emit_findings
) {
    if (p_set == nullptr) {
        return;
    }
    for (const PropertyColumn &column : p_set->columns) {
        if (column.get_lane() != PropertySet::RETAINED) {
            continue;
        }
        NETW_ERROR(
            sys::PREDICTION,
            "Predicted state property '%s' uses NetwPropertyConfig.retained, "
            "which a predicted entity cannot use. Remove retained from it.",
            String(column.get_key()).utf8().get_data()
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
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (handle == nullptr) {
        return Dictionary();
    }
    const Ref<NetwEntity> seated = owner_entity(p_slot);
    NetwMultiplayer *host = core();
    const sim::Row *standing = host != nullptr && seated.is_valid()
        ? host->sim_row_of(seated->get_rid_handle())
        : nullptr;
    int claim = 0;
    if (standing != nullptr && standing->choice.declared()) {
        claim = sim::claims_exact(
                    standing->choice,
                    handle->get_schedule()
                        == NetwSimulation::SCHEDULE_STEPPED
                )
            ? 1
            : 2;
    }
    return reachability_report(
        p_slot,
        seated.is_valid() ? seated->get_name() : StringName(),
        handle->get_divergence_epsilon(),
        handle->get_teleport_threshold(),
        handle->get_breach_response(),
        breach_source_of(p_slot),
        claim,
        handle->get_collision_cooldown_ticks(),
        handle->get_correction_path_clear().is_valid()
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
                    "carry_step() needs simulation.schedule = FRAME; under "
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
                    "NetwPrediction.field_recovery counts which "
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
                    "simulation.schedule to FRAME, or carry_along() a "
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
        "Prediction reset the tick history of an entity it no longer "
        "predicts (slot %d).",
        p_slot
    );
    row->journal.clear(p_epoch);
}

void NetwPredictionEngine::tape_reset(int64_t p_slot, int64_t p_epoch) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Prediction reset the input history of an entity it no longer "
        "predicts (slot %d).",
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
        "Prediction acknowledged input for an entity it no longer predicts "
        "(slot %d).",
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
        "Prediction renumbered the input history of an entity it no longer "
        "predicts (slot %d).",
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
        "Prediction recorded an input tick for an entity it no longer "
        "predicts (slot %d).",
        p_slot
    );
    row->author_tape_entry(p_label, p_fresh);
}

void NetwPredictionEngine::open_lane(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Prediction read input for an entity it no longer predicts "
        "(slot %d).",
        p_slot
    );
    row->open_lane();
}

void NetwPredictionEngine::close_lane(int64_t p_slot) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->close_lane();
    }
}

bool NetwPredictionEngine::wire_role_timeline(
    int64_t p_slot,
    int p_role,
    bool p_fallback_latched
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Prediction set up the roles of an entity it no longer predicts "
        "(slot %d).",
        p_slot
    );
    const Ref<NetwEntity> entity = owner_entity(p_slot);
    const bool speculates = p_role == NetwPrediction::ROLE_PREDICT
        || p_role == NetwPrediction::ROLE_SIMULATE
        || (p_role == NetwPrediction::ROLE_REMOTE && p_fallback_latched);
    const bool reads_registry = p_role == NetwPrediction::ROLE_CONSUME
        || p_role == NetwPrediction::ROLE_HOST_LOCAL;
    if (!speculates && !reads_registry) {
        return false;
    }
    if (speculates || register_rewind(entity) >= 0) {
        row->open_lane();
    } else {
        row->close_lane();
    }
    if (p_role == NetwPrediction::ROLE_PREDICT) {
        set_latest_input_tick(p_slot, -1);
    } else if (p_role == NetwPrediction::ROLE_CONSUME) {
        set_next_input_tick(p_slot, -1);
        set_ack(p_slot, -1);
        forget_last_input(p_slot);
    }
    return true;
}

bool NetwPredictionEngine::record_predicted(
    int64_t p_slot,
    int64_t p_key,
    const predict::StateRow &p_state
) {
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && row->record_state_row(p_key, p_state);
}

const predict::TransitionBook *NetwPredictionEngine::predicted_book(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->book : nullptr;
}

const predict::TransitionBook *NetwPredictionEngine::input_book(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->inputs : nullptr;
}

const predict::Wiring *NetwPredictionEngine::wiring_of(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->wiring : nullptr;
}

const predict::FieldCodec *NetwPredictionEngine::input_codec_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? &row->input_codec : nullptr;
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

DriveOutcome NetwPredictionEngine::record_drive_hashed(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label,
    int p_kind,
    int64_t p_c_hash,
    int64_t p_drive_tick,
    bool p_caller_selected,
    bool p_input_recorded,
    bool p_authoring
) {
    NETW_PREDICT_TICK_SCOPE();
    DriveOutcome drove;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return drove;
    }
    const bool is_new = !journal_has(p_slot, p_transition);
    NetwPrediction *declared
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (is_new && declared != nullptr
        && declared->get_schedule() == NetwSimulation::SCHEDULE_FRAME) {
        report_quantum_declaration();
        row = mutable_row_of(p_slot);
        if (row == nullptr) {
            return drove;
        }
    }
    predict::StateStamp stamp;
    open_state_stamp(p_slot, row->cursor.raw_fingerprints, stamp);
    const int64_t pre_fp = stamp.fp;
    const int64_t raw_fp = stamp.raw_fp;
    const int evidence_mask = int(stamp.evidence_mask);
    const int64_t c_hash = p_c_hash;

    const PackedStringArray &participants = row->island_participants;
    if (!row->topology_open || row->topology_schedule != row->config.schedule
        || row->topology_epoch != row->axes.epoch
        || row->topology_participants != participants) {
        row->rows.open_topology
            = topology_facts(p_slot, row->config.schedule, row->axes.epoch);
        row->topology_schedule = row->config.schedule;
        row->topology_epoch = row->axes.epoch;
        row->topology_participants = participants;
        row->topology_open = true;
    }
    const Dictionary facts = row->rows.open_topology;

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
                  row->frame_index,
                  row->tick_delta,
                  row->declared_quantum,
                  pre_fp,
                  stamp.families.pose,
                  stamp.families.momentum,
                  stamp.families.controller,
                  raw_fp,
                  evidence_mask,
                  p_authoring
              )
            : open_drive(
                  p_slot,
                  facts,
                  p_drive_tick,
                  row->frame_index,
                  row->tick_delta,
                  row->declared_quantum,
                  true,
                  pre_fp,
                  stamp.families.pose,
                  stamp.families.momentum,
                  stamp.families.controller,
                  raw_fp,
                  evidence_mask
              );
        const int64_t opened = record.transition;
        if (record.ran) {
            drove.transition = opened;
            drove.label = record.label;
            drove.kind = int(record.kind);
            drove.fresh = record.fresh;
            drove.ran = true;
        }
        if (opened >= 0) {
            stamp_pending_provenance(p_slot, opened);
        }
    }
    if (is_new) {
        if (predict::Slot *held = mutable_row_of(p_slot)) {
            held->pending_provenance = predict::PendingProvenance();
        }
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
    if (drove.ran && host != nullptr
        && host->event_wants(EventPlane::PREDICT_DRIVE, 0)) {
        Dictionary reported;
        reported[StringName("transition")] = drove.transition;
        reported[StringName("label")] = drove.label;
        reported[StringName("kind")] = drove.kind;
        reported[StringName("fresh")] = drove.fresh;
        report_predict(
            p_slot,
            EventPlane::PREDICT_DRIVE,
            reported,
            Dictionary()
        );
    }
    return drove;
}

ComparisonChoice NetwPredictionEngine::select_comparison(
    int64_t p_slot,
    int64_t p_ack
) {
    ComparisonChoice chosen;
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return chosen;
    }
    if (row->config.schedule == int(Schedule::FRAME)) {
        const int64_t label = tape_label_of(p_slot, p_ack);
        if (label < 0 || !row->book.carries_fields(p_ack + 1)) {
            return chosen;
        }
        note_compare_staleness(p_slot, 0);
        chosen.label = label;
        chosen.key = p_ack + 1;
        chosen.chosen = true;
        return chosen;
    }
    if (!row->lane_open) {
        return chosen;
    }
    const int64_t compared = row->book.newest_at_or_before(p_ack + 1);
    note_compare_staleness(
        p_slot,
        compared < 0 ? -1 : int(p_ack + 1 - compared)
    );
    chosen.label = p_ack;
    chosen.key = compared;
    chosen.chosen = true;
    return chosen;
}

OpenedComparison NetwPredictionEngine::open_state_comparison(
    int64_t p_slot,
    int64_t p_recv_tick,
    int64_t p_ack,
    const predict::Arrival *p_arrival,
    const predict::StateRow *p_held
) {
    OpenedComparison opened;
    if (p_ack < 0) {
        return opened;
    }
    NetwPrediction *handle
        = Object::cast_to<NetwPrediction>(handle_of(p_slot));
    if (ack_domain_confirmed_of(p_slot)) {
        const int age = mark_authority_ack(p_slot, p_ack);
        if (age >= 0 && handle != nullptr) {
            handle->set_ack_age_ticks(age);
        }
    }
    const int admission = admit_state(p_slot, p_ack);
    if (admission == ADMIT_REALIGN) {
        opened.realign = true;
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
    const ComparisonChoice chosen = select_comparison(p_slot, p_ack);
    predict::Slot *row = mutable_row_of(p_slot);
    if (!chosen.chosen || row == nullptr) {
        return opened;
    }
    opened = open_comparison(p_slot, p_ack);
    opened.label = chosen.label;
    opened.key = chosen.key;
    opened.opened = true;
    row->compare_stats.comparisons_ran += opened.reconstructed ? 1 : 0;
    row->compare_stats.comparisons_skipped += opened.reconstructed ? 0 : 1;
    if (chosen.key < 0
        || !row->book.read_row(row->wiring.codec, chosen.key, row->compared)) {
        row->compared.resize(row->wiring.count());
    }
    take_received(p_slot, p_arrival, p_held);
    return opened;
}

bool NetwPredictionEngine::read_arrival(
    int64_t p_slot,
    const predict::Arrival &p_arrival,
    predict::StateRow &r_row
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && p_arrival.read(row->wiring.codec, r_row);
}

void NetwPredictionEngine::take_received(
    int64_t p_slot,
    const predict::Arrival *p_arrival,
    const predict::StateRow *p_held
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    if (p_arrival != nullptr) {
        p_arrival->read(row->wiring.codec, row->received);
    } else if (p_held != nullptr) {
        row->received = *p_held;
    } else {
        row->received.resize(row->wiring.count());
    }
}

int NetwPredictionEngine::admit_state(int64_t p_slot, int64_t p_ack) {
    if (row_of(p_slot) == nullptr) {
        return ADMIT_CLOSED;
    }
    note_verdict_reason(p_slot, int(predict::VERDICT_REASON_NONE));
    clear_tier_errors(p_slot);
    if (reseed_align_pending(p_slot)) {
        if (reseed_epoch_confirmed(p_slot)) {
            return ADMIT_REALIGN;
        }
        note_verdict_reason(
            p_slot,
            int(predict::VERDICT_REASON_REALIGN_PENDING)
        );
        return ADMIT_REALIGN_PENDING;
    }
    if (!admit_post_reseed(p_slot, p_ack)) {
        note_verdict_reason(
            p_slot,
            int(predict::VERDICT_REASON_RESEED_IGNORED)
        );
        return ADMIT_RESEED_IGNORED;
    }
    return ADMIT_PROCEED;
}

OpenedComparison NetwPredictionEngine::open_comparison(
    int64_t p_slot,
    int64_t p_ack
) {
    OpenedComparison opened;
    opened.domain = int(predict::Domain::OUT_OF_DOMAIN);
    opened.episode_state = -1;
    if (row_of(p_slot) == nullptr) {
        return opened;
    }
    const int at = journal_slot_of(p_slot, p_ack);
    if (at >= 0) {
        opened.domain = journal_domain_at(p_slot, at);
        opened.row_flags = journal_flags_at(p_slot, at);
    }
    opened.episode_state = episode_state(p_slot);
    opened.probation = probation_pending(p_slot);
    const bool reconstructed = stream_reconstructed_of(p_slot);
    opened.reconstructed = reconstructed;
    if (!reconstructed) {
        clear_divergence(p_slot);
        note_verdict_reason(
            p_slot,
            int(predict::VERDICT_REASON_AWAITING_RECONSTRUCTION)
        );
    }
    return opened;
}

int NetwPredictionEngine::refuse_recovery(int64_t p_slot, bool p_corrected) {
    const int none = int(predict::VERDICT_REASON_NONE);
    if (!p_corrected || row_of(p_slot) == nullptr) {
        return none;
    }
    int reason = none;
    if (episode_budget_exhausted(p_slot)) {
        reason = int(predict::VERDICT_REASON_EVIDENCE_EXHAUSTED);
    } else if (
        episode_operator_pending(
            p_slot,
            int(predict::Operator::TRANSPORT_DELTA)
        )
    ) {
        reason = int(predict::VERDICT_REASON_TRANSPORT_PENDING);
    } else if (
        episode_operator_pending(p_slot, int(predict::Operator::DISSIPATE))
    ) {
        reason = int(predict::VERDICT_REASON_DISSIPATE_PENDING);
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
    const int unknown = int(predict::Attribution::UNKNOWN);
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
        return int(predict::Attribution::COMMAND);
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
    const double age = double(span) * row->tick_delta;
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
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    predict::StateRow restore;
    predict::read_payload(row->wiring, p_payload, restore);
    record_restore_row(
        p_slot,
        restore,
        p_operator,
        p_basis,
        p_provenance_slot,
        p_target,
        p_ack_age_ticks,
        p_divergence_epsilon,
        p_evidence_free,
        p_pool_planned
    );
}

void NetwPredictionEngine::record_restore_row(
    int64_t p_slot,
    const predict::StateRow &p_restore,
    int p_operator,
    int64_t p_basis,
    int64_t p_provenance_slot,
    const StringName &p_target,
    int p_ack_age_ticks,
    double p_divergence_epsilon,
    bool p_evidence_free,
    bool p_pool_planned
) {
    if (row_of(p_slot) == nullptr) {
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
    int64_t delta_fp = 0;
    if (predict::Slot *row = mutable_row_of(p_slot)) {
        bool any = false;
        delta_fp = predict::fingerprint_row(
            row->wiring.codec,
            p_restore,
            ~uint64_t(0),
            row->fingerprint_scratch,
            any
        );
    }
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
    if (predict::Slot *row = mutable_row_of(p_slot)) {
        predict::PendingProvenance &pending = row->pending_provenance;
        pending.episode = episode;
        pending.write_id = write_id;
        pending.op = p_operator;
        pending.basis = p_basis;
        pending.held = true;
    }
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
        measured = String(" The last replay was off by ") + p_residual
            + String(".");
    }
    return String("It did not reproduce ") + String::num_int64(p_count)
        + String(" ticks already recorded.") + measured
        + String(
               " Use carry_step only on a value the physics engine does not "
               "move, and read only the NetwPredictCarryContext in it."
        );
}

void warn_carry_retired(const StringName &p_field, const String &p_reason) {
    NETW_WARN(
        sys::PREDICTION,
        "The NetwPropertyConfig.carry_step rule for '%s' was turned off, so "
        "a correction restores the value as the server had it. %s",
        String(p_field).utf8().get_data(),
        p_reason.utf8().get_data()
    );
}

} // namespace

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
                        "It needs NetwSimulation.schedule set to "
                        "SCHEDULE_FRAME."
                    )
                );
                break;
            case CARRY_RETIRED_IMPURE:
                warn_carry_retired(
                    charged.field,
                    String(
                        "It wrote to the body. A carry_step rule may read only "
                        "its NetwPredictCarryContext and must write nothing."
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
    const predict::StateRow &p_stall
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
    rewired = mutable_row_of(p_slot);
    rewired->frame_command.resize(rewired->input_codec.count());
    rewired->stall = p_stall;
    set_tape_epoch(p_slot, (tape_epoch_of(p_slot) + 1) & 0xFF);
    set_next_tape_entry_index(p_slot, 0);
    set_last_driven_entry_index(p_slot, -1);
    set_last_recorded_entry_index(p_slot, -1);
    if (!p_same_stream) {
        set_stream_reconstructed(p_slot, false);
        const Ref<NetwEntity> seated = owner_entity(p_slot);
        if (core() != nullptr && seated.is_valid()) {
            core()->sim_forget_reconstruction(seated->get_rid_handle());
        }
    }
    clear_out_of_domain_window(p_slot);
    clear_environment_epoch(p_slot);
    set_command_epoch(p_slot, -1);
    set_ack_of_acks(p_slot, -1);
    if (p_was_wired) {
        set_ack_domain_confirmed(p_slot, false);
    }
    set_owner_ack_floor(p_slot, -1);
    note_attribution(p_slot, int(predict::Attribution::UNKNOWN), -1);
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
    const PropertyBindingPtr &p_state,
    const PropertyBindingPtr &p_input
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr) {
        return;
    }
    const uint64_t state = p_state != nullptr ? p_state->id : 0;
    const uint64_t input = p_input != nullptr ? p_input->id : 0;
    if (binding_book != nullptr) {
        binding_book->pin(state, repl::Pin::STATE);
        binding_book->pin(input, repl::Pin::INPUT);
    }
    release_pins(*row);
    row->state_binding_id = state;
    row->input_binding_id = input;
}

void NetwPredictionEngine::release_pins(predict::Slot &r_row) {
    const uint64_t state = r_row.state_binding_id;
    const uint64_t input = r_row.input_binding_id;
    r_row.state_binding_id = 0;
    r_row.input_binding_id = 0;
    if (binding_book != nullptr) {
        binding_book->unpin(state, repl::Pin::STATE);
        binding_book->unpin(input, repl::Pin::INPUT);
    }
}

void NetwPredictionEngine::release_bindings() {
    for (predict::Slot *row : rows) {
        if (row != nullptr) {
            release_pins(*row);
        }
    }
}

repl::PropertyBinding *NetwPredictionEngine::state_binding_in(
    const predict::Slot &p_row
) const {
    return binding_book != nullptr ? binding_book->get(p_row.state_binding_id)
                                   : nullptr;
}

repl::PropertyBinding *NetwPredictionEngine::input_binding_in(
    const predict::Slot &p_row
) const {
    return binding_book != nullptr ? binding_book->get(p_row.input_binding_id)
                                   : nullptr;
}

PropertyBindingPtr NetwPredictionEngine::state_binding_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? nullptr : state_binding_in(*row);
}

PropertyBindingPtr NetwPredictionEngine::input_binding_of(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row == nullptr ? nullptr : input_binding_in(*row);
}

bool NetwPredictionEngine::input_lane_open(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->lane_open;
}

bool NetwPredictionEngine::record_authored_command(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label,
    bool p_fresh,
    const predict::StateRow &p_command
) {
    NETW_PREDICT_TICK_SCOPE();
    predict::Slot *row = mutable_row_of(p_slot);
    return row != nullptr && row->lane_open
        && row->record_authored_row(p_transition, p_label, p_fresh, p_command);
}

int64_t NetwPredictionEngine::record_arrived_inputs(
    int64_t p_slot,
    const predict::Arrival &p_arrival,
    bool p_keyed_by_tick
) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row == nullptr || !row->lane_open) {
        return -1;
    }
    int64_t oldest = -1;
    const auto admit = [&](int64_t p_tick) {
        if (p_keyed_by_tick) {
            row->record_authored_row(p_tick, p_tick, true, row->arrived);
        }
        oldest = oldest < 0 ? p_tick : std::min(oldest, p_tick);
    };
    const uint32_t samples = p_arrival.sample_count();
    if (samples == 0) {
        if (p_arrival.tick >= 0
            && p_arrival.read(row->input_codec, row->arrived)) {
            admit(p_arrival.tick);
        }
        return oldest;
    }
    for (uint32_t sample = 0; sample < samples; ++sample) {
        const int64_t tick = p_arrival.sample_tick(sample);
        if (tick >= 0
            && p_arrival.read_sample(row->input_codec, sample, row->arrived)) {
            admit(tick);
        }
    }
    return oldest;
}


bool NetwPredictionEngine::has_authored_input(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->has_authored(p_transition);
}

int64_t NetwPredictionEngine::newest_authored_input(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->lane_open ? row->newest_authored() : -1;
}

void NetwPredictionEngine::note_relayed(int64_t p_slot, int64_t p_transition) {
    predict::Slot *row = mutable_row_of(p_slot);
    if (row != nullptr) {
        row->inputs.note_relayed(p_transition);
    }
}

bool NetwPredictionEngine::relayed_at(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->inputs.relayed_at(p_transition);
}

bool NetwPredictionEngine::input_fresh_at(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->inputs.fresh_at(p_transition);
}

int64_t NetwPredictionEngine::input_label_of(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->inputs.label_of(p_transition) : -1;
}

bool NetwPredictionEngine::command_codes_agree(int64_t p_slot) const {
    const predict::Slot *row = row_of(p_slot);
    const SchemaRecord *schema = input_schema(p_slot);
    if (row == nullptr || schema == nullptr) {
        return false;
    }
    predict::CommandFrameRecord frame;
    return predict::CommandFrameRecord::open(*schema, frame)
        && predict::codes_agree(
            row->input_codec,
            frame.row_plan(),
            *schema,
            input_keys(p_slot)
        );
}

void NetwPredictionEngine::trim_history(int64_t p_slot, int64_t p_ack) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND(
        row == nullptr,
        sys::PREDICTION,
        "Prediction trimmed the history of an entity it no longer predicts "
        "(slot %d).",
        p_slot
    );
    row->trim_history(p_ack);
}

bool NetwPredictionEngine::command_admit(
    int64_t p_slot,
    int64_t p_transition,
    int64_t p_label,
    bool p_fresh,
    const predict::StateRow &p_command
) {
    predict::Slot *row = mutable_row_of(p_slot);
    NETW_ERR_COND_V(
        row == nullptr,
        false,
        sys::PREDICTION,
        "Prediction wrote input for an entity it no longer predicts "
        "(slot %d).",
        p_slot
    );
    return row->admit_command(p_transition, p_label, p_fresh, p_command);
}

bool NetwPredictionEngine::command_has(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->holds_command(p_transition);
}

int64_t NetwPredictionEngine::command_label_of(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->holds_command(p_transition)
        ? row->inputs.label_of(p_transition)
        : -1;
}

bool NetwPredictionEngine::command_is_fresh(
    int64_t p_slot,
    int64_t p_transition
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr && row->holds_command(p_transition)
        && row->inputs.fresh_at(p_transition);
}

int NetwPredictionEngine::command_depth_from(
    int64_t p_slot,
    int64_t p_cursor
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->command_depth_from(p_cursor) : 0;
}

PackedInt64Array NetwPredictionEngine::command_transitions(
    int64_t p_slot
) const {
    const predict::Slot *row = row_of(p_slot);
    return row != nullptr ? row->command_keys() : PackedInt64Array();
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
    values[CURSOR_LATEST_INPUT_TICK] = slot.recorded_input_tick;
    values[CURSOR_LAST_DRIVEN_INPUT_TICK] = slot.drive_input_tick;
    values[CURSOR_LAST_FRAME_TRANSITION_TICK] = slot.drive_frame_tick;
    return out;
}

int64_t NetwPredictionEngine::drive_stat(
    int64_t p_slot,
    int p_column,
    int64_t p_absent
) const {
    const predict::Slot *row = row_of(p_slot);
    if (row == nullptr) {
        return p_absent;
    }
    const predict::DriveStats &stats = row->stats;
    switch (p_column) {
        case STAT_DRIVE_SEQ:
            return stats.drive_seq;
        case STAT_LAST_DRIVE_LABEL:
            return stats.last_drive_label;
        case STAT_LAST_DRIVE_KIND:
            return int64_t(stats.last_drive_kind);
        case STAT_TAPE_EPOCH:
            return stats.tape_epoch;
        case STAT_TAPE_INDEX:
            return stats.tape_index;
        case STAT_ACK_AGE_TICKS:
            return stats.ack_age_ticks;
        case STAT_AUTHORING_CLAMPED:
            return stats.authoring_clamped;
        case STAT_SPECULATION_HELD:
            return stats.speculation_held;
        case STAT_CHAIN_BREAKS:
            return stats.chain_breaks;
        case STAT_QUANTUM_STEPS:
            return stats.quantum_steps;
        case STAT_QUANTUM_DECLARED:
            return stats.quantum_declared;
        case STAT_QUANTUM_FAULTS:
            return stats.quantum_faults;
        case STAT_OWNER_LOST:
            return row->port.lost;
        case STAT_MAX_REPLAY_DEPTH:
            return stats.max_replay_depth;
        default:
            return p_absent;
    }
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

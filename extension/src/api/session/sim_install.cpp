#include "netw/api/netw_multiplayer.hpp"

#include <algorithm>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/display/book.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/compare.hpp"
#include "netw/prediction_core.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr double UNQUANTIZED_EPSILON = 0.0001;
constexpr double NEGLIGIBLE_DELTA = 0.000001;

double epsilon_of(
    const NetwPropertySetBinding *p_binding,
    const StringName &p_key,
    const Variant &p_value
) {
    const double declared = p_binding->epsilon_override_of(p_key);
    if (declared >= 0.0) {
        return declared;
    }
    const Ref<NetwPropertySetColumn> field = p_binding->field_of(p_key);
    const Ref<NetwQuantize> quantizer
        = field.is_valid() ? field->get_quantizer() : Ref<NetwQuantize>();
    if (quantizer.is_null()) {
        return UNQUANTIZED_EPSILON;
    }
    return std::max(
        UNQUANTIZED_EPSILON,
        2.0 * quantizer->max_error(p_value.get_type())
    );
}

bool matches(
    const NetwPropertySetBinding *p_binding,
    Node *p_node,
    const Array &p_keys,
    const Array &p_values
) {
    for (int at = 0; at < p_keys.size(); ++at) {
        const StringName key = p_keys[at];
        const double error
            = predict::value_error(port_get(p_node, key), p_values[at], false);
        if (error > epsilon_of(p_binding, key, p_values[at])) {
            return false;
        }
    }
    return true;
}

Array projected(
    const NetwPropertySetBinding *p_binding,
    const Array &p_keys,
    const Array &p_values,
    double p_seconds
) {
    Dictionary payload;
    Dictionary projection;
    for (int at = 0; at < p_keys.size(); ++at) {
        const StringName key = p_keys[at];
        payload[key] = p_values[at];
        const StringName carry = p_binding->carry_channel_of(key);
        if (carry != StringName()) {
            projection[key] = carry;
        }
    }
    if (projection.is_empty()) {
        return p_values;
    }
    const Dictionary moved
        = prediction_core::project_payload(payload, projection, p_seconds);
    Array out;
    for (int at = 0; at < p_keys.size(); ++at) {
        out.push_back(moved[p_keys[at]]);
    }
    return out;
}

Array gather(Node *p_node, const Array &p_keys) {
    Array out;
    for (int at = 0; at < p_keys.size(); ++at) {
        out.push_back(port_get(p_node, StringName(p_keys[at])));
    }
    return out;
}

} // namespace

bool NetwMultiplayer::sim_takes_install(
    const RID &p_entity,
    const NetwPropertySetBinding *p_binding
) const {
    const sim::Row *row = sim_rows.row_of(p_entity);
    if (row == nullptr || row->facts.predicted || row->mode != sim::Mode::ACTIVE
        || p_binding == nullptr || p_binding->get_set().is_null()) {
        return false;
    }
    const int64_t record = row->facts.state_rows
        ? NetwPropertySet::RECORD_STATE
        : NetwPropertySet::RECORD_BROADCAST;
    return p_binding->get_set()->get_record() == record;
}

Error NetwMultiplayer::sim_admit_install(
    const RID &p_entity,
    const sim::Sample &p_sample,
    bool p_whole
) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row == nullptr || !sim_reconstructs(p_entity, p_sample.comp, p_whole)) {
        return OK;
    }
    sim::Sample arrived = p_sample;
    arrived.tenure = sim_author_tenure(p_entity);
    if (!sim::installs_on_arrival(
            row->declaration.restore,
            arrived.tick,
            sim_display_tick()
        )) {
        sim::hold(row->installs, arrived);
        return OK;
    }
    sim_install(p_entity, arrived);
    return OK;
}

bool NetwMultiplayer::sim_install_image(
    const RID &p_entity,
    NetwPropertySetBinding *p_binding,
    const sim::Sample &p_image
) {
    const sim::Row *row = sim_rows.row_of(p_entity);
    if (row == nullptr || row->facts.predicted || row->facts.state_rows
        || p_binding == nullptr || p_binding->get_set().is_null()
        || p_binding->get_set()->get_record()
            != NetwPropertySet::RECORD_BROADCAST) {
        return false;
    }
    if (row->mode == sim::Mode::ACTIVE) {
        sim::Sample image = p_image;
        image.sender = sim_author_peer(p_entity);
        sim_admit_install(p_entity, image, true);
        return true;
    }
    if (row->mode != sim::Mode::AUTHORITY) {
        return false;
    }
    int64_t age = -1;
    const Array values = sim_install_target(
        p_binding,
        p_image,
        row->declaration.restore,
        int64_t(row->declaration.max_restore_ticks),
        age
    );
    sim_write_install(p_entity, p_binding, p_image.keys, values, age);
    return true;
}

void NetwMultiplayer::sim_drain_installs() {
    const int64_t display = sim_display_tick();
    if (display < 0) {
        return;
    }
    const LocalVector<RID> holding = sim_rows.holding();
    LocalVector<sim::Sample> due;
    for (const RID &entity : holding) {
        sim::Row *row = sim_rows.find(entity);
        if (row == nullptr) {
            continue;
        }
        sim::take_due(row->installs, display, due);
        for (const sim::Sample &sample : due) {
            sim_install(entity, sample);
        }
    }
}

void NetwMultiplayer::sim_install(
    const RID &p_entity,
    const sim::Sample &p_sample
) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row == nullptr) {
        return;
    }
    NetwPropertySetBinding *binding = Object::cast_to<NetwPropertySetBinding>(
        gd::object_of(p_sample.binding)
    );
    Node *node = binding != nullptr ? binding->node() : nullptr;
    if (node == nullptr || row->mode != sim::Mode::ACTIVE
        || p_sample.sender != sim_author_peer(p_entity)
        || p_sample.tenure != sim_author_tenure(p_entity)) {
        row->installs.stats.dropped += 1;
        return;
    }
    if (sim_fenced(p_entity, p_sample.tick)) {
        return;
    }
    int64_t age = -1;
    const Array values = sim_install_target(
        binding,
        p_sample,
        row->declaration.restore,
        int64_t(row->declaration.max_restore_ticks),
        age
    );
    sim_write_install(p_entity, binding, p_sample.keys, values, age);
}

bool NetwMultiplayer::sim_reconstructs(
    const RID &p_entity,
    int64_t p_comp,
    bool p_whole
) {
    return sim::reconstructs(sim_row(p_entity).installs, p_comp, p_whole);
}

void NetwMultiplayer::sim_note_whole(const RID &p_entity, int64_t p_comp) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row != nullptr) {
        sim::reconstructs(row->installs, p_comp, true);
    }
}

void NetwMultiplayer::sim_forget_reconstruction(const RID &p_entity) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row != nullptr) {
        row->installs.reconstructed.clear();
    }
}

Array NetwMultiplayer::sim_install_target(
    const NetwPropertySetBinding *p_binding,
    const sim::Sample &p_sample,
    sim::Restore p_restore,
    int64_t p_max_restore_ticks,
    int64_t &r_age
) const {
    const ClockEngine &clock = clock_engine();
    const int64_t now = clock.get_configured() ? clock.get_tick() : -1;
    r_age = now >= 0 && p_sample.tick >= 0
        ? std::max(int64_t(0), now - p_sample.tick)
        : -1;
    if (p_restore != sim::Restore::EXTRAPOLATED || r_age <= 0
        || p_binding == nullptr) {
        return p_sample.values;
    }
    const int64_t span = std::min(r_age, p_max_restore_ticks);
    return projected(
        p_binding,
        p_sample.keys,
        p_sample.values,
        double(span) * clock.ticktime()
    );
}

bool NetwMultiplayer::sim_write_install(
    const RID &p_entity,
    NetwPropertySetBinding *p_binding,
    const Array &p_keys,
    const Array &p_values,
    int64_t p_age
) {
    Node *node = p_binding != nullptr ? p_binding->node() : nullptr;
    if (node == nullptr) {
        return false;
    }
    sim::Row *row = sim_rows.find(p_entity);
    if (row != nullptr && sim::asleep(row->bodies)
        && matches(p_binding, node, p_keys, p_values)) {
        row->installs.stats.skipped += 1;
        return false;
    }
    const Array before = gather(node, p_keys);
    p_binding->install_values(p_keys, p_values);
    const Array after = gather(node, p_keys);
    Dictionary deltas;
    for (int at = 0; at < p_keys.size(); ++at) {
        const Variant delta
            = prediction_core::pose_delta(after[at], before[at], false);
        if (delta.get_type() != Variant::NIL
            && predict::value_error(after[at], before[at], false)
                > NEGLIGIBLE_DELTA) {
            deltas[p_keys[at]] = delta;
        }
    }
    if (!deltas.is_empty()) {
        display_absorb_recovery(
            display_book->runtime_of(p_entity),
            deltas,
            false
        );
    }
    sim::Row *installed = sim_rows.find(p_entity);
    if (installed != nullptr) {
        sim::note_install(installed->installs.stats, p_age);
    }
    return true;
}

int64_t NetwMultiplayer::sim_author_peer(const RID &p_entity) const {
    const sim::Row *row = sim_rows.row_of(p_entity);
    if (row != nullptr && sim::session_authors(row->facts)) {
        return session_authority_peer();
    }
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    return entity.is_valid() ? entity->get_controller() : 0;
}

uint64_t NetwMultiplayer::sim_author_tenure(const RID &p_entity) const {
    const sim::Row *row = sim_rows.row_of(p_entity);
    if (row != nullptr && sim::session_authors(row->facts)) {
        return 0;
    }
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    return entity.is_valid() ? entity->get_control_tenure() : 0;
}

int64_t NetwMultiplayer::sim_display_tick() const {
    const ClockEngine &clock = clock_engine();
    return clock.get_configured() ? int64_t(clock.display_tick()) : -1;
}

} // namespace netw

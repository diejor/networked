#include "netw/api/netw_multiplayer.hpp"

#include <algorithm>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/prediction.hpp"
#include "netw/display/book.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/compare.hpp"
#include "netw/prediction_core.hpp"
#include "netw/project.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/row.hpp"

using namespace godot;

namespace netw {

using repl::PropertyBinding;
using repl::PropertyColumn;
using repl::PropertySet;

namespace {

constexpr double UNQUANTIZED_EPSILON = 0.0001;
constexpr double NEGLIGIBLE_DELTA = 0.000001;

double epsilon_of(
    const PropertyBinding *p_binding,
    const StringName &p_key,
    const Variant &p_value
) {
    const double declared = p_binding->epsilon_override_of(p_key);
    if (declared >= 0.0) {
        return declared;
    }
    const PropertyColumn *field = p_binding->field_of(p_key);
    const Ref<NetwQuantize> quantizer
        = field != nullptr ? field->get_quantizer() : Ref<NetwQuantize>();
    if (quantizer.is_null()) {
        return UNQUANTIZED_EPSILON;
    }
    return std::max(
        UNQUANTIZED_EPSILON,
        2.0 * quantizer->max_error(p_value.get_type())
    );
}

Array projected(
    const PropertyBinding *p_binding,
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

} // namespace

bool NetwMultiplayer::sim_takes_install(
    const RID &p_entity,
    const PropertyBinding *p_binding
) const {
    const sim::Row *row = sim_rows.row_of(p_entity);
    if (row == nullptr || row->facts.predicted || row->mode != sim::Mode::ACTIVE
        || p_binding == nullptr || p_binding->get_set() == nullptr) {
        return false;
    }
    const int64_t record = row->facts.state_rows
        ? PropertySet::RECORD_STATE
        : PropertySet::RECORD_BROADCAST;
    return p_binding->get_set()->get_record() == record;
}

void NetwMultiplayer::sim_forget_reconstruction(const RID &p_entity) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row != nullptr) {
        row->installs.reconstructed.clear();
    }
}

Array NetwMultiplayer::sim_install_target(
    const PropertyBinding *p_binding,
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

void NetwMultiplayer::sim_install_target_row(
    const PropertyBinding *p_binding,
    int64_t p_tick,
    const LocalVector<StringName> &p_keys,
    const LocalVector<Variant> &p_values,
    sim::Restore p_restore,
    int64_t p_max_restore_ticks,
    LocalVector<Variant> &r_target,
    int64_t &r_age
) const {
    const ClockEngine &clock = clock_engine();
    const int64_t now = clock.get_configured() ? clock.get_tick() : -1;
    r_age = now >= 0 && p_tick >= 0 ? std::max(int64_t(0), now - p_tick) : -1;
    const uint32_t count = p_values.size();
    r_target.resize(count);
    for (uint32_t at = 0; at < count; ++at) {
        r_target[at] = p_values[at];
    }
    if (p_restore != sim::Restore::EXTRAPOLATED || r_age <= 0
        || p_binding == nullptr) {
        return;
    }
    const double seconds
        = double(std::min(r_age, p_max_restore_ticks)) * clock.ticktime();
    if (seconds <= 0.0) {
        return;
    }
    for (uint32_t at = 0; at < count && at < p_keys.size(); ++at) {
        const StringName carry = p_binding->carry_channel_of(p_keys[at]);
        if (carry == StringName()) {
            continue;
        }
        int channel = -1;
        for (uint32_t other = 0; other < p_keys.size(); ++other) {
            if (p_keys[other] == carry) {
                channel = int(other);
                break;
            }
        }
        if (channel < 0 || uint32_t(channel) >= count
            || !project::supports(int(p_values[at].get_type()))) {
            continue;
        }
        r_target[at] = project::forward(
            p_values[at],
            p_values[uint32_t(channel)],
            seconds
        );
    }
}

bool NetwMultiplayer::sim_write_install_row(
    const RID &p_entity,
    PropertyBinding *p_binding,
    const LocalVector<StringName> &p_keys,
    const LocalVector<Variant> &p_values,
    int64_t p_age,
    LocalVector<StringName> &r_moved_keys,
    LocalVector<Variant> &r_moved
) {
    Node *node = p_binding != nullptr ? p_binding->node() : nullptr;
    if (node == nullptr || p_keys.size() != p_values.size()) {
        return false;
    }
    sim::Row *row = sim_rows.find(p_entity);
    if (row != nullptr && sim::asleep(row->bodies)) {
        bool same = true;
        for (uint32_t at = 0; at < p_keys.size() && same; ++at) {
            const double error = predict::value_error(
                port_get(node, p_keys[at]),
                p_values[at],
                false
            );
            same = error <= epsilon_of(p_binding, p_keys[at], p_values[at]);
        }
        if (same) {
            row->installs.stats.skipped += 1;
            return false;
        }
    }
    r_moved_keys.clear();
    r_moved.clear();
    for (uint32_t at = 0; at < p_keys.size(); ++at) {
        const StringName &key = p_keys[at];
        const Variant before = port_get(node, key);
        port_set(node, key, p_values[at]);
        const Variant delta
            = prediction_core::pose_delta(p_values[at], before, false);
        if (delta.get_type() != Variant::NIL
            && predict::value_error(p_values[at], before, false)
                > NEGLIGIBLE_DELTA) {
            r_moved_keys.push_back(key);
            r_moved.push_back(delta);
        }
    }
    if (!r_moved_keys.is_empty()) {
        display_absorb_moved(
            display_book->runtime_of(p_entity),
            r_moved_keys,
            r_moved
        );
    }
    sim::note_install(sim_row(p_entity).installs.stats, p_age);
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

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/colors.hpp"
#include "netw/display/book.hpp"
#include "netw/display/runtime.hpp"
#include "netw/object_port.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/tick_work.hpp"
#include "netw/prediction_core.hpp"
#include "netw/profile.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/repl/property_set.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/row.hpp"
#include "netw/store/port.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw {

using repl::PropertyBinding;
using repl::PropertySet;

namespace {

constexpr double UNQUANTIZED_EPSILON = 0.0001;
constexpr double NEGLIGIBLE_DELTA = 0.000001;
constexpr uint64_t EVERY_COLUMN = ~uint64_t(0);

using store::push_rank;
using store::PUSH_RANKS;

const StringName &apply_rows_seam() {
    static const StringName name("_sync_apply_rows");
    return name;
}

bool masked_out(uint64_t p_mask, uint32_t p_column) {
    return p_column < 64 && ((p_mask >> p_column) & 1) == 0;
}

Variant pushed_value(
    const store::Table &p_table,
    uint32_t p_row,
    int p_age,
    uint32_t p_column
) {
    return p_age < 0 ? p_table.read_value(p_row, p_column)
                     : p_table.history_value(p_row, uint32_t(p_age), p_column);
}

uint32_t row_of_route(const store::Table &p_table, int64_t p_route) {
    for (uint32_t row = 0; row < p_table.row_count(); ++row) {
        if (p_table.routes[row] == p_route) {
            return row;
        }
    }
    return store::Table::NO_SLOT;
}

bool codes_within_step(
    const wire::ColumnPlan &p_slot,
    const wire::CodeRow &p_left,
    const wire::CodeRow &p_right
) {
    for (int element = 0; element < p_slot.stride; ++element) {
        const uint64_t left = p_left.read(p_slot, element);
        const uint64_t right = p_right.read(p_slot, element);
        if ((left > right ? left - right : right - left) > 1) {
            return false;
        }
    }
    return true;
}

} // namespace

bool NetwMultiplayer::store_applies_by_seam() {
    ReplicationCore *plane = get_replication_plane();
    if (plane != nullptr && plane->gate_seam(apply_rows_seam()) != nullptr) {
        return true;
    }
    return overrides_seam(apply_rows_seam());
}

Error NetwMultiplayer::sync_apply_rows_default(
    const RID &p_table,
    const PackedInt64Array &p_routes,
    const Array &p_columns
) {
    const uint32_t index = store_world.table_of(p_table);
    if (index == store::Table::NO_SLOT) {
        return ERR_DOES_NOT_EXIST;
    }
    const store::Table &table = store_world.table(index);
    if (p_columns.size() != int64_t(table.columns.size())) {
        return ERR_INVALID_PARAMETER;
    }
    for (int64_t at = 0; at < p_routes.size(); ++at) {
        const uint32_t row = row_of_route(table, p_routes[at]);
        Object *node = row != store::Table::NO_SLOT
            ? gd::object_of(ObjectID(table.nodes[row]))
            : nullptr;
        if (node == nullptr) {
            continue;
        }
        for (int rank = 0; rank < PUSH_RANKS; ++rank) {
            for (uint32_t column = 0; column < table.columns.size(); ++column) {
                const Array values = p_columns[column];
                if (push_rank(table.columns[column]) != rank
                    || at >= values.size()) {
                    continue;
                }
                port_set(node, table.schema.at(int(column))->key, values[at]);
            }
        }
    }
    return OK;
}

Error NetwMultiplayer::run_apply_rows(
    const RID &p_table,
    const PackedInt64Array &p_routes,
    const Array &p_columns
) {
    NETW_ZONE_NC("session apply rows seam", colors::WIRE);
    ReplicationCore *plane = get_replication_plane();
    Object *seam
        = plane != nullptr ? plane->gate_seam(apply_rows_seam()) : nullptr;
    if (seam != nullptr) {
        return Error(
            int(seam->call(apply_rows_seam(), p_table, p_routes, p_columns))
        );
    }
    Error answered = OK;
    if (GDVIRTUAL_CALL(
            _sync_apply_rows,
            p_table,
            p_routes,
            p_columns,
            answered
        )) {
        return answered;
    }
    return sync_apply_rows_default(p_table, p_routes, p_columns);
}

bool NetwMultiplayer::store_push(
    const store::RowAddress &p_at,
    int p_age,
    uint64_t p_mask,
    display::Runtime *p_absorb,
    const Array *p_values
) {
    store::Table &table = store_world.table(p_at.table);
    const uint32_t row = p_at.row;
    NETW_ZONE_LEAF_F(
        "Store push entity",
        colors::TABLE,
        profile::SUBSYSTEM_ENTITY,
        "entity %lld",
        static_cast<long long>(table.routes[row])
    );
    Object *node = gd::object_of(ObjectID(table.nodes[row]));
    if (node == nullptr) {
        return false;
    }
    table.working.set(row, true);
#if defined(NETW_TESTS)
    store_world.push_work.pushes += 1;
    const int64_t allocated = store::allocations_seen();
#endif
    if (store_applies_by_seam()) {
        PackedInt64Array routes;
        routes.push_back(table.routes[row]);
        Array columns;
        for (uint32_t column = 0; column < table.columns.size(); ++column) {
            Array one;
            one.push_back(
                p_values != nullptr ? (*p_values)[column]
                                    : pushed_value(table, row, p_age, column)
            );
            columns.push_back(one);
        }
        run_apply_rows(table.rid, routes, columns);
        sim_note_push(node);
        return true;
    }
    const bool absorbing = p_absorb != nullptr
        && p_absorb->get_pump_mode() == display::PUMP_CHASE;
    Dictionary deltas;
    for (int rank = 0; rank < PUSH_RANKS; ++rank) {
        for (uint32_t column = 0; column < table.columns.size(); ++column) {
            if (push_rank(table.columns[column]) != rank
                || masked_out(p_mask, column)) {
                continue;
            }
            const StringName &key = table.schema.at(int(column))->key;
            Variant value = p_values != nullptr
                ? (*p_values)[column]
                : pushed_value(table, row, p_age, column);
            if (table.columns[column].value_bytes == 0) {
                value = value.duplicate(true);
            }
            if (!absorbing) {
                port_set(node, key, value);
                continue;
            }
            const Variant before = port_get(node, key);
            port_set(node, key, value);
            const Variant delta
                = prediction_core::pose_delta(value, before, false);
            if (delta.get_type() != Variant::NIL
                && predict::value_error(value, before, false)
                    > NEGLIGIBLE_DELTA) {
                deltas[key] = delta;
            }
        }
    }
    if (!deltas.is_empty()) {
        display_absorb_recovery(p_absorb, deltas, false);
    }
#if defined(NETW_TESTS)
    store_world.push_work.allocations += store::allocations_seen() - allocated;
#endif
    sim_note_push(node);
    return true;
}

bool NetwMultiplayer::store_rests(
    const store::RowAddress &p_at,
    int p_age,
    Node *p_node,
    const PropertyBinding *p_binding
) {
    store::Table &table = store_world.table(p_at.table);
    for (uint32_t column = 0; column < table.columns.size(); ++column) {
        const table::SchemaColumn *declared = table.schema.at(int(column));
        const Variant live = port_get(p_node, declared->key);
        const Variant pushed = pushed_value(table, p_at.row, p_age, column);
        const double declared_epsilon = p_binding != nullptr
            ? p_binding->epsilon_override_of(declared->key)
            : -1.0;
        const wire::ColumnPlan &slot = table.plan.column(column);
        if (declared_epsilon < 0.0 && declared->quantizer.is_valid()
            && !slot.variable) {
            table.landing.clear();
            table.base.clear();
            if (!table.encode(column, live, table.landing)
                || !table.encode(column, pushed, table.base)
                || !codes_within_step(slot, table.landing, table.base)) {
                return false;
            }
            continue;
        }
        const double epsilon
            = declared_epsilon >= 0.0 ? declared_epsilon : UNQUANTIZED_EPSILON;
        if (predict::value_error(live, pushed, false) > epsilon) {
            return false;
        }
    }
    return true;
}

bool NetwMultiplayer::sim_push(
    const RID &p_entity,
    const store::RowAddress &p_at,
    int64_t p_stamp,
    int64_t p_sender,
    uint64_t p_tenure,
    int p_age
) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row == nullptr) {
        return false;
    }
    store::Table &table = store_world.table(p_at.table);
    Node *node
        = Object::cast_to<Node>(gd::object_of(ObjectID(table.nodes[p_at.row])));
    if (node == nullptr || row->mode != sim::Mode::ACTIVE
        || p_sender != sim_author_peer(p_entity)
        || p_tenure != sim_author_tenure(p_entity)) {
        row->installs.stats.dropped += 1;
        return false;
    }
    if (sim_fenced(p_entity, p_stamp)) {
        return false;
    }
    PropertyBinding *binding = binding_book.live(table.owners[p_at.row]);
    if (sim::asleep(row->bodies) && store_rests(p_at, p_age, node, binding)) {
        row->installs.stats.skipped += 1;
        return false;
    }
    const sim::Restore restore = row->declaration.restore;
    const int64_t reach = int64_t(row->declaration.max_restore_ticks);
    const ClockEngine &clock = clock_engine();
    const int64_t now = clock.get_configured() ? clock.get_tick() : -1;
    const int64_t age
        = now >= 0 && p_stamp >= 0 ? (now > p_stamp ? now - p_stamp : 0) : -1;
    display::Runtime *runtime = display_book->runtime_of(p_entity);
    if (restore == sim::Restore::EXTRAPOLATED && age > 0
        && binding != nullptr) {
        sim::Sample sample;
        sample.tick = p_stamp;
        for (uint32_t column = 0; column < table.columns.size(); ++column) {
            sample.keys.push_back(table.schema.at(int(column))->key);
            sample.values.push_back(
                pushed_value(table, p_at.row, p_age, column)
            );
        }
        int64_t projected_age = -1;
        const Array values = sim_install_target(
            binding,
            sample,
            restore,
            reach,
            projected_age
        );
        store_push(p_at, p_age, EVERY_COLUMN, runtime, &values);
    } else {
        store_push(p_at, p_age, EVERY_COLUMN, runtime);
    }
    sim::Row *installed = sim_rows.find(p_entity);
    if (installed != nullptr) {
        sim::note_install(installed->installs.stats, age);
    }
    return true;
}

void NetwMultiplayer::store_received(
    PropertyBinding *p_binding,
    const store::RowAddress &p_at,
    bool p_retained,
    bool p_volatile,
    int64_t p_sender
) {
    store::Table &table = store_world.table(p_at.table);
    const Ref<NetwEntity> entity = wrapper_for_route(p_binding->route);
    if (!p_binding->writes_node(p_retained, entity.ptr())) {
        return;
    }
    const RID rid = entity.is_valid() ? entity->get_rid_handle() : RID();
    if (p_volatile && rid.is_valid() && sim_takes_install(rid, p_binding)) {
        sim::Row *row = sim_rows.find(rid);
        const int64_t tick = table.received_ticks[p_at.row];
        const uint64_t tenure = sim_author_tenure(rid);
        if (sim::installs_on_arrival(
                row->declaration.restore,
                tick,
                sim_display_tick()
            )) {
            sim_push(rid, p_at, tick, p_sender, tenure, -1);
            return;
        }
        row->installs.stats.dropped
            += table.owe_push(p_at.row, tick, p_sender, tenure);
        row->installs.stats.held += 1;
        return;
    }
    store_push(p_at, -1, EVERY_COLUMN, nullptr);
}

void NetwMultiplayer::sim_drain_installs() {
    const int64_t display = sim_display_tick();
    if (display < 0) {
        return;
    }
    NETW_ZONE_NC("Store drain", colors::TABLE);
    for (uint32_t index = 0; index < store_world.table_count(); ++index) {
        store::Table &table = store_world.table(index);
        for (uint32_t word = 0; word < table.due.words.size(); ++word) {
            uint64_t bits = table.due.words[word];
            while (bits != 0) {
                const uint32_t row = word * 64 + store::Bits::lowest(bits);
                bits &= bits - 1;
                int64_t stamp = -1;
                if (row >= table.row_count()
                    || !table.take_due(row, display, stamp)) {
                    continue;
                }
                NETW_ZONE_LEAF_F(
                    "Store drain entity",
                    colors::TABLE,
                    profile::SUBSYSTEM_ENTITY,
                    "entity %lld",
                    static_cast<long long>(table.routes[row])
                );
                const Ref<NetwEntity> entity
                    = wrapper_for_route(table.routes[row]);
                const int age = table.history_at(row, stamp);
                if (entity.is_null() || age < 0) {
                    continue;
                }
                sim_push(
                    entity->get_rid_handle(),
                    store::RowAddress{index, row},
                    stamp,
                    table.due_senders[row],
                    table.due_tenures[row],
                    age
                );
            }
        }
    }
}

PackedByteArray NetwMultiplayer::store_take_image(PropertyBinding *p_binding) {
    if (p_binding == nullptr || p_binding->get_set() == nullptr) {
        return PackedByteArray();
    }
    const LocalVector<repl::PropertyColumn> &fields
        = p_binding->get_set()->columns;
    Array values;
    values.resize(int64_t(fields.size()));
    uint32_t found = 0;
    LocalVector<Variant> live;
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const store::RowAddress at = store_world.row_of(p_binding->id, lane);
        if (!at.valid()) {
            continue;
        }
        if (!store_world.read_live(at, live)) {
            return PackedByteArray();
        }
        const store::Table &table = store_world.table(at.table);
        for (uint32_t column = 0; column < live.size(); ++column) {
            const StringName &key = table.schema.at(int(column))->key;
            for (uint32_t field = 0; field < fields.size(); ++field) {
                if (fields[field].get_key() == key) {
                    values[field] = live[column];
                    found += 1;
                    break;
                }
            }
        }
    }
    if (found != fields.size()) {
        return PackedByteArray();
    }
    return p_binding->write_image(values);
}

void NetwMultiplayer::store_install_image(
    PropertyBinding *p_binding,
    const Array &p_keys,
    const Array &p_values,
    int64_t p_tick
) {
    if (p_binding == nullptr || p_keys.size() != p_values.size()) {
        return;
    }
    const uint64_t owner = p_binding->id;
    store::RowAddress lanes[2];
    for (uint32_t lane = 0; lane < 2; ++lane) {
        lanes[lane] = store_world.row_of(owner, lane);
        if (!lanes[lane].valid()) {
            continue;
        }
        store::Table &table = store_world.table(lanes[lane].table);
        const uint32_t row = lanes[lane].row;
        table.landing
            .assign(table.plan, table.codes_of(row), table.carried_of(row));
        for (uint32_t column = 0; column < table.columns.size(); ++column) {
            const StringName &key = table.schema.at(int(column))->key;
            for (int at = 0; at < p_keys.size(); ++at) {
                if (StringName(p_keys[at]) == key) {
                    table.encode(column, p_values[at], table.landing);
                    break;
                }
            }
        }
        table.land(row, table.landing, p_tick);
    }
    if (!p_binding->writes_node(false)) {
        return;
    }
    const Ref<NetwEntity> entity = wrapper_for_route(p_binding->route);
    const RID rid = entity.is_valid() ? entity->get_rid_handle() : RID();
    const sim::Row *row = rid.is_valid() ? sim_rows.row_of(rid) : nullptr;
    const bool broadcast = p_binding->get_set() != nullptr
        && p_binding->get_set()->get_record() == PropertySet::RECORD_BROADCAST;
    const bool simulated = row != nullptr && !row->facts.predicted
        && !row->facts.state_rows && broadcast;
    if (simulated && row->mode == sim::Mode::ACTIVE && lanes[0].valid()) {
        store::Table &table = store_world.table(lanes[0].table);
        const int64_t author = sim_author_peer(rid);
        const uint64_t tenure = sim_author_tenure(rid);
        if (sim::installs_on_arrival(
                row->declaration.restore,
                p_tick,
                sim_display_tick()
            )) {
            sim_push(rid, lanes[0], p_tick, author, tenure, -1);
        } else {
            table.owe_push(lanes[0].row, p_tick, author, tenure);
        }
        if (lanes[1].valid()) {
            store_push(lanes[1], -1, EVERY_COLUMN, nullptr);
        }
        return;
    }
    display::Runtime *runtime = simulated && row->mode == sim::Mode::AUTHORITY
        ? display_book->runtime_of(rid)
        : nullptr;
    for (uint32_t lane = 0; lane < 2; ++lane) {
        if (lanes[lane].valid()) {
            store_push(lanes[lane], -1, EVERY_COLUMN, runtime);
        }
    }
}

void NetwMultiplayer::store_retain(PropertyBinding *p_binding) {
    Node *node = p_binding != nullptr ? p_binding->node() : nullptr;
    if (node == nullptr) {
        return;
    }
    const ClockEngine &clock = clock_engine();
    const int64_t tick = clock.get_configured() ? clock.get_tick() : 0;
    const uint64_t owner = p_binding->id;
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const store::RowAddress at = store_world.row_of(owner, lane);
        if (!at.valid() || store_world.table(at.table).holds_values(at.row)) {
            continue;
        }
        store::Table &table = store_world.table(at.table);
        table.landing.clear();
        bool read = true;
        for (uint32_t column = 0; read && column < table.columns.size();
             ++column) {
            const StringName &key = table.schema.at(int(column))->key;
            read = gd::has_property(node, key)
                && table.encode(
                    column,
                    port_get(node, key).duplicate(true),
                    table.landing
                );
        }
        if (read) {
            table.land(at.row, table.landing, tick);
        }
    }
}

void NetwMultiplayer::store_reinstall(PropertyBinding *p_binding) {
    if (p_binding == nullptr || !p_binding->write_gate) {
        return;
    }
    const uint64_t owner = p_binding->id;
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const store::RowAddress at = store_world.row_of(owner, lane);
        if (at.valid() && store_world.table(at.table).holds_values(at.row)) {
            store_push(at, -1, EVERY_COLUMN, nullptr);
        }
    }
}

Error NetwMultiplayer::store_write_entity(
    PropertyBinding *p_binding,
    const StringName &p_key,
    int64_t p_route
) {
    const uint64_t owner = p_binding->id;
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const store::RowAddress at = store_world.row_of(owner, lane);
        if (!at.valid()) {
            continue;
        }
        store::Table &table = store_world.table(at.table);
        for (uint32_t column = 0; column < table.columns.size() && column < 64;
             ++column) {
            if (table.schema.at(int(column))->key != p_key) {
                continue;
            }
            table.write_value(at.row, column, p_route);
            store_push(at, -1, uint64_t(1) << column, nullptr);
            return OK;
        }
    }
    return ERR_DOES_NOT_EXIST;
}

} // namespace netw

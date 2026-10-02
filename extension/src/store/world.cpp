#include "netw/store/world.hpp"

using namespace godot;

namespace netw::store {

World::~World() {
    clear();
}

void World::clear() {
    arrays.forget_slots();
    for (uint32_t at = 0; at < tables.size(); ++at) {
        if (tables[at]->rid.is_valid()) {
            table_names.free(tables[at]->rid);
        }
        memdelete(tables[at]);
    }
    tables.clear();
    tables_by_set.clear();
    owner_rows.clear();
    body_rows.clear();
    reprobed.clear();
    residents.clear();
    slot_route.clear();
    slot_epoch.clear();
    slot_gen.clear();
    slot_rows.clear();
    slot_weight.clear();
    slot_marks.clear();
    slot_tenure.clear();
    slot_tenure_tick.clear();
    slot_predict.clear();
    slot_lagcomp.clear();
    free_slots.clear();
    route_slot.clear();
    sides.reset();
    peer_index.clear();
    free_peers.reset();
    lane_rows.clear();
    spare_images.reset();
    receipts = ReceiptCounts();
    live.resize(0);
    stale.resize(0);
    stale_slots.clear();
    slot_stamps.clear();
}

uint32_t World::acquire_slot(int64_t p_route, int64_t p_epoch) {
    uint32_t slot = 0;
    if (!free_slots.is_empty()) {
        slot = free_slots[free_slots.size() - 1];
        free_slots.resize(free_slots.size() - 1);
    } else {
        slot = slot_route.size();
        slot_route.push_back(0);
        slot_epoch.push_back(0);
        slot_gen.push_back(0);
        slot_rows.push_back(0);
        slot_weight.push_back(1.0f);
        slot_marks.push_back(0);
        slot_tenure.push_back(0);
        slot_tenure_tick.push_back(-1);
        slot_predict.push_back(-1);
        slot_lagcomp.push_back(-1);
        grow_slot_bits(slot_route.size());
    }
    slot_route[slot] = p_route;
    slot_epoch[slot] = p_epoch;
    slot_rows[slot] = 0;
    slot_weight[slot] = 1.0f;
    slot_marks[slot] = 0;
    slot_tenure[slot] = 0;
    slot_tenure_tick[slot] = -1;
    slot_predict[slot] = -1;
    slot_lagcomp[slot] = -1;
    route_slot[p_route] = slot;
    fresh_slot_bits(slot);
    return slot;
}

uint32_t World::claim_slot(int64_t p_route, int64_t p_epoch) {
    const EntityRef held = find(p_route, p_epoch);
    const uint32_t slot = held.slot != Table::NO_SLOT
        ? held.slot
        : acquire_slot(p_route, p_epoch);
    slot_rows[slot] += 1;
    return slot;
}

void World::release_slot(uint32_t p_slot) {
    const uint32_t *named = route_slot.getptr(slot_route[p_slot]);
    if (named != nullptr && *named == p_slot) {
        route_slot.erase(slot_route[p_slot]);
    }
    slot_route[p_slot] = 0;
    slot_epoch[p_slot] = 0;
    slot_gen[p_slot] += 1;
    slot_predict[p_slot] = -1;
    slot_lagcomp[p_slot] = -1;
    free_slots.push_back(p_slot);
}

void World::leave_slot(uint32_t p_slot) {
    if (p_slot == Table::NO_SLOT) {
        return;
    }
    slot_rows[p_slot] -= 1;
    if (slot_rows[p_slot] == 0) {
        release_slot(p_slot);
    }
}

int64_t World::note_tenure(
    uint32_t p_slot,
    uint64_t p_tenure,
    int64_t p_tick
) {
    if (p_slot >= slot_tenure.size()) {
        return -1;
    }
    if (slot_tenure[p_slot] != p_tenure) {
        if (slot_tenure_tick[p_slot] >= 0 || slot_tenure[p_slot] != 0) {
            slot_tenure_tick[p_slot] = p_tick;
        }
        slot_tenure[p_slot] = p_tenure;
    }
    return slot_tenure_tick[p_slot];
}

EntityRef World::enter(int64_t p_route, int64_t p_epoch) {
    if (p_route <= 0) {
        return EntityRef();
    }
    return ref_of(claim_slot(p_route, p_epoch));
}

void World::leave(const EntityRef &p_ref) {
    if (holds(p_ref)) {
        leave_slot(p_ref.slot);
    }
}

EntityRef World::find(int64_t p_route, int64_t p_epoch) const {
    const uint32_t *held = route_slot.getptr(p_route);
    if (held != nullptr && slot_epoch[*held] == p_epoch) {
        return ref_of(*held);
    }
    return EntityRef();
}

bool World::holds(const EntityRef &p_ref) const {
    return p_ref.slot < slot_gen.size() && slot_gen[p_ref.slot] == p_ref.gen
        && slot_route[p_ref.slot] != 0;
}

void World::bind_predict(uint32_t p_slot, int64_t p_predict) {
    if (p_slot < slot_predict.size() && slot_route[p_slot] != 0) {
        slot_predict[p_slot] = p_predict;
    }
}

void World::bind_lagcomp(uint32_t p_slot, int64_t p_lagcomp) {
    if (p_slot < slot_lagcomp.size() && slot_route[p_slot] != 0) {
        slot_lagcomp[p_slot] = p_lagcomp;
    }
}

EntityRef World::ref_of(uint32_t p_slot) const {
    EntityRef out;
    if (p_slot < slot_gen.size()) {
        out.slot = p_slot;
        out.gen = slot_gen[p_slot];
    }
    return out;
}

int64_t World::route_of(uint32_t p_slot) const {
    return p_slot < slot_route.size() ? slot_route[p_slot] : 0;
}

int64_t World::epoch_of(uint32_t p_slot) const {
    return p_slot < slot_epoch.size() ? slot_epoch[p_slot] : 0;
}

uint32_t World::live_slot_count() const {
    return slot_route.size() - free_slots.size();
}

uint32_t World::table_for(
    uint64_t p_set,
    Family p_family,
    const table::SchemaRecord &p_schema
) {
    SetTables *known = tables_by_set.getptr(p_set);
    if (known == nullptr) {
        known = &tables_by_set.insert(p_set, SetTables())->value;
    }
    uint32_t &index = known->family[uint32_t(p_family)];
    if (index != Table::NO_SLOT) {
        return index;
    }
    Table *made = memnew(Table);
    made->set_id = p_set;
    made->compile(p_schema, p_family);
    made->size_peers(sides.size());
    index = tables.size();
    TableName name;
    name.index = index;
    made->rid = table_names.make_rid(name);
    tables.push_back(made);
    return index;
}

uint32_t World::table_of(const RID &p_rid) const {
    const TableName *name = table_names.get_or_null(p_rid);
    return name != nullptr ? name->index : Table::NO_SLOT;
}

RowAddress World::add_row(
    uint32_t p_table,
    uint64_t p_owner,
    uint32_t p_lane,
    uint64_t p_node,
    uint8_t p_comp
) {
    Table &held = *tables[p_table];
    RowAddress at;
    at.table = p_table;
    at.row = held.add_row();
    held.owners[at.row] = p_owner;
    held.nodes[at.row] = p_node;
    held.comps[at.row] = p_comp;
    held.lanes[at.row] = uint8_t(p_lane);
    OwnerRows *rows = owner_rows.getptr(p_owner);
    if (rows == nullptr) {
        rows = &owner_rows.insert(p_owner, OwnerRows())->value;
    }
    rows->lane[p_lane] = at;
    return at;
}

bool World::probes(uint64_t p_node) const {
    return body_rows.has(p_node);
}

void World::count_probe(const Table &p_table, uint32_t p_row, bool p_reads) {
    if (p_table.body_columns == 0 || !p_table.scalar) {
        return;
    }
    const uint64_t node = p_table.nodes[p_row];
    uint32_t *probed = body_rows.getptr(node);
    if (p_reads) {
        if (probed != nullptr) {
            *probed += 1;
            return;
        }
        body_rows.insert(node, 1);
        reprobed.push_back(node);
        return;
    }
    if (probed == nullptr) {
        return;
    }
    if (*probed > 1) {
        *probed -= 1;
        return;
    }
    body_rows.erase(node);
    reprobed.push_back(node);
}

RowAddress World::row_of(uint64_t p_owner, uint32_t p_lane) const {
    const OwnerRows *rows = owner_rows.getptr(p_owner);
    return rows != nullptr ? rows->lane[p_lane] : RowAddress();
}

void World::place(const RowAddress &p_at, int64_t p_route, int64_t p_epoch) {
    Table &held = *tables[p_at.table];
    const uint32_t was = held.slots[p_at.row];
    if (was != Table::NO_SLOT && slot_route[was] == p_route
        && slot_epoch[was] == p_epoch) {
        return;
    }
    if (was != Table::NO_SLOT) {
        close_life(p_at);
    }
    if (held.routes[p_at.row] != p_route) {
        forget_lane(p_at);
    }
    uint32_t slot = Table::NO_SLOT;
    if (p_route > 0) {
        slot = claim_slot(p_route, p_epoch);
    }
    held.slots[p_at.row] = slot;
    held.routes[p_at.row] = p_route;
    held.admit_stamps[p_at.row] = 0;
    held.unsourced.set(p_at.row, true);
    leave_slot(was);
}

void World::remove_row(const RowAddress &p_at) {
    if (!p_at.valid()) {
        return;
    }
    Table &held = *tables[p_at.table];
    const uint64_t owner = held.owners[p_at.row];
    const uint32_t lane = held.lanes[p_at.row];
    const uint32_t slot = held.slots[p_at.row];
    if (held.sources[p_at.row] != uint8_t(Source::NONE)) {
        count_probe(held, p_at.row, false);
    }
    close_life(p_at);
    forget_lane(p_at);
    held.touch(p_at.row);
    const uint32_t moved = held.swap_remove(p_at.row);
    if (moved != Table::NO_SLOT) {
        OwnerRows *rows = owner_rows.getptr(held.owners[p_at.row]);
        if (rows != nullptr) {
            rows->lane[held.lanes[p_at.row]] = p_at;
        }
        if ((held.flags[p_at.row] & Table::DECLARED) != 0) {
            lane_rows[lane_key(held, p_at.row)] = p_at;
        }
    }
    OwnerRows *mine = owner_rows.getptr(owner);
    if (mine != nullptr) {
        mine->lane[lane] = RowAddress();
        if (!mine->lane[0].valid() && !mine->lane[1].valid()) {
            owner_rows.erase(owner);
        }
    }
    leave_slot(slot);
}

void World::remove_owner(uint64_t p_owner) {
    for (uint32_t lane = 0; lane < 2; ++lane) {
        remove_row(row_of(p_owner, lane));
    }
}

void World::unsource_all() {
    for (uint32_t at = 0; at < tables.size(); ++at) {
        tables[at]->unsourced.fill();
    }
}

void World::unsource_owner(uint64_t p_owner) {
    for (uint32_t lane = 0; lane < 2; ++lane) {
        const RowAddress at = row_of(p_owner, lane);
        if (at.valid()) {
            tables[at.table]->unsourced.set(at.row, true);
        }
    }
}

#if defined(NETW_PROFILING)
int64_t World::row_bytes() const {
    int64_t bytes = held::vector(slot_route) + held::vector(slot_epoch)
        + held::vector(slot_gen) + held::vector(slot_rows)
        + held::vector(slot_weight) + held::vector(slot_marks)
        + held::vector(slot_tenure) + held::vector(slot_tenure_tick)
        + held::vector(slot_predict) + held::vector(slot_lagcomp)
        + held::vector(free_slots) + held::map(route_slot)
        + held::vector(tables) + held::map(tables_by_set)
        + held::map(owner_rows) + held::map(body_rows) + held::map(lane_rows)
        + held::vector(live.words) + held::vector(stale.words)
        + held::vector(stale_slots) + held::vector(slot_stamps)
        + probe.held_bytes() + held::vector(visiting)
        + residents.held_bytes() + held::vector(misses) + held::set(dozing)
        + held::vector(woken) + held::vector(reprobed) + held::set(watched);
    for (const Table *table : tables) {
        bytes += table->held_bytes();
    }
    return bytes;
}

int64_t World::peer_bytes() const {
    int64_t bytes = held::vector(sides) + held::map(peer_index)
        + held::vector(free_peers);
    for (const PeerSide &side : sides) {
        bytes += held::vector(side.visible.words)
            + held::vector(side.held.words) + held::map(side.requests)
            + held::map(side.tokens) + held::vector(side.refreshes)
            + held::vector(side.due);
    }
    for (const Table *table : tables) {
        bytes += table->peer_bytes();
    }
    return bytes;
}

int64_t World::flight_bytes() const {
    int64_t bytes = held::vector(spare_images);
    for (const wire::CodeRow &image : spare_images) {
        bytes += image.held_bytes();
    }
    for (const PeerSide &side : sides) {
        bytes += held::map_of_vectors(side.flights);
        for (const auto &lane : side.flights) {
            for (const Flight &flight : lane.value) {
                bytes += flight.image.held_bytes();
            }
        }
    }
    return bytes;
}
#endif

} // namespace netw::store

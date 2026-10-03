#include "netw/display/book.hpp"

using namespace godot;

namespace netw::display {

Book::Row &Book::row_for(const RID &p_entity) {
    HashMap<RID, Row>::Iterator found = rows.find(p_entity);
    if (found != rows.end()) {
        return found->value;
    }
    order.push_back(p_entity);
    rows.insert(p_entity, Row());
    return rows[p_entity];
}

Book::~Book() {
    for (KeyValue<RID, Row> &row : rows) {
        if (row.value.runtime != nullptr) {
            memdelete(row.value.runtime);
        }
    }
}

void Book::set_went_dirty(const Callable &callback) {
    went_dirty = callback;
}

PumpStats &Book::get_stats() {
    return stats;
}

const PumpStats &Book::get_stats() const {
    return stats;
}

void Book::clear_dirty(const RID &p_entity) {
    for (uint32_t at = 0; at < dirty.size(); ++at) {
        if (dirty[at] == p_entity) {
            dirty.remove_at(at);
            return;
        }
    }
}

void Book::forget(const RID &p_entity) {
    clear_dirty(p_entity);
    HashMap<RID, Row>::Iterator found = rows.find(p_entity);
    if (found == rows.end()) {
        return;
    }
    if (found->value.route != 0) {
        by_route.erase(found->value.route);
    }
    if (found->value.runtime != nullptr) {
        release(found->value.runtime);
        memdelete(found->value.runtime);
    }
    rows.remove(found);
    for (uint32_t at = 0; at < order.size(); ++at) {
        if (order[at] == p_entity) {
            order.remove_at(at);
            break;
        }
    }
}

void Book::enroll(const RID &p_entity, int64_t p_route) {
    Row &row = row_for(p_entity);
    if (row.route == p_route) {
        return;
    }
    if (row.route != 0) {
        by_route.erase(row.route);
    }
    row.route = p_route;
    if (p_route != 0) {
        by_route[p_route] = p_entity;
    }
}

Runtime *Book::open_runtime(const RID &p_entity) {
    Row &row = row_for(p_entity);
    if (row.runtime == nullptr) {
        row.runtime = memnew(Runtime);
        live.push_back(row.runtime);
        wake(row.runtime);
    }
    return row.runtime;
}

void Book::release(Runtime *p_runtime) {
    const int64_t at = live.find(p_runtime);
    if (at >= 0) {
        live.remove_at(uint32_t(at));
    }
    if (p_runtime->get_pump_listed()) {
        const int64_t listed = pumping.find(p_runtime);
        if (listed >= 0) {
            pumping[uint32_t(listed)] = nullptr;
        }
    }
    if (p_runtime->get_record_listed()) {
        const int64_t listed = recording.find(p_runtime);
        if (listed >= 0) {
            recording[uint32_t(listed)] = nullptr;
        }
    }
}

void Book::wake(Runtime *p_runtime) {
    if (p_runtime == nullptr) {
        return;
    }
    if (p_runtime->channels().is_empty()
        && p_runtime->get_pump_mode() != netw::display::PUMP_UNRESOLVED) {
        return;
    }
    if (!p_runtime->get_pump_listed()) {
        p_runtime->set_pump_listed(true);
        pumping.push_back(p_runtime);
    }
    if (!p_runtime->get_record_listed()) {
        p_runtime->set_record_listed(true);
        recording.push_back(p_runtime);
    }
}

void Book::wake_route(int64_t p_route) {
    wake(runtime_at(p_route));
}

void Book::wake_all() {
    for (uint32_t at = 0; at < live.size(); ++at) {
        wake(live[at]);
    }
}

Runtime *Book::runtime_of(const RID &p_entity) const {
    HashMap<RID, Row>::ConstIterator found = rows.find(p_entity);
    return found != rows.end() ? found->value.runtime : nullptr;
}

Runtime *Book::runtime_at(int64_t p_route) const {
    HashMap<int64_t, RID>::ConstIterator found = by_route.find(p_route);
    return found != by_route.end() ? runtime_of(found->value) : nullptr;
}

LocalVector<Runtime *> Book::runtimes() const {
    LocalVector<Runtime *> out;
    out.resize(live.size());
    for (uint32_t at = 0; at < live.size(); ++at) {
        out[at] = live[at];
    }
    return out;
}

void Book::set_decl(const RID &p_entity, const Decl &p_decl) {
    Row &row = row_for(p_entity);
    row.decl = p_decl;
    if (row.runtime != nullptr) {
        row.runtime->set_config(row.decl);
        wake(row.runtime);
    }
}

Decl Book::decl_of(const RID &p_entity) const {
    HashMap<RID, Row>::ConstIterator found = rows.find(p_entity);
    return found != rows.end() ? found->value.decl : Decl();
}

Decl *Book::decl_ptr(const RID &p_entity) {
    HashMap<RID, Row>::Iterator found = rows.find(p_entity);
    return found != rows.end() ? &found->value.decl : nullptr;
}

RID Book::entity_at(int64_t p_route) const {
    HashMap<int64_t, RID>::ConstIterator found = by_route.find(p_route);
    return found != by_route.end() ? found->value : RID();
}

int64_t Book::route_of(const RID &p_entity) const {
    HashMap<RID, Row>::ConstIterator found = rows.find(p_entity);
    return found != rows.end() ? found->value.route : 0;
}

void Book::mark_dirty(const RID &p_entity, int p_dirt) {
    if (!p_entity.is_valid() || p_dirt == netw::display::DIRT_NONE) {
        return;
    }
    if (p_dirt == netw::display::DIRT_ROLE) {
        if (went_dirty.is_valid()) {
            went_dirty.call(p_entity, p_dirt);
        }
        return;
    }
    if (is_dirty(p_entity)) {
        return;
    }
    Runtime *runtime = runtime_of(p_entity);
    if (runtime != nullptr) {
        runtime->set_rebuild_queued(true);
    }
    dirty.push_back(p_entity);
    if (went_dirty.is_valid()) {
        went_dirty.call(p_entity, p_dirt);
    }
}

int Book::write_param(
    const RID &p_entity,
    Decl &p_fallback,
    int p_param,
    const Variant &p_value
) {
    Decl *decl = decl_ptr(p_entity);
    if (decl == nullptr) {
        if (p_entity.is_valid()) {
            set_decl(p_entity, p_fallback);
            decl = decl_ptr(p_entity);
        } else {
            decl = &p_fallback;
        }
    }
    const int dirt = decl->set_param(p_param, p_value);
    Runtime *runtime = runtime_of(p_entity);
    if (runtime != nullptr) {
        runtime->set_config(*decl);
        wake(runtime);
    }
    mark_dirty(p_entity, dirt);
    return dirt;
}

bool Book::is_dirty(const RID &p_entity) const {
    for (uint32_t at = 0; at < dirty.size(); ++at) {
        if (dirty[at] == p_entity) {
            return true;
        }
    }
    return false;
}

TypedArray<RID> Book::take_dirty() {
    TypedArray<RID> out;
    for (uint32_t at = 0; at < dirty.size(); ++at) {
        out.push_back(dirty[at]);
    }
    dirty.clear();
    return out;
}

void Book::drop(const RID &p_entity) {
    forget(p_entity);
}

void Book::drop_route(int64_t p_route) {
    HashMap<int64_t, RID>::ConstIterator found = by_route.find(p_route);
    if (found == by_route.end()) {
        return;
    }
    forget(found->value);
}

void Book::clear() {
    for (KeyValue<RID, Row> &row : rows) {
        if (row.value.runtime != nullptr) {
            memdelete(row.value.runtime);
        }
    }
    rows.clear();
    by_route.clear();
    order.clear();
    dirty.clear();
    live.clear();
    pumping.clear();
    recording.clear();
}

int Book::size() const {
    return int(rows.size());
}

} // namespace netw::display

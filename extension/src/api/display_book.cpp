#include "netw/api/display_book.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

const char *SIG_WENT_DIRTY = "went_dirty";

NetwDisplayBook::Row &NetwDisplayBook::row_for(const RID &p_entity) {
    HashMap<RID, Row>::Iterator found = rows.find(p_entity);
    if (found != rows.end()) {
        return found->value;
    }
    order.push_back(p_entity);
    rows.insert(p_entity, Row());
    return rows[p_entity];
}

NetwDisplayBook::NetwDisplayBook() {
    stats.instantiate();
}

Ref<NetwPumpStats> NetwDisplayBook::get_stats() const {
    return stats;
}

void NetwDisplayBook::set_stats(const Ref<NetwPumpStats> &p_stats) {
    stats = p_stats;
}

void NetwDisplayBook::clear_dirty(const RID &p_entity) {
    for (uint32_t at = 0; at < dirty.size(); ++at) {
        if (dirty[at] == p_entity) {
            dirty.remove_at(at);
            return;
        }
    }
}

void NetwDisplayBook::forget(const RID &p_entity) {
    clear_dirty(p_entity);
    HashMap<RID, Row>::Iterator found = rows.find(p_entity);
    if (found == rows.end()) {
        return;
    }
    if (found->value.route != 0) {
        by_route.erase(found->value.route);
    }
    rows.remove(found);
    for (uint32_t at = 0; at < order.size(); ++at) {
        if (order[at] == p_entity) {
            order.remove_at(at);
            break;
        }
    }
}

void NetwDisplayBook::enroll(const RID &p_entity, int64_t p_route) {
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

void NetwDisplayBook::set_runtime(
    const RID &p_entity,
    const Ref<NetwDisplayRuntime> &p_runtime
) {
    row_for(p_entity).runtime = p_runtime;
}

Ref<NetwDisplayRuntime> NetwDisplayBook::runtime_of(const RID &p_entity) const {
    HashMap<RID, Row>::ConstIterator found = rows.find(p_entity);
    return found != rows.end() ? found->value.runtime
                               : Ref<NetwDisplayRuntime>();
}

Ref<NetwDisplayRuntime> NetwDisplayBook::runtime_at(int64_t p_route) const {
    HashMap<int64_t, RID>::ConstIterator found = by_route.find(p_route);
    return found != by_route.end() ? runtime_of(found->value)
                                   : Ref<NetwDisplayRuntime>();
}

TypedArray<NetwDisplayRuntime> NetwDisplayBook::runtimes() const {
    TypedArray<NetwDisplayRuntime> out;
    for (uint32_t at = 0; at < order.size(); ++at) {
        HashMap<RID, Row>::ConstIterator found = rows.find(order[at]);
        if (found != rows.end() && found->value.runtime.is_valid()) {
            out.push_back(found->value.runtime);
        }
    }
    return out;
}

void NetwDisplayBook::set_decl(
    const RID &p_entity,
    const Ref<NetwDisplayDecl> &p_decl
) {
    row_for(p_entity).decl = p_decl;
}

Ref<NetwDisplayDecl> NetwDisplayBook::decl_of(const RID &p_entity) const {
    HashMap<RID, Row>::ConstIterator found = rows.find(p_entity);
    return found != rows.end() ? found->value.decl : Ref<NetwDisplayDecl>();
}

RID NetwDisplayBook::entity_at(int64_t p_route) const {
    HashMap<int64_t, RID>::ConstIterator found = by_route.find(p_route);
    return found != by_route.end() ? found->value : RID();
}

int64_t NetwDisplayBook::route_of(const RID &p_entity) const {
    HashMap<RID, Row>::ConstIterator found = rows.find(p_entity);
    return found != rows.end() ? found->value.route : 0;
}

void NetwDisplayBook::mark_dirty(const RID &p_entity, int p_dirt) {
    if (!p_entity.is_valid() || p_dirt == NetwDisplayDecl::DIRT_NONE) {
        return;
    }
    if (p_dirt == NetwDisplayDecl::DIRT_ROLE) {
        emit_signal(SIG_WENT_DIRTY, p_entity, p_dirt);
        return;
    }
    if (is_dirty(p_entity)) {
        return;
    }
    Ref<NetwDisplayRuntime> runtime = runtime_of(p_entity);
    if (runtime.is_valid()) {
        runtime->set_rebuild_queued(true);
    }
    dirty.push_back(p_entity);
    emit_signal(SIG_WENT_DIRTY, p_entity, p_dirt);
}

int NetwDisplayBook::write_param(
    const RID &p_entity,
    const Ref<NetwDisplayDecl> &p_decl,
    int p_param,
    const Variant &p_value
) {
    Ref<NetwDisplayDecl> decl = decl_of(p_entity);
    if (decl.is_null()) {
        decl = p_decl;
        if (decl.is_null()) {
            return NetwDisplayDecl::DIRT_NONE;
        }
        if (p_entity.is_valid()) {
            set_decl(p_entity, decl);
        }
    }
    const int dirt = decl->set_param(p_param, p_value);
    mark_dirty(p_entity, dirt);
    return dirt;
}

bool NetwDisplayBook::is_dirty(const RID &p_entity) const {
    for (uint32_t at = 0; at < dirty.size(); ++at) {
        if (dirty[at] == p_entity) {
            return true;
        }
    }
    return false;
}

TypedArray<RID> NetwDisplayBook::take_dirty() {
    TypedArray<RID> out;
    for (uint32_t at = 0; at < dirty.size(); ++at) {
        out.push_back(dirty[at]);
    }
    dirty.clear();
    return out;
}

void NetwDisplayBook::drop(const RID &p_entity) {
    forget(p_entity);
}

void NetwDisplayBook::drop_route(int64_t p_route) {
    HashMap<int64_t, RID>::ConstIterator found = by_route.find(p_route);
    if (found == by_route.end()) {
        return;
    }
    forget(found->value);
}

void NetwDisplayBook::clear() {
    rows.clear();
    by_route.clear();
    order.clear();
    dirty.clear();
}

int NetwDisplayBook::size() const {
    return int(rows.size());
}

void NetwDisplayBook::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("enroll", "entity", "route"),
        &NetwDisplayBook::enroll
    );
    ClassDB::bind_method(
        D_METHOD("set_runtime", "entity", "runtime"),
        &NetwDisplayBook::set_runtime
    );
    ClassDB::bind_method(
        D_METHOD("runtime_of", "entity"),
        &NetwDisplayBook::runtime_of
    );
    ClassDB::bind_method(
        D_METHOD("runtime_at", "route"),
        &NetwDisplayBook::runtime_at
    );
    ClassDB::bind_method(D_METHOD("runtimes"), &NetwDisplayBook::runtimes);
    ClassDB::bind_method(
        D_METHOD("set_decl", "entity", "decl"),
        &NetwDisplayBook::set_decl
    );
    ClassDB::bind_method(
        D_METHOD("decl_of", "entity"),
        &NetwDisplayBook::decl_of
    );
    ClassDB::bind_method(
        D_METHOD("entity_at", "route"),
        &NetwDisplayBook::entity_at
    );
    ClassDB::bind_method(
        D_METHOD("route_of", "entity"),
        &NetwDisplayBook::route_of
    );
    ClassDB::bind_method(D_METHOD("drop", "entity"), &NetwDisplayBook::drop);
    ClassDB::bind_method(
        D_METHOD("drop_route", "route"),
        &NetwDisplayBook::drop_route
    );
    ClassDB::bind_method(
        D_METHOD("mark_dirty", "entity", "dirt"),
        &NetwDisplayBook::mark_dirty,
        DEFVAL(int(NetwDisplayDecl::DIRT_RUNTIME))
    );
    ClassDB::bind_method(
        D_METHOD("write_param", "entity", "decl", "param", "value"),
        &NetwDisplayBook::write_param
    );
    ClassDB::bind_method(
        D_METHOD("is_dirty", "entity"),
        &NetwDisplayBook::is_dirty
    );
    ClassDB::bind_method(
        D_METHOD("clear_dirty", "entity"),
        &NetwDisplayBook::clear_dirty
    );
    ClassDB::bind_method(D_METHOD("take_dirty"), &NetwDisplayBook::take_dirty);
    ClassDB::bind_method(D_METHOD("clear"), &NetwDisplayBook::clear);
    ClassDB::bind_method(D_METHOD("size"), &NetwDisplayBook::size);
    ClassDB::bind_method(D_METHOD("get_stats"), &NetwDisplayBook::get_stats);
    ClassDB::bind_method(
        D_METHOD("set_stats", "stats"),
        &NetwDisplayBook::set_stats
    );

    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "stats",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwPumpStats"
        ),
        "set_stats",
        "get_stats"
    );

    ADD_SIGNAL(MethodInfo(
        SIG_WENT_DIRTY,
        PropertyInfo(Variant::RID, "entity"),
        PropertyInfo(Variant::INT, "dirt")
    ));
}

} // namespace netw

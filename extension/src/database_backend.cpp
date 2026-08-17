#include "netw/database_backend.hpp"
#include "godot/class_db.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;
using namespace netw;

namespace netw {

namespace {

Ref<NetwPromise> answered(const Ref<NetwPromise> &p_ret, const char *p_verb) {
    if (p_ret.is_valid()) {
        return p_ret;
    }
    NETW_WARN(
        sys::SESSION,
        "a backend's %s override answered no promise, so the operation has "
        "no result to settle",
        p_verb
    );
    return NetwPromise::rejected(int(ERR_INVALID_DATA), String(p_verb));
}

} // namespace

void NetwDatabaseBackend::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("initialize", "schema", "slot"),
        &NetwDatabaseBackend::initialize,
        DEFVAL("")
    );
    ClassDB::bind_method(
        D_METHOD("upsert", "table", "id", "data"),
        &NetwDatabaseBackend::upsert
    );
    ClassDB::bind_method(
        D_METHOD("commit", "operations"),
        &NetwDatabaseBackend::commit,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("find_by_id", "table", "id"),
        &NetwDatabaseBackend::find_by_id
    );
    ClassDB::bind_method(
        D_METHOD("find_all", "table", "filter"),
        &NetwDatabaseBackend::find_all,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("erase", "table", "id"),
        &NetwDatabaseBackend::erase
    );
    ClassDB::bind_method(
        D_METHOD("warm", "directives"),
        &NetwDatabaseBackend::warm,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("list_namespaces"),
        &NetwDatabaseBackend::list_namespaces
    );
    ClassDB::bind_method(
        D_METHOD("delete_namespace", "slot"),
        &NetwDatabaseBackend::delete_namespace
    );

    GDVIRTUAL_BIND(_initialize, "schema", "slot");
    GDVIRTUAL_BIND(_upsert, "table", "id", "data");
    GDVIRTUAL_BIND(_commit, "operations");
    GDVIRTUAL_BIND(_find_by_id, "table", "id");
    GDVIRTUAL_BIND(_find_all, "table", "filter");
    GDVIRTUAL_BIND(_delete, "table", "id");
    GDVIRTUAL_BIND(_warm, "directives");
    GDVIRTUAL_BIND(_list_namespaces);
    GDVIRTUAL_BIND(_delete_namespace, "slot");
}

Ref<NetwPromise> NetwDatabaseBackend::initialize(
    const Dictionary &schema,
    const String &slot
) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_initialize, schema, slot, ret)) {
        return answered(ret, "_initialize");
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackend::upsert(
    const StringName &table,
    const StringName &id,
    const Dictionary &data
) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_upsert, table, id, data, ret)) {
        return answered(ret, "_upsert");
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackend::commit(const Array &operations) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_commit, operations, ret)) {
        return answered(ret, "_commit");
    }
    for (int i = 0; i < operations.size(); ++i) {
        Dictionary entry = operations[i];
        StringName table = entry.get("table", StringName());
        StringName id = entry.get("id", StringName());
        Dictionary data = entry.get("data", Dictionary());
        const Ref<NetwPromise> wrote = upsert(table, id, data);
        if (wrote.is_null() || !wrote->get_is_settled()) {
            NETW_WARN(
                sys::SESSION,
                "a backend whose writes settle later owes its own _commit to "
                "sequence them; this one left operation %d in flight",
                i
            );
            return NetwPromise::rejected(
                int(ERR_UNAVAILABLE),
                String("_commit")
            );
        }
        if (wrote->get_is_failed()) {
            return wrote;
        }
        const Variant err = wrote->get_result();
        if (err.get_type() == Variant::INT && Error(int(err)) != OK) {
            return wrote;
        }
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackend::find_by_id(
    const StringName &table,
    const StringName &id
) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_find_by_id, table, id, ret)) {
        return answered(ret, "_find_by_id");
    }
    return NetwPromise::resolved(Dictionary());
}

Ref<NetwPromise> NetwDatabaseBackend::find_all(
    const StringName &table,
    const Dictionary &filter
) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_find_all, table, filter, ret)) {
        return answered(ret, "_find_all");
    }
    return NetwPromise::resolved(TypedArray<Dictionary>());
}

Ref<NetwPromise> NetwDatabaseBackend::erase(
    const StringName &table,
    const StringName &id
) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_delete, table, id, ret)) {
        return answered(ret, "_delete");
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackend::warm(const Array &directives) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_warm, directives, ret)) {
        return answered(ret, "_warm");
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackend::list_namespaces() {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_list_namespaces, ret)) {
        return answered(ret, "_list_namespaces");
    }
    return NetwPromise::resolved(TypedArray<StringName>());
}

Ref<NetwPromise> NetwDatabaseBackend::delete_namespace(const String &slot) {
    Ref<NetwPromise> ret;
    if (GDVIRTUAL_CALL(_delete_namespace, slot, ret)) {
        return answered(ret, "_delete_namespace");
    }
    return NetwPromise::resolved(OK);
}

// In-memory test backend

void NetwDatabaseBackendDict::_bind_methods() {}

Dictionary NetwDatabaseBackendDict::get_ns() {
    if (!data.has(ns)) {
        data[ns] = Dictionary();
    }
    return data[ns];
}

bool NetwDatabaseBackendDict::matches_filter(
    const Dictionary &record,
    const Dictionary &filter
) {
    Array keys = filter.keys();
    for (int i = 0; i < keys.size(); ++i) {
        Variant k = keys[i];
        if (!record.has(k) || record[k] != filter[k]) {
            return false;
        }
    }
    return true;
}

Ref<NetwPromise> NetwDatabaseBackendDict::initialize(
    const Dictionary &schema,
    const String &slot
) {
    ns = slot;
    get_ns();
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackendDict::upsert(
    const StringName &table,
    const StringName &id,
    const Dictionary &record_data
) {
    Dictionary active_ns = get_ns();
    if (!active_ns.has(table)) {
        active_ns[table] = Dictionary();
    }
    Dictionary table_map = active_ns[table];
    if (!table_map.has(id)) {
        table_map[id] = Dictionary();
    }
    Dictionary rec = table_map[id];
    Array keys = record_data.keys();
    for (int i = 0; i < keys.size(); ++i) {
        Variant k = keys[i];
        rec[k] = record_data[k];
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackendDict::find_by_id(
    const StringName &table,
    const StringName &id
) {
    Dictionary active_ns = get_ns();
    if (active_ns.has(table)) {
        Dictionary table_map = active_ns[table];
        if (table_map.has(id)) {
            Dictionary rec = table_map[id];
            return NetwPromise::resolved(rec.duplicate());
        }
    }
    return NetwPromise::resolved(Dictionary());
}

Ref<NetwPromise> NetwDatabaseBackendDict::find_all(
    const StringName &table,
    const Dictionary &filter
) {
    TypedArray<Dictionary> results;
    Dictionary active_ns = get_ns();
    if (!active_ns.has(table)) {
        return NetwPromise::resolved(results);
    }
    Dictionary table_map = active_ns[table];
    Array ids = table_map.keys();
    for (int i = 0; i < ids.size(); ++i) {
        Dictionary rec = table_map[ids[i]];
        if (matches_filter(rec, filter)) {
            results.append(rec.duplicate());
        }
    }
    return NetwPromise::resolved(results);
}

Ref<NetwPromise> NetwDatabaseBackendDict::erase(
    const StringName &table,
    const StringName &id
) {
    Dictionary active_ns = get_ns();
    if (active_ns.has(table)) {
        Dictionary table_map = active_ns[table];
        table_map.erase(id);
    }
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseBackendDict::list_namespaces() {
    TypedArray<StringName> out;
    Array keys = data.keys();
    for (int i = 0; i < keys.size(); ++i) {
        out.append(StringName(keys[i]));
    }
    return NetwPromise::resolved(out);
}

Ref<NetwPromise> NetwDatabaseBackendDict::delete_namespace(const String &slot) {
    data.erase(slot);
    return NetwPromise::resolved(OK);
}

} // namespace netw

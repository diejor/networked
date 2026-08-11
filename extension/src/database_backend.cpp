#include "netw/database_backend.hpp"
#include "godot/class_db.hpp"

using namespace godot;
using namespace netw;

namespace netw {

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

Variant NetwDatabaseBackend::initialize(
    const Dictionary &schema,
    const String &slot
) {
    Variant ret;
    if (GDVIRTUAL_CALL(_initialize, schema, slot, ret)) {
        return ret;
    }
    return OK;
}

Variant NetwDatabaseBackend::upsert(
    const StringName &table,
    const StringName &id,
    const Dictionary &data
) {
    Variant ret;
    if (GDVIRTUAL_CALL(_upsert, table, id, data, ret)) {
        return ret;
    }
    return OK;
}

Variant NetwDatabaseBackend::commit(const Array &operations) {
    Variant ret;
    if (GDVIRTUAL_CALL(_commit, operations, ret)) {
        return ret;
    }
    for (int i = 0; i < operations.size(); ++i) {
        Dictionary entry = operations[i];
        StringName table = entry.get("table", StringName());
        StringName id = entry.get("id", StringName());
        Dictionary data = entry.get("data", Dictionary());
        Variant err = upsert(table, id, data);
        // An asynchronous upsert answers with a coroutine rather than a code,
        // and a backend whose writes await owes its own _commit to sequence
        // them. The fallback loop reports only what it can read.
        if (err.get_type() == Variant::INT && (Error)(int)err != OK) {
            return err;
        }
    }
    return OK;
}

Variant NetwDatabaseBackend::find_by_id(
    const StringName &table,
    const StringName &id
) {
    Variant ret;
    if (GDVIRTUAL_CALL(_find_by_id, table, id, ret)) {
        return ret;
    }
    return Dictionary();
}

Variant NetwDatabaseBackend::find_all(
    const StringName &table,
    const Dictionary &filter
) {
    Variant ret;
    if (GDVIRTUAL_CALL(_find_all, table, filter, ret)) {
        return ret;
    }
    return TypedArray<Dictionary>();
}

Variant NetwDatabaseBackend::erase(
    const StringName &table,
    const StringName &id
) {
    Variant ret;
    if (GDVIRTUAL_CALL(_delete, table, id, ret)) {
        return ret;
    }
    return OK;
}

Variant NetwDatabaseBackend::warm(const Array &directives) {
    Variant ret;
    if (GDVIRTUAL_CALL(_warm, directives, ret)) {
        return ret;
    }
    return OK;
}

Variant NetwDatabaseBackend::list_namespaces() {
    Variant ret;
    if (GDVIRTUAL_CALL(_list_namespaces, ret)) {
        return ret;
    }
    return TypedArray<StringName>();
}

Variant NetwDatabaseBackend::delete_namespace(const String &slot) {
    Variant ret;
    if (GDVIRTUAL_CALL(_delete_namespace, slot, ret)) {
        return ret;
    }
    return OK;
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

Variant NetwDatabaseBackendDict::initialize(
    const Dictionary &schema,
    const String &slot
) {
    ns = slot;
    get_ns();
    return OK;
}

Variant NetwDatabaseBackendDict::upsert(
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
    return OK;
}

Variant NetwDatabaseBackendDict::find_by_id(
    const StringName &table,
    const StringName &id
) {
    Dictionary active_ns = get_ns();
    if (active_ns.has(table)) {
        Dictionary table_map = active_ns[table];
        if (table_map.has(id)) {
            Dictionary rec = table_map[id];
            return rec.duplicate();
        }
    }
    return Dictionary();
}

Variant NetwDatabaseBackendDict::find_all(
    const StringName &table,
    const Dictionary &filter
) {
    TypedArray<Dictionary> results;
    Dictionary active_ns = get_ns();
    if (!active_ns.has(table)) {
        return results;
    }
    Dictionary table_map = active_ns[table];
    Array ids = table_map.keys();
    for (int i = 0; i < ids.size(); ++i) {
        Dictionary rec = table_map[ids[i]];
        if (matches_filter(rec, filter)) {
            results.append(rec.duplicate());
        }
    }
    return results;
}

Variant NetwDatabaseBackendDict::erase(
    const StringName &table,
    const StringName &id
) {
    Dictionary active_ns = get_ns();
    if (active_ns.has(table)) {
        Dictionary table_map = active_ns[table];
        table_map.erase(id);
    }
    return OK;
}

Variant NetwDatabaseBackendDict::list_namespaces() {
    TypedArray<StringName> out;
    Array keys = data.keys();
    for (int i = 0; i < keys.size(); ++i) {
        out.append(StringName(keys[i]));
    }
    return out;
}

Variant NetwDatabaseBackendDict::delete_namespace(const String &slot) {
    data.erase(slot);
    return OK;
}

} // namespace netw

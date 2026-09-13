#include "netw/database_backend_dict.hpp"
#include "godot/class_db.hpp"

using namespace godot;
using namespace netw;

namespace netw {

Dictionary DatabaseBackendDict::get_ns() {
    if (!data.has(ns)) {
        data[ns] = Dictionary();
    }
    return data[ns];
}

bool DatabaseBackendDict::matches_filter(
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

Ref<NetwPromise> DatabaseBackendDict::initialize(
    const Dictionary &schema,
    const String &slot
) {
    ns = slot;
    get_ns();
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> DatabaseBackendDict::upsert(
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

Ref<NetwPromise> DatabaseBackendDict::find_by_id(
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

Ref<NetwPromise> DatabaseBackendDict::find_all(
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

Ref<NetwPromise> DatabaseBackendDict::erase(
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

Ref<NetwPromise> DatabaseBackendDict::list_namespaces() {
    TypedArray<StringName> out;
    Array keys = data.keys();
    for (int i = 0; i < keys.size(); ++i) {
        out.append(StringName(keys[i]));
    }
    return NetwPromise::resolved(out);
}

Ref<NetwPromise> DatabaseBackendDict::delete_namespace(const String &slot) {
    data.erase(slot);
    return NetwPromise::resolved(OK);
}

} // namespace netw

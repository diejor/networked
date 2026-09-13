#include "netw/api/database_backend.hpp"
#include "godot/class_db.hpp"
#include "netw/database_backend_dict.hpp"
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
    return NetwPromise::rejected(ERR_INVALID_DATA, String(p_verb));
}

} // namespace

Ref<NetwDatabaseBackend> NetwDatabaseBackend::in_memory() {
    return Ref<NetwDatabaseBackend>(memnew(DatabaseBackendDict));
}

void NetwDatabaseBackend::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwDatabaseBackend",
        D_METHOD("in_memory"),
        &NetwDatabaseBackend::in_memory
    );
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
            return NetwPromise::rejected(ERR_UNAVAILABLE, String("_commit"));
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

} // namespace netw

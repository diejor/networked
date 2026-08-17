#include "netw/sync_model.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

namespace {

Ref<NetwSyncSetRow> wrap(const repl::SetRow *p_row) {
    Ref<NetwSyncSetRow> out;
    if (p_row == nullptr) {
        return out;
    }
    out.instantiate();
    out->row = *p_row;
    return out;
}

} // namespace

void NetwSyncSetRow::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_route"), &NetwSyncSetRow::get_route);
    ClassDB::bind_method(D_METHOD("get_ordinal"), &NetwSyncSetRow::get_ordinal);
    ClassDB::bind_method(D_METHOD("get_comp"), &NetwSyncSetRow::get_comp);
    ClassDB::bind_method(D_METHOD("get_kind"), &NetwSyncSetRow::get_kind);
    ClassDB::bind_method(D_METHOD("get_key"), &NetwSyncSetRow::get_key);
    ClassDB::bind_method(D_METHOD("get_set"), &NetwSyncSetRow::get_set);
    ClassDB::bind_method(D_METHOD("get_record"), &NetwSyncSetRow::get_record);
    ClassDB::bind_method(
        D_METHOD("get_schema_hash"),
        &NetwSyncSetRow::get_schema_hash
    );
    ClassDB::bind_method(D_METHOD("get_policy"), &NetwSyncSetRow::get_policy);
    ClassDB::bind_method(
        D_METHOD("get_audience"),
        &NetwSyncSetRow::get_audience
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "route"), "", "get_route");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "ordinal"), "", "get_ordinal");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "comp"), "", "get_comp");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "kind"), "", "get_kind");
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "key"), "", "get_key");
    ADD_PROPERTY(PropertyInfo(Variant::RID, "set"), "", "get_set");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "record"), "", "get_record");
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "schema_hash"),
        "",
        "get_schema_hash"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "policy"), "", "get_policy");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "audience"), "", "get_audience");
}

int64_t NetwSyncModel::declare(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_comp,
    const RID &p_set,
    int64_t p_record,
    int64_t p_schema_hash,
    int64_t p_policy,
    int64_t p_audience
) {
    return impl.declare(
        p_route,
        p_kind,
        p_key,
        p_comp,
        p_set,
        p_record,
        p_schema_hash,
        p_policy,
        p_audience
    );
}

void NetwSyncModel::drop(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    impl.drop(p_route, p_kind, p_key, p_record);
}

Ref<NetwSyncSetRow> NetwSyncModel::row(int64_t p_route, int64_t p_ordinal)
    const {
    return wrap(impl.row(p_route, p_ordinal));
}

Ref<NetwSyncSetRow> NetwSyncModel::row_for(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) const {
    return wrap(impl.row_for(p_route, p_kind, p_key, p_record));
}

TypedArray<NetwSyncSetRow> NetwSyncModel::route_rows(int64_t p_route) const {
    TypedArray<NetwSyncSetRow> out;
    const LocalVector<repl::SetRow> *rows = impl.route_rows(p_route);
    if (rows == nullptr) {
        return out;
    }
    for (const repl::SetRow &row : *rows) {
        out.push_back(wrap(&row));
    }
    return out;
}

bool NetwSyncModel::authors(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_local_id,
    bool p_node_authority,
    int64_t p_controller
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return false;
    }
    return repl::row_authors(
        *found,
        p_local_id,
        p_node_authority,
        p_controller
    );
}

PackedInt32Array NetwSyncModel::recipients(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_local_id,
    const PackedInt32Array &p_live
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return PackedInt32Array();
    }
    return repl::row_recipients(*found, p_local_id, p_live);
}

PackedInt32Array NetwSyncModel::event_recipients(
    bool p_is_host,
    int64_t p_local_id,
    int64_t p_exclude,
    const PackedInt32Array &p_live
) {
    return repl::event_recipients(p_is_host, p_local_id, p_exclude, p_live);
}

bool NetwSyncModel::admits_sender(
    int64_t p_route,
    int64_t p_ordinal,
    int64_t p_sender,
    int64_t p_node_authority,
    int64_t p_controller
) const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return false;
    }
    return repl::row_admits_sender(
        *found,
        p_sender,
        p_node_authority,
        p_controller
    );
}

void NetwSyncModel::note_descriptors(
    int64_t p_route,
    const Dictionary &p_noted
) {
    HashMap<int64_t, int64_t> noted;
    const Array ordinals = p_noted.keys();
    for (int at = 0; at < ordinals.size(); ++at) {
        noted[int64_t(ordinals[at])] = int64_t(p_noted[ordinals[at]]);
    }
    impl.note_descriptors(p_route, noted);
}

bool NetwSyncModel::admits_schema(int64_t p_route, int64_t p_ordinal) const {
    return impl.admits_schema(p_route, p_ordinal);
}

StringName NetwSyncModel::slot_of(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    return StringName(
        String::num_int64(p_route) + "/" + String::num_int64(p_kind) + "/"
        + String(p_key) + "/" + String::num_int64(p_record)
    );
}

void NetwSyncModel::attach(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record,
    const Ref<RefCounted> &p_binding
) {
    const StringName slot = slot_of(p_route, p_kind, p_key, p_record);
    if (p_binding.is_null()) {
        bindings.erase(slot);
        return;
    }
    bindings[slot] = p_binding;
}

void NetwSyncModel::detach(
    int64_t p_route,
    int64_t p_kind,
    const StringName &p_key,
    int64_t p_record
) {
    bindings.erase(slot_of(p_route, p_kind, p_key, p_record));
}

Ref<RefCounted> NetwSyncModel::binding_of(int64_t p_route, int64_t p_ordinal)
    const {
    const repl::SetRow *found = impl.row(p_route, p_ordinal);
    if (found == nullptr) {
        return Ref<RefCounted>();
    }
    HashMap<StringName, Ref<RefCounted>>::ConstIterator held = bindings.find(
        slot_of(found->route, found->kind, found->key, found->record)
    );
    return held == bindings.end() ? Ref<RefCounted>() : held->value;
}

TypedArray<RefCounted> NetwSyncModel::route_bindings(
    int64_t p_route,
    int64_t p_kind
) const {
    TypedArray<RefCounted> out;
    const LocalVector<repl::SetRow> *rows = impl.route_rows(p_route);
    if (rows == nullptr) {
        return out;
    }
    for (const repl::SetRow &row : *rows) {
        if (row.kind != p_kind) {
            continue;
        }
        HashMap<StringName, Ref<RefCounted>>::ConstIterator held
            = bindings.find(
                slot_of(row.route, row.kind, row.key, row.record)
            );
        if (held != bindings.end()) {
            out.push_back(held->value);
        }
    }
    return out;
}

void NetwSyncModel::clear_route(int64_t p_route) {
    const String prefix = String::num_int64(p_route) + "/";
    LocalVector<StringName> doomed;
    for (const KeyValue<StringName, Ref<RefCounted>> &row : bindings) {
        if (String(row.key).begins_with(prefix)) {
            doomed.push_back(row.key);
        }
    }
    for (const StringName &slot : doomed) {
        bindings.erase(slot);
    }
    impl.clear_route(p_route);
}

void NetwSyncModel::clear() {
    bindings.clear();
    impl.clear();
}

void NetwSyncModel::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("declare", "route", "kind", "key", "comp", "set", "record",
                 "schema_hash", "policy", "audience"),
        &NetwSyncModel::declare,
        DEFVAL(int64_t(WritePolicy::AUTHORITY)),
        DEFVAL(repl::SET_AUDIENCE_PUBLIC)
    );
    ClassDB::bind_method(
        D_METHOD("drop", "route", "kind", "key", "record"),
        &NetwSyncModel::drop,
        DEFVAL(0)
    );
    ClassDB::bind_method(D_METHOD("row", "route", "ordinal"), &NetwSyncModel::row);
    ClassDB::bind_method(
        D_METHOD("row_for", "route", "kind", "key", "record"),
        &NetwSyncModel::row_for,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("route_rows", "route"),
        &NetwSyncModel::route_rows
    );
    ClassDB::bind_method(
        D_METHOD("authors", "route", "ordinal", "local_id", "node_authority",
                 "controller"),
        &NetwSyncModel::authors
    );
    ClassDB::bind_method(
        D_METHOD("recipients", "route", "ordinal", "local_id", "live"),
        &NetwSyncModel::recipients
    );
    ClassDB::bind_static_method(
        "NetwSyncModel",
        D_METHOD("event_recipients", "is_host", "local_id", "exclude", "live"),
        &NetwSyncModel::event_recipients
    );
    ClassDB::bind_method(
        D_METHOD("admits_sender", "route", "ordinal", "sender",
                 "node_authority", "controller"),
        &NetwSyncModel::admits_sender
    );
    ClassDB::bind_method(
        D_METHOD("note_descriptors", "route", "noted"),
        &NetwSyncModel::note_descriptors
    );
    ClassDB::bind_method(
        D_METHOD("admits_schema", "route", "ordinal"),
        &NetwSyncModel::admits_schema
    );
    ClassDB::bind_method(
        D_METHOD("clear_route", "route"),
        &NetwSyncModel::clear_route
    );
    ClassDB::bind_method(
        D_METHOD("attach", "route", "kind", "key", "record", "binding"),
        &NetwSyncModel::attach
    );
    ClassDB::bind_method(
        D_METHOD("detach", "route", "kind", "key", "record"),
        &NetwSyncModel::detach
    );
    ClassDB::bind_method(
        D_METHOD("binding_of", "route", "ordinal"),
        &NetwSyncModel::binding_of
    );
    ClassDB::bind_method(
        D_METHOD("route_bindings", "route", "kind"),
        &NetwSyncModel::route_bindings
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwSyncModel::clear);

    BIND_ENUM_CONSTANT(KIND_CONSUMED);
    BIND_ENUM_CONSTANT(KIND_DERIVED);
}

} // namespace netw

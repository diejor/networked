#include "netw/api/database.hpp"

#include "godot/utility.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_model.hpp"
#include "netw/api/transaction.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_RECORD_LOADED = "record_loaded";
constexpr const char *SIG_SCHEMA_REGISTERED = "schema_registered";
constexpr const char *SIG_SCHEMA_MISMATCH = "schema_mismatch";
constexpr const char *SIG_TRANSACTION_COMMITTED = "transaction_committed";

constexpr const char *KEY_IDS = "ids";
constexpr const char *KEY_ROUTES = "routes";
constexpr const char *KEY_TABLE = "table";
constexpr const char *KEY_ID = "id";
constexpr const char *KEY_DATA = "data";
constexpr const char *KEY_REQUEST = "request";
constexpr const char *KEY_MISSING = "missing";
constexpr const char *KEY_UNKNOWN = "unknown";
constexpr const char *KEY_OK = "ok";

NetwMultiplayer *core_of(const Ref<MultiplayerAPI> &p_session) {
    if (p_session.is_null()) {
        return nullptr;
    }
    NetwMultiplayer *seated = Object::cast_to<NetwMultiplayer>(p_session.ptr());
    if (seated != nullptr) {
        return seated;
    }
    return Object::cast_to<NetwMultiplayer>(
        Object::cast_to<Object>(p_session->get(StringName("_native_core")))
    );
}

TypedArray<StringName> names_of(const Dictionary &p_values) {
    TypedArray<StringName> names;
    const Array keys = p_values.keys();
    for (int at = 0; at < keys.size(); at++) {
        names.push_back(StringName(keys[at]));
    }
    return names;
}

} // namespace

const Ref<WarmPolicy> &NetwDatabase::warm_policy_in_force() const {
    if (warm_policy_assigned) {
        return warm_policy;
    }
    if (stock_warm_policy.is_null()) {
        stock_warm_policy.instantiate();
    }
    return stock_warm_policy;
}

void NetwDatabase::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_backend", "backend"),
        &NetwDatabase::set_backend
    );
    ClassDB::bind_method(D_METHOD("get_backend"), &NetwDatabase::get_backend);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "backend",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDatabaseBackend"
        ),
        "set_backend",
        "get_backend"
    );

    ClassDB::bind_method(
        D_METHOD("set_mismatch_policy", "policy"),
        &NetwDatabase::set_mismatch_policy
    );
    ClassDB::bind_method(
        D_METHOD("get_mismatch_policy"),
        &NetwDatabase::get_mismatch_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "mismatch_policy",
            PROPERTY_HINT_ENUM,
            "Purge,Load Partial,Fail"
        ),
        "set_mismatch_policy",
        "get_mismatch_policy"
    );

    ClassDB::bind_method(
        D_METHOD("set_warm_policy", "policy"),
        &NetwDatabase::set_warm_policy
    );
    ClassDB::bind_method(
        D_METHOD("get_warm_policy"),
        &NetwDatabase::get_warm_policy
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "warm_policy",
            PROPERTY_HINT_RESOURCE_TYPE,
            "WarmPolicy",
            PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_EDITOR_INSTANTIATE_OBJECT
        ),
        "set_warm_policy",
        "get_warm_policy"
    );

    ClassDB::bind_method(D_METHOD("table", "table"), &NetwDatabase::table);
    ClassDB::bind_method(
        D_METHOD("declare_table", "table", "schema", "record_script"),
        &NetwDatabase::declare_table,
        DEFVAL(Variant()),
        DEFVAL(Ref<Script>())
    );
    ClassDB::bind_method(
        D_METHOD("get_column_type", "table", "column"),
        &NetwDatabase::get_column_type
    );
    ClassDB::bind_method(
        D_METHOD("get_registered_columns", "table"),
        &NetwDatabase::get_registered_columns
    );

    ClassDB::bind_method(D_METHOD("find", "table", "id"), &NetwDatabase::find);
    ClassDB::bind_method(
        D_METHOD("find_all", "table", "filter"),
        &NetwDatabase::find_all,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("transaction", "body"),
        &NetwDatabase::transaction
    );
    ClassDB::bind_method(
        D_METHOD("erase", "table", "id"),
        &NetwDatabase::erase
    );
    ClassDB::bind_method(
        D_METHOD("warm", "table", "request"),
        &NetwDatabase::warm
    );

    ClassDB::bind_method(
        D_METHOD("table_flush", "session", "table", "into", "ids"),
        &NetwDatabase::table_flush
    );
    ClassDB::bind_method(
        D_METHOD("table_hydrate", "session", "table", "into"),
        &NetwDatabase::table_hydrate
    );

    ClassDB::bind_method(
        D_METHOD("open_slot", "slot"),
        &NetwDatabase::open_slot
    );
    ClassDB::bind_method(D_METHOD("current_slot"), &NetwDatabase::current_slot);
    ClassDB::bind_method(D_METHOD("list_slots"), &NetwDatabase::list_slots);
    ClassDB::bind_method(
        D_METHOD("delete_slot", "slot"),
        &NetwDatabase::delete_slot
    );

    ADD_SIGNAL(MethodInfo(
        SIG_RECORD_LOADED,
        PropertyInfo(Variant::STRING_NAME, "table"),
        PropertyInfo(Variant::STRING_NAME, "id"),
        PropertyInfo(Variant::BOOL, "hit")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCHEMA_REGISTERED,
        PropertyInfo(Variant::STRING_NAME, "table"),
        PropertyInfo(Variant::ARRAY, "columns")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_SCHEMA_MISMATCH,
        PropertyInfo(Variant::STRING_NAME, "table"),
        PropertyInfo(Variant::STRING_NAME, "id"),
        PropertyInfo(Variant::ARRAY, "missing"),
        PropertyInfo(Variant::ARRAY, "unknown")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_TRANSACTION_COMMITTED,
        PropertyInfo(Variant::INT, "table_count"),
        PropertyInfo(Variant::INT, "record_count")
    ));

    BIND_ENUM_CONSTANT(PURGE);
    BIND_ENUM_CONSTANT(LOAD_PARTIAL);
    BIND_ENUM_CONSTANT(FAIL);
}

void NetwDatabase::set_backend(const Ref<NetwDatabaseBackend> &p_backend) {
    backend = p_backend;
}

void NetwDatabase::set_mismatch_policy(SchemaMismatchPolicy p_policy) {
    mismatch_policy = p_policy;
}

void NetwDatabase::set_warm_policy(const Ref<WarmPolicy> &p_policy) {
    warm_policy = p_policy;
    warm_policy_assigned = true;
}

Ref<NetwRecordTable> NetwDatabase::table(const StringName &p_table) {
    Ref<Script> script;
    if (const godot::HashMap<StringName, Ref<Script>>::ConstIterator held
        = table_scripts.find(p_table)) {
        script = held->value;
    }
    return NetwRecordTable::open(Ref<NetwDatabase>(this), p_table, script);
}

void NetwDatabase::declare_table(
    const StringName &p_table,
    const Variant &p_declaration,
    const Ref<Script> &p_record_script
) {
    if (p_record_script.is_valid()) {
        table_scripts[p_table] = p_record_script;
    }

    const Ref<NetwSchema> declared = p_declaration;
    if (declared.is_valid()) {
        TypedArray<StringName> names;
        godot::HashMap<StringName, int> types;
        if (const godot::HashMap<StringName, godot::HashMap<StringName, int>>::
                ConstIterator held = column_types.find(p_table)) {
            types = held->value;
        }
        const TypedArray<NetwSchemaColumn> columns = declared->get_columns();
        for (int at = 0; at < columns.size(); at++) {
            const Ref<NetwSchemaColumn> column = columns[at];
            if (column.is_null()) {
                continue;
            }
            names.push_back(column->get_key());
            types[column->get_key()] = column->get_type();
        }
        column_types[p_table] = types;
        if (!names.is_empty()) {
            register_schema(p_table, names);
        }
        return;
    }

    if (p_declaration.get_type() == Variant::ARRAY) {
        const Array listed = p_declaration;
        TypedArray<StringName> names;
        for (int at = 0; at < listed.size(); at++) {
            names.push_back(StringName(listed[at]));
        }
        if (!names.is_empty()) {
            register_schema(p_table, names);
        }
    }
}

void NetwDatabase::register_schema(
    const StringName &p_table,
    const TypedArray<StringName> &p_columns
) {
    if (!schema.has(p_table)) {
        schema[p_table] = TypedArray<StringName>();
    }
    TypedArray<StringName> &existing = schema[p_table];
    for (int at = 0; at < p_columns.size(); at++) {
        if (!existing.has(p_columns[at])) {
            existing.push_back(p_columns[at]);
        }
    }

    emit_signal(SIG_SCHEMA_REGISTERED, p_table, existing.duplicate());
}

int NetwDatabase::get_column_type(
    const StringName &p_table,
    const StringName &p_column
) const {
    const godot::HashMap<StringName, godot::HashMap<StringName, int>>::
        ConstIterator held = column_types.find(p_table);
    if (!held) {
        return -1;
    }
    const godot::HashMap<StringName, int>::ConstIterator declared
        = held->value.find(p_column);
    return declared ? declared->value : -1;
}

TypedArray<StringName> NetwDatabase::get_registered_columns(
    const StringName &p_table
) const {
    const godot::HashMap<StringName, TypedArray<StringName>>::ConstIterator held
        = schema.find(p_table);
    if (!held) {
        return TypedArray<StringName>();
    }
    return held->value.duplicate();
}

void NetwDatabase::initialize_backend() {
    if (initialized) {
        return;
    }
    initialized = true;
    slot_locked = true;
    if (backend.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "the database has no backend assigned, so its calls are no-ops"
        );
        return;
    }
    Dictionary declared;
    for (const KeyValue<StringName, TypedArray<StringName>> &row : schema) {
        declared[row.key] = row.value;
    }
    const Ref<NetwPromise> ready = backend->initialize(declared, String(slot));
    ready->catch_error(callable_mp(this, &NetwDatabase::refuse_init));
    ready->then(callable_mp(this, &NetwDatabase::warm_declared));
}

void NetwDatabase::refuse_init(int p_code, const String &p_detail) {
    NETW_ERROR(
        sys::TABLE,
        "backend initialization failed with %s: %s",
        gd::error_name(p_code),
        p_detail
    );
}

void NetwDatabase::warm_declared(const Variant &p_outcome) {
    const int64_t code = p_outcome;
    if (code != OK) {
        refuse_init(int(code), String());
        return;
    }
    if (backend.is_null()) {
        return;
    }
    backend->warm(build_warm_directives());
}

Array NetwDatabase::build_warm_directives() const {
    Array directives;
    const Ref<WarmPolicy> &policy = warm_policy_in_force();
    if (policy.is_null()) {
        return directives;
    }
    for (const KeyValue<StringName, TypedArray<StringName>> &row : schema) {
        const Ref<WarmRequest> request = policy->plan_table(row.key, row.value);
        if (request.is_null()
            || request->get_kind() == WarmRequest::KIND_NONE) {
            continue;
        }
        Dictionary directive;
        directive[KEY_TABLE] = row.key;
        directive[KEY_REQUEST] = request;
        directives.push_back(directive);
    }
    return directives;
}

Ref<NetwPromise> NetwDatabase::warm(
    const StringName &p_table,
    const Ref<WarmRequest> &p_request
) {
    initialize_backend();
    if (backend.is_null()) {
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    Dictionary directive;
    directive[KEY_TABLE] = p_table;
    directive[KEY_REQUEST] = p_request;
    Array directives;
    directives.push_back(directive);
    return backend->warm(directives);
}

Dictionary NetwDatabase::diff_record(
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_record
) {
    const TypedArray<StringName> declared = get_registered_columns(p_table);
    TypedArray<StringName> missing;
    TypedArray<StringName> unknown;

    for (int at = 0; at < declared.size(); at++) {
        if (!p_record.has(declared[at])) {
            missing.push_back(declared[at]);
        }
    }

    const Array held = p_record.keys();
    for (int at = 0; at < held.size(); at++) {
        const StringName key = held[at];
        if (!declared.has(key)) {
            unknown.push_back(key);
        }
    }

    const bool ok = missing.is_empty() && unknown.is_empty();
    if (!ok) {
        emit_signal(SIG_SCHEMA_MISMATCH, p_table, p_id, missing, unknown);
    }

    Dictionary diff;
    diff[KEY_MISSING] = missing;
    diff[KEY_UNKNOWN] = unknown;
    diff[KEY_OK] = ok;
    return diff;
}

Dictionary NetwDatabase::apply_mismatch_policy(
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_record,
    const Dictionary &p_diff,
    int &r_error
) {
    r_error = OK;

    if (bool(p_diff[KEY_OK])) {
        return p_record;
    }

    const TypedArray<StringName> unknown = p_diff[KEY_UNKNOWN];
    if (unknown.is_empty()) {
        return p_record;
    }

    switch (mismatch_policy) {
        case PURGE: {
            erase(p_table, p_id);
            r_error = ERR_FILE_NOT_FOUND;
            return Dictionary();
        }
        case LOAD_PARTIAL: {
            const TypedArray<StringName> declared
                = get_registered_columns(p_table);
            Dictionary filtered;
            for (int at = 0; at < declared.size(); at++) {
                const StringName key = declared[at];
                if (p_record.has(key)) {
                    filtered[key] = p_record[key];
                }
            }
            return filtered;
        }
        case FAIL: {
            r_error = ERR_UNCONFIGURED;
            return Dictionary();
        }
    }

    return p_record;
}

Dictionary NetwDatabase::reject_mistyped(
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_record
) const {
    const godot::HashMap<StringName, godot::HashMap<StringName, int>>::
        ConstIterator held = column_types.find(p_table);
    if (!held || held->value.is_empty() || p_record.is_empty()) {
        return p_record;
    }
    Dictionary out = p_record;
    bool copied = false;
    const Array keys = p_record.keys();
    for (int at = 0; at < keys.size(); at++) {
        const StringName column = keys[at];
        const godot::HashMap<StringName, int>::ConstIterator typed
            = held->value.find(column);
        const int declared = typed ? typed->value : -1;
        if (declared < 0 || declared == NetwMultiplayer::COLUMN_VARIANT) {
            continue;
        }
        const Variant::Type wanted = NetwMultiplayer::schema_get_element_type(
            NetwMultiplayer::ColumnType(declared)
        );
        if (p_record[column].get_type() == wanted) {
            continue;
        }
        if (!copied) {
            out = p_record.duplicate();
            copied = true;
        }
        out.erase(column);
        NETW_WARN(
            sys::TABLE,
            "'%s.%s' in record '%s' is %s but the schema declares %s, so the "
            "scene default is kept for it",
            String(p_table),
            String(column),
            String(p_id),
            Variant::get_type_name(p_record[column].get_type()),
            Variant::get_type_name(wanted)
        );
    }
    return out;
}

Ref<NetwPromise> NetwDatabase::find(
    const StringName &p_table,
    const StringName &p_id
) {
    initialize_backend();
    if (!schema.has(p_table)) {
        NETW_WARN(
            sys::TABLE,
            "read on unregistered table '%s', declare the schema before "
            "querying",
            String(p_table)
        );
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            vformat("table '%s' has no declared schema", String(p_table))
        );
    }
    if (backend.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "the read of '%s' is refused, no backend is set",
            String(p_table)
        );
        return NetwPromise::rejected(ERR_UNCONFIGURED, "no backend is set");
    }

    Ref<NetwPromise> answer;
    answer.instantiate();
    const Ref<NetwPromise> stored = backend->find_by_id(p_table, p_id);
    stored->catch_error(callable_mp(this, &NetwDatabase::settle_unread)
                            .bind(answer, p_table, p_id));
    stored->then(callable_mp(this, &NetwDatabase::settle_found)
                     .bind(answer, p_table, p_id));
    return answer;
}

void NetwDatabase::settle_unread(
    int,
    const String &,
    const Ref<NetwPromise> &p_answer,
    const StringName &p_table,
    const StringName &p_id
) {
    settle_found(Dictionary(), p_answer, p_table, p_id);
}

void NetwDatabase::settle_found(
    const Dictionary &p_record,
    const Ref<NetwPromise> &p_answer,
    const StringName &p_table,
    const StringName &p_id
) {
    emit_signal(SIG_RECORD_LOADED, p_table, p_id, !p_record.is_empty());
    if (p_record.is_empty()) {
        p_answer->resolve(Dictionary());
        return;
    }

    const Dictionary diff = diff_record(p_table, p_id, p_record);
    if (bool(diff[KEY_OK])) {
        p_answer->resolve(reject_mistyped(p_table, p_id, p_record));
        return;
    }

    int refusal = OK;
    const Dictionary judged
        = apply_mismatch_policy(p_table, p_id, p_record, diff, refusal);
    if (judged.is_empty() && refusal != OK && refusal != ERR_FILE_NOT_FOUND) {
        p_answer->reject(
            static_cast<Error>(refusal),
            vformat(
                "the schema mismatch policy refused record '%s.%s'",
                String(p_table),
                String(p_id)
            )
        );
        return;
    }
    p_answer->resolve(reject_mistyped(p_table, p_id, judged));
}

Ref<NetwPromise> NetwDatabase::find_all(
    const StringName &p_table,
    const Dictionary &p_filter
) {
    initialize_backend();
    if (!schema.has(p_table)) {
        NETW_ERROR(
            sys::TABLE,
            "read on unregistered table '%s', declare the schema before "
            "querying",
            String(p_table)
        );
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            vformat("table '%s' has no declared schema", String(p_table))
        );
    }
    if (backend.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "the read of '%s' is refused, no backend is set",
            String(p_table)
        );
        return NetwPromise::rejected(ERR_UNCONFIGURED, "no backend is set");
    }
    return backend->find_all(p_table, p_filter);
}

Ref<NetwPromise> NetwDatabase::transaction(const Callable &p_body) {
    initialize_backend();
    if (backend.is_null()) {
        NETW_ERROR(sys::TABLE, "the transaction is refused, no backend is set");
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }

    Ref<NetwTransaction> collected;
    collected.instantiate();
    if (p_body.is_valid()) {
        p_body.call(collected);
    }

    const Array rows = collected->queued();
    Dictionary touched;
    for (int at = 0; at < rows.size(); at++) {
        const Dictionary row = rows[at];
        touched[row[KEY_TABLE]] = true;
    }

    const Ref<NetwPromise> written = backend->commit(rows);
    written->then(callable_mp(this, &NetwDatabase::announce_committed)
                      .bind(touched.size(), rows.size()));
    return written;
}

void NetwDatabase::announce_committed(
    const Variant &p_outcome,
    int p_table_count,
    int p_row_count
) {
    if (int64_t(p_outcome) != OK) {
        return;
    }
    emit_signal(SIG_TRANSACTION_COMMITTED, p_table_count, p_row_count);
}

Ref<NetwPromise> NetwDatabase::erase(
    const StringName &p_table,
    const StringName &p_id
) {
    initialize_backend();
    if (backend.is_null()) {
        NETW_ERROR(sys::TABLE, "the erase is refused, no backend is set");
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    return backend->erase(p_table, p_id);
}

Ref<NetwPromise> NetwDatabase::table_flush(
    const Ref<MultiplayerAPI> &p_session,
    const RID &p_table,
    const StringName &p_into,
    const PackedStringArray &p_ids
) {
    NetwMultiplayer *core = core_of(p_session);
    if (core == nullptr || !p_session->is_server()) {
        NETW_ERROR(sys::TABLE, "table_flush is server-only");
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    if (p_into.is_empty()) {
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    const RID declared = core->table_get_schema(p_table);
    if (!declared.is_valid()) {
        return NetwPromise::resolved(int64_t(ERR_DOES_NOT_EXIST));
    }
    const PackedInt64Array routes = core->table_read_routes(p_table);
    if (p_ids.size() != routes.size()) {
        return NetwPromise::resolved(int64_t(ERR_INVALID_DATA));
    }

    Dictionary values;
    values[KEY_IDS] = p_ids;
    PackedStringArray skipped;
    const int columns = core->schema_get_column_count(declared);
    for (int column = 0; column < columns; column++) {
        const StringName key = core->schema_get_column_key(declared, column);
        if (core->schema_get_column_type(declared, column)
            == NetwMultiplayer::COLUMN_ENTITY) {
            skipped.push_back(String(key));
            continue;
        }
        values[key] = core->table_read_column(p_table, column).duplicate();
    }
    if (!skipped.is_empty()) {
        NETW_WARN(
            sys::TABLE,
            "table '%s' column(s) %s are routes, which mean nothing in the "
            "session that loads them, so they are not saved and hydrate "
            "zero-fills them",
            String(core->schema_get_name(declared)),
            String(", ").join(skipped)
        );
    }

    declare_table(p_into, names_of(values));
    return transaction(
        callable_mp(this, &NetwDatabase::queue_table_record)
            .bind(p_into, core->schema_get_name(declared), values)
    );
}

void NetwDatabase::queue_table_record(
    const Ref<NetwTransaction> &p_transaction,
    const StringName &p_into,
    const StringName &p_record,
    const Dictionary &p_values
) {
    if (p_transaction.is_null()) {
        return;
    }
    p_transaction->queue_upsert(p_into, p_record, p_values);
}

Ref<NetwPromise> NetwDatabase::table_hydrate(
    const Ref<MultiplayerAPI> &p_session,
    const RID &p_table,
    const StringName &p_into
) {
    Dictionary empty;
    empty[KEY_ROUTES] = PackedInt64Array();
    empty[KEY_IDS] = PackedStringArray();

    NetwMultiplayer *core = core_of(p_session);
    if (core == nullptr || !p_session->is_server()) {
        NETW_ERROR(sys::TABLE, "table_hydrate is server-only");
        return NetwPromise::resolved(empty);
    }
    const RID declared = core->table_get_schema(p_table);
    if (p_into.is_empty() || !declared.is_valid()) {
        return NetwPromise::resolved(empty);
    }

    TypedArray<StringName> names;
    names.push_back(StringName(KEY_IDS));
    const int columns = core->schema_get_column_count(declared);
    for (int column = 0; column < columns; column++) {
        names.push_back(core->schema_get_column_key(declared, column));
    }
    declare_table(p_into, names);

    Ref<NetwPromise> answer;
    answer.instantiate();
    const Ref<NetwPromise> stored
        = find(p_into, core->schema_get_name(declared));
    stored->catch_error(
        callable_mp(this, &NetwDatabase::settle_unhydrated).bind(answer, empty)
    );
    stored->then(callable_mp(this, &NetwDatabase::settle_hydrated)
                     .bind(answer, p_session, p_table));
    return answer;
}

void NetwDatabase::settle_unhydrated(
    int,
    const String &,
    const Ref<NetwPromise> &p_answer,
    const Dictionary &p_empty
) {
    p_answer->resolve(p_empty);
}

void NetwDatabase::settle_hydrated(
    const Dictionary &p_data,
    const Ref<NetwPromise> &p_answer,
    const Ref<MultiplayerAPI> &p_session,
    const RID &p_table
) {
    NetwMultiplayer *core = core_of(p_session);
    if (core == nullptr) {
        Dictionary empty;
        empty[KEY_ROUTES] = PackedInt64Array();
        empty[KEY_IDS] = PackedStringArray();
        p_answer->resolve(empty);
        return;
    }
    p_answer->resolve(core->persist_table_commit(
        p_table,
        core->table_get_schema(p_table),
        p_data
    ));
}

void NetwDatabase::open_slot(const StringName &p_slot) {
    if (slot_locked) {
        NETW_ERROR(
            sys::TABLE,
            "the slot is startup-only and the backend already initialized, so "
            "'%s' is refused. Open a slot before any persistence engine "
            "registers",
            String(p_slot)
        );
        return;
    }
    slot = p_slot;
}

Ref<NetwPromise> NetwDatabase::list_slots() {
    if (backend.is_null()) {
        return NetwPromise::resolved(TypedArray<StringName>());
    }
    return backend->list_namespaces();
}

Ref<NetwPromise> NetwDatabase::delete_slot(const StringName &p_slot) {
    if (backend.is_null()) {
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    return backend->delete_namespace(String(p_slot));
}

bool NetwDatabase::_get(const StringName &p_property, Variant &r_ret) const {
    if (!schema.has(p_property)) {
        return false;
    }
    r_ret = const_cast<NetwDatabase *>(this)->table(p_property);
    return true;
}

void NetwDatabase::_get_property_list(List<PropertyInfo> *p_list) const {
    for (const KeyValue<StringName, TypedArray<StringName>> &row : schema) {
        p_list->push_back(PropertyInfo(
            Variant::OBJECT,
            String(row.key),
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwRecordTable",
            PROPERTY_USAGE_NONE
        ));
    }
}

} // namespace netw

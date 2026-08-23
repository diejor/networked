#include "netw/api/persistence_engine.hpp"

#include "godot/class_db.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/colors.hpp"
#include "netw/entity_control.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr int LANE_RETAINED = 1;

const char *KEY_PROPERTY = "property";
const char *KEY_INTERVAL = "interval";
const char *KEY_NODE = "node";
const char *KEY_DATABASE = "database";
const char *KEY_TABLE = "table";
const char *KEY_PROVIDER = "record_id_provider";
const char *KEY_SPAWN_HYDRATION = "hydrate_on_spawn";
const char *KEY_DEFAULT_INTERVAL = "default_interval";

void queue_row(
    const Variant &p_transaction,
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_values
) {
    Object *transaction = p_transaction;
    if (transaction == nullptr) {
        return;
    }
    transaction->call("queue_upsert", p_table, p_id, p_values);
}

Callable &config_reader() {
    static Callable reader;
    return reader;
}

Callable &property_configs_reader() {
    static Callable reader;
    return reader;
}

Callable &schema_declarer() {
    static Callable declarer;
    return declarer;
}

HashMap<String, uint64_t> &claims() {
    static HashMap<String, uint64_t> held;
    return held;
}

bool holds(const Array &p_keys, const StringName &p_property) {
    for (int at = 0; at < p_keys.size(); ++at) {
        if (StringName(p_keys[at]) == p_property) {
            return true;
        }
    }
    return false;
}

} // namespace

StringName NetwPersistenceEngine::meta_database() {
    return StringName("netw_persistence_database");
}

StringName NetwPersistenceEngine::meta_table() {
    return StringName("netw_persistence_table");
}

StringName NetwPersistenceEngine::meta_columns() {
    return StringName("netw_persistence_columns");
}

void NetwPersistenceEngine::set_config_reader(const Callable &p_reader) {
    config_reader() = p_reader;
}

void NetwPersistenceEngine::set_property_configs_reader(
    const Callable &p_reader
) {
    property_configs_reader() = p_reader;
}

void NetwPersistenceEngine::set_schema_declarer(const Callable &p_declarer) {
    schema_declarer() = p_declarer;
}

void NetwPersistenceEngine::forget_claims() {
    claims().clear();
}

Dictionary NetwPersistenceEngine::config_of(Object *p_owner) {
    if (p_owner == nullptr) {
        return Dictionary();
    }
    if (!config_reader().is_valid()) {
        NETW_ERROR(
            sys::TABLE,
            "no persistence config reader is installed, so no archetype "
            "declaration can be read"
        );
        return Dictionary();
    }
    return config_reader().call(p_owner);
}

Ref<NetwPersistenceEngine> NetwPersistenceEngine::create(
    Object *p_entity,
    const Dictionary &p_declaration
) {
    Ref<NetwEntity> entity = Object::cast_to<NetwEntity>(p_entity);
    if (entity.is_null() || p_declaration.is_empty()) {
        NETW_ERROR(
            sys::TABLE,
            "a persistence engine needs both an entity and a declaration, so "
            "one was not built"
        );
        return Ref<NetwPersistenceEngine>();
    }
    Ref<NetwPersistenceEngine> engine;
    engine.instantiate();
    engine->entity_id = gd::instance_id(p_entity);
    engine->declared_database = p_declaration.get(KEY_DATABASE, Variant());
    engine->declared_table = p_declaration.get(KEY_TABLE, StringName());
    engine->record_id_provider = p_declaration.get(
        KEY_PROVIDER,
        StringName()
    );
    engine->hydrate_on_spawn = p_declaration.get(KEY_SPAWN_HYDRATION, false);
    engine->book.set_default_interval(
        double(p_declaration.get(KEY_DEFAULT_INTERVAL, 0.0))
    );
    engine->build_columns();
    return engine;
}

Object *NetwPersistenceEngine::entity() const {
    return gd::object_of(entity_id);
}

Node *NetwPersistenceEngine::owner() const {
    Ref<NetwEntity> held = Object::cast_to<NetwEntity>(entity());
    if (held.is_null()) {
        return nullptr;
    }
    return held->get_owner();
}

Node *NetwPersistenceEngine::owner_node() const {
    return owner();
}

Node *NetwPersistenceEngine::column_node(const StringName &p_property) const {
    const HashMap<StringName, ObjectID>::ConstIterator row
        = column_nodes.find(p_property);
    if (row == column_nodes.end()) {
        return nullptr;
    }
    return Object::cast_to<Node>(gd::object_of(row->value));
}

void NetwPersistenceEngine::warn_duplicate(
    const StringName &p_property
) const {
    Node *root = owner();
    NETW_WARN(
        sys::TABLE,
        "persisted column '%s' is declared on more than one node under entity "
        "'%s', so the last write wins",
        String(p_property),
        root != nullptr ? String(root->get_name()) : String()
    );
}

void NetwPersistenceEngine::build_columns() {
    NETW_ZONE_NC("persistence build columns", colors::TABLE);
    Ref<NetwEntity> held = Object::cast_to<NetwEntity>(entity());
    Node *root = held.is_valid() ? held->get_owner() : nullptr;
    if (root == nullptr) {
        return;
    }

    LocalVector<Node *> pending;
    LocalVector<Node *> walked;
    walked.push_back(root);
    const TypedArray<Node> roots = root->get_children();
    for (int at = 0; at < roots.size(); ++at) {
        pending.push_back(Object::cast_to<Node>(roots[at]));
    }
    while (!pending.is_empty()) {
        Node *node = pending[pending.size() - 1];
        pending.resize(pending.size() - 1);
        if (node == nullptr || NetwEntity::of(node) != held) {
            continue;
        }
        walked.push_back(node);
        const TypedArray<Node> children = node->get_children();
        for (int at = 0; at < children.size(); ++at) {
            pending.push_back(Object::cast_to<Node>(children[at]));
        }
    }

    for (uint32_t at = 0; at < walked.size(); ++at) {
        Node *node = walked[at];
        const Dictionary configs = property_configs_reader().is_valid()
            ? Dictionary(property_configs_reader().call(node))
            : Dictionary();
        const Array declared = configs.keys();
        for (int index = 0; index < declared.size(); ++index) {
            const StringName property = declared[index];
            Object *cfg = gd::live_object(configs[property]);
            if (cfg == nullptr || !bool(cfg->get("is_persisted"))) {
                continue;
            }
            if (column_nodes.has(property)) {
                warn_duplicate(property);
            }
            column_nodes[property] = gd::instance_id(node);
            book.declare(property, double(cfg->get("persist_interval")));
        }
    }

    const Array extra = root->get_meta(meta_columns(), Array());
    for (int at = 0; at < extra.size(); ++at) {
        const Dictionary entry = extra[at];
        const StringName property = entry.get(KEY_PROPERTY, StringName());
        if (property == StringName() || column_nodes.has(property)) {
            continue;
        }
        column_nodes[property] = gd::instance_id(root);
        book.declare(property, double(entry.get(KEY_INTERVAL, 0.0)));
    }
}

bool NetwPersistenceEngine::columns_empty() const {
    return book.is_empty();
}

Variant NetwPersistenceEngine::database() const {
    Node *root = owner();
    if (root != nullptr && root->has_meta(meta_database())) {
        return root->get_meta(meta_database());
    }
    return declared_database;
}

StringName NetwPersistenceEngine::table_name() const {
    Node *root = owner();
    if (root != nullptr && root->has_meta(meta_table())) {
        return root->get_meta(meta_table());
    }
    return declared_table;
}

bool NetwPersistenceEngine::wants_spawn_hydration() const {
    return hydrate_on_spawn;
}

StringName NetwPersistenceEngine::record_id() const {
    Ref<NetwEntity> held = Object::cast_to<NetwEntity>(entity());
    Node *root = held.is_valid() ? held->get_owner() : nullptr;
    if (root == nullptr) {
        return StringName();
    }
    if (record_id_provider != StringName()
        && root->has_method(record_id_provider)) {
        return StringName(root->call(record_id_provider));
    }
    if (held->get_entity_id() != StringName()) {
        return held->get_entity_id();
    }
    return StringName(root->get_name());
}

Dictionary NetwPersistenceEngine::gather(const Array &p_keys) const {
    Dictionary out;
    const Array declared = book.properties();
    for (int at = 0; at < declared.size(); ++at) {
        const StringName property = declared[at];
        if (!p_keys.is_empty() && !holds(p_keys, property)) {
            continue;
        }
        Node *node = column_node(property);
        if (node != nullptr) {
            out[property] = node->get(property);
        }
    }
    return out;
}

void NetwPersistenceEngine::apply(const Dictionary &p_data) {
    const Array declared = book.properties();
    for (int at = 0; at < declared.size(); ++at) {
        const StringName property = declared[at];
        if (!p_data.has(property)) {
            continue;
        }
        Node *node = column_node(property);
        if (node != nullptr) {
            node->set(property, p_data[property]);
        }
    }
}

bool NetwPersistenceEngine::is_dirty() const {
    return book.differs(gather(Array()));
}

void NetwPersistenceEngine::ensure_schema() {
    if (schema_registered) {
        return;
    }
    const Variant held_database = database();
    Object *db = gd::live_object(held_database);
    const StringName table = table_name();
    if (db == nullptr || table == StringName()) {
        return;
    }
    if (!schema_declarer().is_valid()) {
        NETW_ERROR(
            sys::TABLE,
            "no schema declarer is installed, so table '%s' was never declared",
            String(table)
        );
        return;
    }
    Array columns;
    const Array declared = book.properties();
    for (int at = 0; at < declared.size(); ++at) {
        const StringName property = declared[at];
        Dictionary column;
        column[KEY_PROPERTY] = property;
        column[KEY_NODE] = gd::held(column_node(property));
        columns.push_back(column);
    }
    schema_declarer().call(db, table, columns);
    schema_registered = true;
    claim_record_id(db);
}

void NetwPersistenceEngine::claim_record_id(Object *p_database) {
    const StringName id = record_id();
    Node *root = owner();
    if (id == StringName() || root == nullptr) {
        return;
    }
    const StringName table = table_name();
    const bool nameless = record_id_provider == StringName();
    Ref<NetwEntity> held = Object::cast_to<NetwEntity>(entity());
    if (nameless
        && (held.is_null() || held->get_entity_id() == StringName())) {
        NETW_WARN(
            sys::TABLE,
            "persisted entity '%s' has no record id provider and no entity id, "
            "so it saves under its node name: a rename, or a second instance, "
            "loads the wrong row",
            String(root->get_name())
        );
    }
    const String key = String::num_uint64(
                           uint64_t(gd::instance_id(p_database))
                       )
        + "/" + String(table) + "/" + String(id);
    const HashMap<String, uint64_t>::ConstIterator claimed = claims().find(key);
    const uint64_t mine = uint64_t(gd::instance_id(root));
    if (claimed != claims().end() && claimed->value != mine
        && gd::object_of(ObjectID(claimed->value)) != nullptr) {
        NETW_WARN(
            sys::TABLE,
            "persisted entity '%s' claims record '%s.%s', which another live "
            "entity already holds, so both flush the same row every tick",
            String(root->get_name()),
            String(table),
            String(id)
        );
        return;
    }
    claims()[key] = mine;
}

Ref<NetwPromise> NetwPersistenceEngine::hydrate() {
    NETW_ZONE_NC("persistence hydrate", colors::TABLE);
    const Variant held_database = database();
    Object *db = gd::live_object(held_database);
    const StringName table = table_name();
    if (db == nullptr || table == StringName()) {
        NETW_TRACE(
            sys::TABLE,
            "hydrate refused: the archetype names no database or no table"
        );
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    ensure_schema();
    const Ref<NetwPromise> stored = db->call(
        "find_promise",
        table,
        record_id()
    );
    if (stored.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "the database answered no promise for table '%s', so the hydrate "
            "would have waited forever",
            String(table)
        );
        return NetwPromise::rejected(
            ERR_INVALID_DATA,
            "find_promise answered no promise"
        );
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    stored->catch_error(
        callable_mp(this, &NetwPersistenceEngine::settle_refused)
            .bind(answer)
    );
    stored->then(
        callable_mp(this, &NetwPersistenceEngine::settle_hydrated)
            .bind(answer)
    );
    return answer;
}

void NetwPersistenceEngine::settle_hydrated(
    const Dictionary &p_record,
    const Ref<NetwPromise> &p_answer
) {
    if (!p_record.is_empty()) {
        apply(p_record);
        book.adopt(gather(Array()));
    }
    emit_signal(StringName("hydrated"));
    NETW_TRACE(
        sys::TABLE,
        "hydrated %d column(s) onto '%s'",
        int(p_record.size()),
        String(record_id())
    );
    p_answer->resolve(int64_t(OK));
}

void NetwPersistenceEngine::settle_refused(
    int p_code,
    const String &p_detail,
    const Ref<NetwPromise> &p_answer
) {
    NETW_TRACE(
        sys::TABLE,
        "the database refused record '%s' with %d, %s",
        String(record_id()),
        p_code,
        p_detail
    );
    p_answer->resolve(int64_t(p_code));
}

Ref<NetwPromise> NetwPersistenceEngine::flush(const Array &p_keys) {
    NETW_ZONE_NC("persistence flush", colors::TABLE);
    const Variant held_database = database();
    Object *db = gd::live_object(held_database);
    const StringName table = table_name();
    if (db == nullptr || table == StringName()) {
        NETW_TRACE(
            sys::TABLE,
            "flush refused: the archetype names no database or no table"
        );
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    ensure_schema();
    const Dictionary subset = gather(p_keys);
    if (subset.is_empty()) {
        return NetwPromise::resolved(int64_t(OK));
    }
    const Ref<NetwPromise> written = db->call(
        "transaction_promise",
        callable_mp_static(&queue_row).bind(table, record_id(), subset)
    );
    if (written.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "the database answered no promise for table '%s', so the flush "
            "would have waited forever",
            String(table)
        );
        return NetwPromise::rejected(
            ERR_INVALID_DATA,
            "transaction_promise answered no promise"
        );
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    written->catch_error(
        callable_mp(this, &NetwPersistenceEngine::settle_refused)
            .bind(answer)
    );
    written->then(
        callable_mp(this, &NetwPersistenceEngine::settle_flushed)
            .bind(subset, answer)
    );
    return answer;
}

void NetwPersistenceEngine::settle_flushed(
    const Variant &p_result,
    const Dictionary &p_subset,
    const Ref<NetwPromise> &p_answer
) {
    const int64_t code = p_result;
    if (code == int64_t(OK)) {
        book.commit(p_subset);
        emit_signal(StringName("flushed"));
    }
    NETW_TRACE(
        sys::TABLE,
        "flushed %d column(s) of '%s' with %d",
        int(p_subset.size()),
        String(record_id()),
        int(code)
    );
    p_answer->resolve(code);
}

Dictionary NetwPersistenceEngine::snapshot_tick(double p_delta) {
    Dictionary out;
    const Array due = book.advance(p_delta);
    if (due.is_empty()) {
        return out;
    }
    const Dictionary values = book.changed(gather(due));
    if (values.is_empty()) {
        return out;
    }
    const Variant held_database = database();
    Object *db = gd::live_object(held_database);
    const StringName table = table_name();
    if (db == nullptr || table == StringName()) {
        return out;
    }
    ensure_schema();
    out["db"] = database();
    out["table"] = table;
    out["id"] = record_id();
    out["values"] = values;
    return out;
}

void NetwPersistenceEngine::commit_snapshot(const Dictionary &p_values) {
    book.commit(p_values);
    emit_signal(StringName("flushed"));
}

void NetwPersistenceEngine::lint() {
    Ref<NetwEntity> held = Object::cast_to<NetwEntity>(entity());
    if (held.is_null() || held->get_owner() == nullptr
        || !property_configs_reader().is_valid()) {
        return;
    }
    const Array declared = book.properties();
    for (int at = 0; at < declared.size(); ++at) {
        const StringName property = declared[at];
        Node *node = column_node(property);
        if (node == nullptr) {
            continue;
        }
        const Dictionary configs = property_configs_reader().call(node);
        const Variant held_config = configs.get(property, Variant());
        Object *cfg = gd::live_object(held_config);
        if (cfg == nullptr
            || int(cfg->get("write_policy")) == int(WritePolicy::AUTHORITY)) {
            continue;
        }
        const bool rides_a_lane = bool(cfg->get("in_state_set"))
            || bool(cfg->get("in_input_set"))
            || int(cfg->get("lane")) == LANE_RETAINED;
        if (!rides_a_lane) {
            NETW_WARN(
                sys::TABLE,
                "persisted field '%s' is client-owned but rides no lane, so "
                "the server never sees the client's value and saves stale "
                "data: add a sync axis or make it authority-written",
                String(property)
            );
            continue;
        }
        const NodePath real_path = held->property_path(node, property, nullptr);
        if (!real_path.is_empty()
            && held->governs_property(real_path, nullptr)) {
            NETW_WARN(
                sys::TABLE,
                "persisted client-owned field '%s' is also governed by a "
                "synchronizer, which is double authority: did you mean an "
                "authority write policy?",
                String(property)
            );
        }
    }
}

void NetwPersistenceEngine::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("meta_database"),
        &NetwPersistenceEngine::meta_database
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("meta_table"),
        &NetwPersistenceEngine::meta_table
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("meta_columns"),
        &NetwPersistenceEngine::meta_columns
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("set_config_reader", "reader"),
        &NetwPersistenceEngine::set_config_reader
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("set_property_configs_reader", "reader"),
        &NetwPersistenceEngine::set_property_configs_reader
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("set_schema_declarer", "declarer"),
        &NetwPersistenceEngine::set_schema_declarer
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("forget_claims"),
        &NetwPersistenceEngine::forget_claims
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("config_of", "owner"),
        &NetwPersistenceEngine::config_of
    );
    ClassDB::bind_static_method(
        "NetwPersistenceEngine",
        D_METHOD("create", "entity", "declaration"),
        &NetwPersistenceEngine::create
    );

    ClassDB::bind_method(
        D_METHOD("owner_node"),
        &NetwPersistenceEngine::owner_node
    );
    ClassDB::bind_method(
        D_METHOD("columns_empty"),
        &NetwPersistenceEngine::columns_empty
    );
    ClassDB::bind_method(
        D_METHOD("database"),
        &NetwPersistenceEngine::database
    );
    ClassDB::bind_method(
        D_METHOD("record_id"),
        &NetwPersistenceEngine::record_id
    );
    ClassDB::bind_method(
        D_METHOD("wants_spawn_hydration"),
        &NetwPersistenceEngine::wants_spawn_hydration
    );
    ClassDB::bind_method(
        D_METHOD("gather", "keys"),
        &NetwPersistenceEngine::gather,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("apply", "data"),
        &NetwPersistenceEngine::apply
    );
    ClassDB::bind_method(
        D_METHOD("is_dirty"),
        &NetwPersistenceEngine::is_dirty
    );
    ClassDB::bind_method(
        D_METHOD("hydrate"),
        &NetwPersistenceEngine::hydrate
    );
    ClassDB::bind_method(
        D_METHOD("flush", "keys"),
        &NetwPersistenceEngine::flush,
        DEFVAL(Array())
    );
    ClassDB::bind_method(
        D_METHOD("snapshot_tick", "delta"),
        &NetwPersistenceEngine::snapshot_tick
    );
    ClassDB::bind_method(
        D_METHOD("commit_snapshot", "values"),
        &NetwPersistenceEngine::commit_snapshot
    );
    ClassDB::bind_method(D_METHOD("lint"), &NetwPersistenceEngine::lint);

    ADD_SIGNAL(MethodInfo("hydrated"));
    ADD_SIGNAL(MethodInfo("flushed"));
}

} // namespace netw

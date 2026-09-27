#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/persistence_handle.hpp"
#include "netw/log.hpp"
#include "netw/persist/binding.hpp"
#include "netw/persist/database.hpp"
#include "netw/persist/envelope.hpp"
#include "netw/persist/snapshot.hpp"
#include "netw/schema_model.hpp"
#include "netw/script/model.hpp"
#include "netw/table/core.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_DATABASE_FAILED = "database_failed";
constexpr const char *SIG_PERSIST_LOADED = "persist_loaded";
constexpr const char *SIG_PERSIST_SAVED = "persist_saved";

const char *STALE_LOAD
    = "the load was canceled, because the session authority or the database "
      "changed while the row was read";
const char *STALE_WRITE
    = "the write settled after the session authority or the database "
      "changed, so its outcome was discarded";

Ref<NetwPromise> unknown_database() {
    return NetwPromise::rejected(
        ERR_DOES_NOT_EXIST,
        "this handle names no database in this session"
    );
}

Ref<NetwPromise> unknown_binding() {
    return NetwPromise::rejected(
        ERR_DOES_NOT_EXIST,
        "this handle names no persistence binding in this session"
    );
}

Ref<NetwPromise> unbound_entity() {
    return NetwPromise::rejected(
        ERR_UNCONFIGURED,
        "this entity binds no persistence, so there is no row to reach"
    );
}

void stage_row(
    persist::Bindings &p_plane,
    const RID &p_binding,
    LocalVector<RID> &r_targets,
    LocalVector<Array> &r_batched
) {
    const persist::Binding *held = p_plane.at(p_binding);
    if (held == nullptr || held->in_flight
        || (held->departed && !held->final_save)
        || p_plane.awaits_load(p_binding) || !p_plane.is_dirty(p_binding)) {
        return;
    }
    const Dictionary values = p_plane.live_values(p_binding);
    if (values.is_empty()) {
        return;
    }
    Dictionary row;
    row["binding"] = p_binding;
    row["values"] = values;
    for (uint32_t at = 0; at < r_targets.size(); ++at) {
        if (r_targets[at] == held->database) {
            r_batched[at].push_back(row);
            return;
        }
    }
    Array made;
    made.push_back(row);
    r_targets.push_back(held->database);
    r_batched.push_back(made);
}

} // namespace

persist::Databases *NetwMultiplayer::get_databases() {
    if (!databases_bound) {
        databases_bound = true;
        databases.bind(
            &schema_core,
            callable_mp(this, &NetwMultiplayer::database_settled),
            callable_mp(this, &NetwMultiplayer::database_connected)
        );
    }
    return &databases;
}

void NetwMultiplayer::database_settled(
    const RID &p_database,
    int64_t p_sequence
) {
    databases.settled(p_database, p_sequence);
}

void NetwMultiplayer::database_connected(
    const RID &p_database,
    int64_t p_generation
) {
    databases.connected(p_database, p_generation);
}

RID NetwMultiplayer::schema_of_declaration(const Ref<NetwSchema> &p_schema) {
    if (p_schema.is_null()) {
        return RID();
    }
    schema_model::adopt(p_schema);
    return schema_find_or_adopt(p_schema->get_schema_name());
}

RID NetwMultiplayer::database_create(
    const StringName &p_name,
    const Ref<NetwDatabaseConfig> &p_config
) {
    const RID existing = get_databases()->find(p_name);
    if (existing.is_valid()) {
        const Ref<NetwDatabaseConfig> held = database_configs.has(p_name)
            ? database_configs[p_name]
            : Ref<NetwDatabaseConfig>();
        if (p_config.is_valid() && held.is_valid()
            && p_config->get_backend() != held->get_backend()) {
            NETW_ERR_V(
                RID(),
                sys::TABLE,
                "database '%s' is already declared in this session with a "
                "different backend",
                String(p_name).utf8().get_data()
            );
        }
        return existing;
    }
    const RID made = get_databases()->create(p_name);
    if (made.is_valid() && p_config.is_valid()) {
        database_configs[p_name] = p_config;
    }
    return made;
}

RID NetwMultiplayer::database_find(const StringName &p_name) const {
    return databases.find(p_name);
}

Ref<NetwDatabase> NetwMultiplayer::database_handle(const RID &p_database) {
    if (!get_databases()->is_valid(p_database)) {
        return Ref<NetwDatabase>();
    }
    const HashMap<RID, Ref<RefCounted>>::Iterator held
        = database_handles.find(p_database);
    if (held != database_handles.end()) {
        return Ref<NetwDatabase>(
            Object::cast_to<NetwDatabase>(held->value.ptr())
        );
    }
    const Ref<NetwDatabase> made = NetwDatabase::over(p_database, this);
    database_handles[p_database] = made;
    return made;
}

Ref<NetwPromise> NetwMultiplayer::database_open(
    const RID &p_database,
    const StringName &p_slot
) {
    persist::Databases *plane = get_databases();
    if (!plane->is_valid(p_database)) {
        return unknown_database();
    }
    const Ref<NetwDatabaseBackend> backend = database_backend_of(p_database);
    if (backend.is_null()) {
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            vformat(
                "database '%s' declares no backend, so there is nothing to "
                "open. Give its config a backend before opening it",
                String(plane->name_of(p_database))
            )
        );
    }
    return plane->open(p_database, p_slot, backend->open(this, p_slot));
}

Ref<NetwDatabaseBackend> NetwMultiplayer::database_backend_of(
    const RID &p_database
) const {
    const StringName name = databases.name_of(p_database);
    const HashMap<StringName, Ref<NetwDatabaseConfig>>::ConstIterator found
        = database_configs.find(name);
    if (found == database_configs.end() || found->value.is_null()) {
        return Ref<NetwDatabaseBackend>();
    }
    return found->value->get_backend();
}

Ref<NetwPromise> NetwMultiplayer::database_close(const RID &p_database) {
    return get_databases()->close(p_database);
}

Ref<NetwPromise> NetwMultiplayer::database_flush(const RID &p_database) {
    return get_databases()->flush(p_database);
}

Ref<NetwPromise> NetwMultiplayer::database_read(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id
) {
    return get_databases()->read(p_database, p_schema, p_id);
}

Ref<NetwPromise> NetwMultiplayer::database_write(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    return get_databases()->write(p_database, p_schema, p_id, p_values);
}

Ref<NetwPromise> NetwMultiplayer::database_patch(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    return get_databases()->patch(p_database, p_schema, p_id, p_values);
}

Ref<NetwPromise> NetwMultiplayer::database_erase(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id
) {
    return get_databases()->erase(p_database, p_schema, p_id);
}

Ref<NetwPromise> NetwMultiplayer::database_scan(
    const RID &p_database,
    const RID &p_schema,
    const Dictionary &p_filter,
    const String &p_cursor,
    int p_limit
) {
    return get_databases()
        ->scan(p_database, p_schema, p_filter, p_cursor, p_limit);
}

Ref<NetwPromise> NetwMultiplayer::database_submit(
    const RID &p_database,
    const Array &p_operations
) {
    return get_databases()->submit(p_database, p_operations);
}

Ref<NetwPromise> NetwMultiplayer::database_list_slots(const RID &p_database) {
    const Ref<NetwDatabaseBackend> backend = database_backend_of(p_database);
    if (backend.is_null()) {
        return NetwPromise::resolved(database_slots_failure(
            ERR_UNCONFIGURED,
            "this database declares no backend to list slots of"
        ));
    }
    const Ref<NetwPromise> asked = backend->list_slots(this);
    Ref<NetwPromise> answer;
    answer.instantiate();
    asked->when_settled(
        callable_mp(this, &NetwMultiplayer::database_slots_settled)
            .bind(asked, answer)
    );
    return answer;
}

void NetwMultiplayer::database_slots_settled(
    const Ref<NetwPromise> &p_asked,
    const Ref<NetwPromise> &p_answer
) {
    if (p_asked.is_null() || p_answer.is_null()) {
        return;
    }
    if (p_asked->get_is_failed()) {
        p_answer->resolve(
            database_slots_failure(p_asked->get_code(), p_asked->get_detail())
        );
        return;
    }
    const Variant answered = p_asked->get_result();
    if (answered.get_type() != Variant::PACKED_STRING_ARRAY) {
        p_answer->resolve(database_slots_failure(
            ERR_INVALID_DATA,
            "the backend answered no PackedStringArray of slots"
        ));
        return;
    }
    p_answer->resolve(database_slots_of(PackedStringArray(answered)));
}

Ref<NetwPromise> NetwMultiplayer::database_delete_slot(
    const RID &p_database,
    const StringName &p_slot
) {
    persist::Databases *plane = get_databases();
    if (!plane->is_valid(p_database)) {
        return unknown_database();
    }
    if (plane->slot_of(p_database) == p_slot) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "slot '%s' is open in this process and cannot be deleted "
                "while it is",
                String(p_slot)
            )
        );
    }
    const Ref<NetwDatabaseBackend> backend = database_backend_of(p_database);
    if (backend.is_null()) {
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            "this database declares no backend to delete a slot of"
        );
    }
    return backend->delete_slot(this, p_slot);
}

StringName NetwMultiplayer::database_get_name(const RID &p_database) const {
    return databases.name_of(p_database);
}

StringName NetwMultiplayer::database_get_slot(const RID &p_database) const {
    return databases.slot_of(p_database);
}

NetwMultiplayer::DatabaseState NetwMultiplayer::database_get_state(
    const RID &p_database
) const {
    return DatabaseState(int(databases.state_of(p_database)));
}

bool NetwMultiplayer::database_is_valid(const RID &p_database) const {
    return databases.is_valid(p_database);
}

Error NetwMultiplayer::database_batch_write(
    Array &r_operations,
    const RID &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    const SchemaRecord *schema = schema_core.record_of(p_schema);
    if (schema == nullptr || !schema->sealed) {
        return ERR_DOES_NOT_EXIST;
    }
    if (String(p_id).is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    String detail;
    const Error checked = persist::validate_row(*schema, p_values, detail);
    if (checked != OK) {
        NETW_ERR_V(checked, sys::TABLE, "%s", detail.utf8().get_data());
    }
    Dictionary operation;
    operation["kind"] = "replace";
    operation["address"] = persist::address_of(
        persist::Kind::RECORD,
        schema->name,
        String(p_id)
    );
    operation["envelope"]
        = persist::seal_record(*schema, schema->storage_version, p_values);
    r_operations.push_back(operation);
    return OK;
}

Error NetwMultiplayer::database_batch_erase(
    Array &r_operations,
    const RID &p_schema,
    const StringName &p_id
) {
    const SchemaRecord *schema = schema_core.record_of(p_schema);
    if (schema == nullptr || !schema->sealed) {
        return ERR_DOES_NOT_EXIST;
    }
    if (String(p_id).is_empty()) {
        return ERR_INVALID_PARAMETER;
    }
    Dictionary operation;
    operation["kind"] = "erase";
    operation["address"] = persist::address_of(
        persist::Kind::RECORD,
        schema->name,
        String(p_id)
    );
    r_operations.push_back(operation);
    return OK;
}

persist::Bindings *NetwMultiplayer::get_bindings() {
    if (!bindings_bound) {
        bindings_bound = true;
        bindings.bind(this);
    }
    return &bindings;
}

RID NetwMultiplayer::persist_bind(Node *p_root) {
    const RID made = get_bindings()->compile(p_root);
    if (made.is_valid()) {
        const Ref<NetwEntity> wrapper = NetwEntity::resolve(p_root);
        if (wrapper.is_valid()) {
            wrapper->get_persistence()->bind_session(this);
        }
    }
    return made;
}

RID NetwMultiplayer::persist_binding_of_entity(const RID &p_entity) const {
    Node *node = entity_get_node(p_entity);
    return node != nullptr ? bindings.find(node) : RID();
}

RID NetwMultiplayer::persist_entity_of_binding(const RID &p_binding) const {
    const persist::Binding *held = bindings.at(p_binding);
    Node *root = held != nullptr
        ? Object::cast_to<Node>(gd::object_of(held->root))
        : nullptr;
    const Ref<NetwEntity> entity = NetwEntity::resolve(root);
    return entity.is_valid() ? entity->get_rid_handle() : RID();
}

int64_t NetwMultiplayer::persist_tenure() const {
    return int64_t(session_core.get_tenure());
}

bool NetwMultiplayer::persist_current(
    int64_t p_tenure,
    const RID &p_database,
    int64_t p_generation
) const {
    return p_tenure == persist_tenure()
        && databases.generation_of(p_database) == p_generation;
}

bool NetwMultiplayer::persist_is_dirty_binding(const RID &p_binding) {
    return get_bindings()->is_dirty(p_binding);
}

RID NetwMultiplayer::persist_get_database(const RID &p_binding) const {
    const persist::Binding *held = bindings.at(p_binding);
    return held != nullptr ? held->database : RID();
}

StringName NetwMultiplayer::persist_get_record_id_binding(
    const RID &p_binding
) const {
    const persist::Binding *held = bindings.at(p_binding);
    return held != nullptr ? held->record_id : StringName();
}

Ref<NetwPromise> NetwMultiplayer::persist_load(const RID &p_entity) {
    const RID binding = persist_binding_of_entity(p_entity);
    if (!binding.is_valid()) {
        return unbound_entity();
    }
    return persist_load_binding(binding);
}

Ref<NetwPromise> NetwMultiplayer::persist_save(const RID &p_entity) {
    const RID binding = persist_binding_of_entity(p_entity);
    if (!binding.is_valid()) {
        return unbound_entity();
    }
    return persist_save_binding(binding);
}

bool NetwMultiplayer::persist_is_dirty(const RID &p_entity) {
    const RID binding = persist_binding_of_entity(p_entity);
    return binding.is_valid() && persist_is_dirty_binding(binding);
}

StringName NetwMultiplayer::persist_get_record_id(const RID &p_entity) const {
    const RID binding = persist_binding_of_entity(p_entity);
    return binding.is_valid() ? persist_get_record_id_binding(binding)
                              : StringName();
}

void NetwMultiplayer::persist_notify_loaded(
    const RID &p_binding,
    bool p_found
) {
    emit_signal(
        StringName(SIG_PERSIST_LOADED),
        persist_entity_of_binding(p_binding),
        p_found
    );
}

void NetwMultiplayer::persist_notify_saved(const RID &p_binding) {
    emit_signal(
        StringName(SIG_PERSIST_SAVED),
        persist_entity_of_binding(p_binding)
    );
}

void NetwMultiplayer::persist_notify_failed(
    const RID &p_database,
    Error p_error,
    const String &p_detail
) {
    NETW_ERROR(
        sys::TABLE,
        "database '%s': %s",
        String(databases.name_of(p_database)).utf8().get_data(),
        p_detail.utf8().get_data()
    );
    emit_signal(
        StringName(SIG_DATABASE_FAILED),
        p_database,
        int(p_error),
        p_detail
    );
}

bool NetwMultiplayer::persist_enroll(Node *p_root) {
    if (p_root == nullptr || !is_host()) {
        return false;
    }
    const Ref<NetwPersistenceConfig> config
        = script::model::get_persistence_config(p_root);
    if (config.is_null()) {
        return false;
    }
    persist::Bindings *plane = get_bindings();
    if (plane->find(p_root).is_valid()) {
        return plane->withholds(p_root);
    }
    const bool gated = config->get_load_at_spawn();
    if (gated) {
        plane->withhold(p_root);
    }
    const RID binding = persist_bind(p_root);
    if (!binding.is_valid() || !gated) {
        return gated;
    }
    const ObjectID outer = persist_enrolling;
    persist_enrolling = gd::instance_id(p_root);
    persist_begin_load(binding);
    persist_enrolling = outer;
    return plane->withholds(p_root);
}

bool NetwMultiplayer::persist_withholds(Node *p_root) const {
    return bindings_bound && bindings.withholds(p_root);
}

void NetwMultiplayer::persist_publish(const RID &p_binding) {
    persist::Bindings *plane = get_bindings();
    const persist::Binding *held = plane->at(p_binding);
    Node *root = held != nullptr
        ? Object::cast_to<Node>(gd::object_of(held->root))
        : nullptr;
    if (root == nullptr || !plane->withholds(root)) {
        return;
    }
    plane->publish(root);
    const Ref<NetwEntity> entity = NetwEntity::resolve(root);
    if (entity.is_valid()) {
        sync_pipeline_recapture_entity(entity);
        const Ref<NetwDisplayHandle> display = entity->get_interpolation();
        if (display.is_valid()) {
            display->reset();
        }
        if (gd::instance_id(root) != persist_enrolling) {
            predict_reconcile_declaration(entity);
        }
    }
    spawn_schedule_visibility_sweep();
}

Ref<NetwPromise> NetwMultiplayer::persist_begin_load(const RID &p_binding) {
    persist::Binding *held = get_bindings()->at(p_binding);
    const int64_t tenure = persist_tenure();
    const int64_t generation = databases.generation_of(held->database);
    const Ref<NetwPromise> asked
        = database_read(held->database, held->schema, held->record_id);
    Ref<NetwPromise> answer;
    answer.instantiate();
    held->loading = answer;
    asked->when_settled(
        callable_mp(this, &NetwMultiplayer::persist_read_settled)
            .bind(p_binding, asked, answer, tenure, generation)
    );
    return answer;
}

Ref<NetwPromise> NetwMultiplayer::persist_load_binding(const RID &p_binding) {
    persist::Bindings *plane = get_bindings();
    persist::Binding *held = plane->at(p_binding);
    if (held == nullptr) {
        return unknown_binding();
    }
    if (held->loading.is_valid()) {
        return held->loading;
    }
    if (!is_host()) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            vformat(
                "record '%s' is loaded by the session authority, and its "
                "values reach this peer by replication",
                String(held->record_id)
            )
        );
    }
    if (held->in_flight) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "record '%s' has a write in flight. Load it after the write "
                "settles",
                String(held->record_id)
            )
        );
    }
    if (plane->has_unsaved_changes(p_binding)) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "record '%s' holds unsaved changes, and a load would discard "
                "them. Save it before loading it again",
                String(held->record_id)
            )
        );
    }
    return persist_begin_load(p_binding);
}

void NetwMultiplayer::persist_load_failed(
    const RID &p_binding,
    const Ref<NetwPromise> &p_answer,
    Error p_error,
    const String &p_detail
) {
    const persist::Binding *held = get_bindings()->at(p_binding);
    if (held != nullptr) {
        persist_notify_failed(
            held->database,
            p_error,
            vformat(
                "loading record '%s' failed, so its entity is not published. "
                "%s",
                String(held->record_id),
                p_detail
            )
        );
    }
    p_answer->reject(p_error, p_detail);
}

void NetwMultiplayer::persist_read_settled(
    const RID &p_binding,
    const Ref<NetwPromise> &p_asked,
    const Ref<NetwPromise> &p_answer,
    int64_t p_tenure,
    int64_t p_generation
) {
    if (p_asked.is_null() || p_answer.is_null()) {
        return;
    }
    persist::Bindings *plane = get_bindings();
    persist::Binding *held = plane->at(p_binding);
    if (held == nullptr) {
        p_answer->reject(ERR_DOES_NOT_EXIST, "the binding is gone");
        return;
    }
    held->loading = Ref<NetwPromise>();
    const RID database = held->database;
    if (!persist_current(p_tenure, database, p_generation)) {
        p_answer->reject(ERR_UNAVAILABLE, STALE_LOAD);
        return;
    }
    if (p_asked->get_is_failed()) {
        persist_load_failed(
            p_binding,
            p_answer,
            p_asked->get_code(),
            p_asked->get_detail()
        );
        return;
    }
    const Dictionary read = p_asked->get_result();
    const Error read_error
        = Error(int(read.get("error", int(ERR_INVALID_DATA))));
    if (read_error != OK) {
        persist_load_failed(
            p_binding,
            p_answer,
            read_error,
            String(read.get("detail", "the database answered no record"))
        );
        return;
    }
    if (plane->has_unsaved_changes(p_binding)) {
        persist_load_failed(
            p_binding,
            p_answer,
            ERR_BUSY,
            "its bound properties changed while the row was being read, and "
            "the stored row would overwrite those changes"
        );
        return;
    }
    const bool found = bool(read.get("found", false));
    if (found) {
        if (!plane->apply(p_binding, read.get("values", Dictionary()))) {
            persist_load_failed(
                p_binding,
                p_answer,
                ERR_DOES_NOT_EXIST,
                "a bound node went away while the stored row was applied"
            );
            return;
        }
        if (!persist_current(p_tenure, database, p_generation)) {
            p_answer->reject(ERR_UNAVAILABLE, STALE_LOAD);
            return;
        }
        plane->adopt(p_binding, plane->live_values(p_binding));
    } else {
        held->reference = plane->live_values(p_binding);
    }
    persist_publish(p_binding);
    persist_notify_loaded(p_binding, found);
    p_answer->resolve(found);
}

Ref<NetwPromise> NetwMultiplayer::persist_submit(
    const RID &p_database,
    const Array &p_rows
) {
    Array operations;
    for (int at = 0; at < p_rows.size(); ++at) {
        const Dictionary row = p_rows[at];
        const persist::Binding *held = bindings.at(row["binding"]);
        if (held == nullptr) {
            return NetwPromise::rejected(
                ERR_DOES_NOT_EXIST,
                "a due binding went away before its save was staged"
            );
        }
        const Error staged = database_batch_write(
            operations,
            held->schema,
            held->record_id,
            row["values"]
        );
        if (staged != OK) {
            return NetwPromise::rejected(
                staged,
                vformat(
                    "record '%s' holds a row its schema cannot store",
                    String(held->record_id)
                )
            );
        }
    }
    return database_submit(p_database, operations);
}

Ref<NetwPromise> NetwMultiplayer::persist_save_binding(const RID &p_binding) {
    persist::Bindings *plane = get_bindings();
    persist::Binding *held = plane->at(p_binding);
    if (held == nullptr) {
        return unknown_binding();
    }
    if (!is_host()) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            vformat(
                "record '%s' is owned by the session authority, and this "
                "peer's copy of its properties is a replicated view rather "
                "than the values to save",
                String(held->record_id)
            )
        );
    }
    if (held->in_flight) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "record '%s' already has a write in flight",
                String(held->record_id)
            )
        );
    }
    if (plane->awaits_load(p_binding)) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "record '%s' has not finished loading, and saving now would "
                "write the values it was spawned with over the stored row",
                String(held->record_id)
            )
        );
    }
    const Dictionary values = plane->live_values(p_binding);
    if (values.is_empty()) {
        return NetwPromise::rejected(
            ERR_UNAVAILABLE,
            vformat(
                "record '%s' binds nodes that are gone, so there is nothing "
                "left to read a row out of",
                String(held->record_id)
            )
        );
    }
    if (!plane->is_dirty(p_binding)) {
        return NetwPromise::resolved(false);
    }
    Dictionary row;
    row["binding"] = p_binding;
    row["values"] = values;
    Array rows;
    rows.push_back(row);

    Ref<NetwPromise> answer;
    answer.instantiate();
    Dictionary tally;
    tally["left"] = 1;
    persist_write_rows(held->database, rows, answer, tally);
    return answer;
}

void NetwMultiplayer::persist_write_rows(
    const RID &p_database,
    const Array &p_rows,
    const Ref<NetwPromise> &p_answer,
    const Dictionary &p_tally
) {
    const int64_t tenure = persist_tenure();
    const int64_t generation = databases.generation_of(p_database);
    const Ref<NetwPromise> asked = persist_submit(p_database, p_rows);
    for (int at = 0; at < p_rows.size(); ++at) {
        const Dictionary row = p_rows[at];
        persist::Binding *held = bindings.at(row["binding"]);
        if (held != nullptr) {
            held->in_flight = true;
        }
    }
    asked->when_settled(
        callable_mp(this, &NetwMultiplayer::persist_batch_settled)
            .bind(
                p_rows,
                asked,
                p_answer,
                p_tally,
                tenure,
                p_database,
                generation
            )
    );
}

void NetwMultiplayer::persist_batch_settled(
    const Array &p_rows,
    const Ref<NetwPromise> &p_asked,
    const Ref<NetwPromise> &p_answer,
    Dictionary p_tally,
    int64_t p_tenure,
    const RID &p_database,
    int64_t p_generation
) {
    if (p_asked.is_null()) {
        return;
    }
    persist::Bindings *plane = get_bindings();
    const bool current = persist_current(p_tenure, p_database, p_generation);
    const Dictionary result = p_asked->get_result();
    const bool refused = p_asked->get_is_failed() || result.is_empty();
    const PackedInt32Array errors
        = refused ? PackedInt32Array() : PackedInt32Array(result["errors"]);
    const PackedByteArray uncertain
        = refused ? PackedByteArray() : PackedByteArray(result["uncertain"]);

    Error error = OK;
    String detail;
    if (!current) {
        error = ERR_UNAVAILABLE;
        detail = STALE_WRITE;
    } else if (refused) {
        error
            = p_asked->get_is_failed() ? p_asked->get_code() : ERR_INVALID_DATA;
        detail = p_asked->get_is_failed()
            ? p_asked->get_detail()
            : String("the database answered no batch outcome");
    } else if (Error(int(result["error"])) != OK) {
        error = Error(int(result["error"]));
        detail = String(result["detail"]);
    }

    LocalVector<RID> saved;
    LocalVector<RID> retiring;
    int unsaved = 0;
    Error first_error = OK;
    StringName first_record;
    String first_failure;
    for (int at = 0; at < p_rows.size(); ++at) {
        const Dictionary row = p_rows[at];
        const RID handle = row["binding"];
        persist::Binding *held = plane->at(handle);
        if (held == nullptr) {
            continue;
        }
        held->in_flight = false;
        Error outcome = current && !refused ? OK : error;
        if (outcome == OK) {
            outcome = at < errors.size() ? Error(errors[at]) : ERR_INVALID_DATA;
        }
        const bool doubted
            = outcome == OK && at < uncertain.size() && uncertain[at] != 0;
        if (doubted) {
            outcome = ERR_UNAVAILABLE;
        }
        held->failed = outcome;
        held->failure = outcome == OK || !detail.is_empty() ? detail
            : doubted
            ? String("the database could not confirm this row was stored")
            : String("the database refused this row");
        if (outcome == OK) {
            plane->adopt(handle, row["values"]);
            saved.push_back(handle);
        } else if (current) {
            if (unsaved == 0) {
                first_error = outcome;
                first_record = held->record_id;
                first_failure = held->failure;
            }
            unsaved += 1;
        }
        if (held->retiring) {
            retiring.push_back(handle);
        }
    }
    for (uint32_t at = 0; at < saved.size(); ++at) {
        if (plane->is_valid(saved[at])) {
            persist_notify_saved(saved[at]);
        }
    }
    if (unsaved > 0) {
        persist_notify_failed(
            p_database,
            first_error,
            unsaved == 1
                ? vformat(
                      "record '%s' was not saved. %s",
                      String(first_record),
                      first_failure
                  )
                : vformat(
                      "%d entity rows were not saved. Record '%s' was not "
                      "saved. %s",
                      unsaved,
                      String(first_record),
                      first_failure
                  )
        );
    }
    for (uint32_t at = 0; at < retiring.size(); ++at) {
        persist_finish(retiring[at]);
    }

    if (error == OK && unsaved > 0) {
        error = first_error;
        detail = first_failure;
    }
    const int left = int(p_tally.get("left", 1)) - 1;
    p_tally["left"] = left;
    if (error != OK) {
        p_tally["error"] = int(error);
        p_tally["detail"] = detail;
    }
    if (p_answer.is_null() || left > 0) {
        return;
    }
    if (p_tally.has("error")) {
        p_answer->reject(
            Error(int(p_tally["error"])),
            String(p_tally.get("detail", ""))
        );
        return;
    }
    p_answer->resolve(true);
}

void NetwMultiplayer::persist_depart(const RID &p_binding, bool p_save) {
    get_bindings()->depart(p_binding, p_save);
}

void NetwMultiplayer::persist_capture_exit(Node *p_owner) {
    if (!bindings_bound) {
        return;
    }
    bindings.capture(bindings.find(p_owner));
}

void NetwMultiplayer::persist_settle_departure(
    ObjectID p_owner,
    bool p_terminal
) {
    if (!bindings_bound) {
        return;
    }
    const RID binding = bindings.find_id(p_owner);
    if (!p_terminal) {
        bindings.keep(binding);
        return;
    }
    persist::Binding *held = bindings.at(binding);
    if (held == nullptr) {
        bindings.unwithhold(p_owner);
        return;
    }
    const bool unloaded = bindings.awaits_load(binding);
    bindings.depart(binding, true);
    held->retiring = true;
    if (unloaded) {
        bindings.release(binding);
        return;
    }
    persist_finish(binding);
}

void NetwMultiplayer::persist_finish(const RID &p_binding) {
    persist::Binding *held = bindings.at(p_binding);
    if (held == nullptr || !held->retiring || held->in_flight) {
        return;
    }
    const bool owed = held->final_save && is_host()
        && !bindings.awaits_load(p_binding) && bindings.is_dirty(p_binding);
    if (owed && !held->final_sent) {
        held->final_sent = true;
        LocalVector<RID> last;
        last.push_back(p_binding);
        persist_write(last);
        return;
    }
    if (!owed) {
        bindings.release(p_binding);
    }
}

void NetwMultiplayer::persist_write(const LocalVector<RID> &p_bindings) {
    LocalVector<RID> targets;
    LocalVector<Array> batched;
    for (uint32_t at = 0; at < p_bindings.size(); ++at) {
        stage_row(bindings, p_bindings[at], targets, batched);
    }
    for (uint32_t at = 0; at < targets.size(); ++at) {
        Dictionary tally;
        tally["left"] = 1;
        persist_write_rows(targets[at], batched[at], Ref<NetwPromise>(), tally);
    }
}

void NetwMultiplayer::persist_pump(double p_delta) {
    if (GDVIRTUAL_CALL(_persist_tick, p_delta)) {
        return;
    }
    persist_tick_default(p_delta);
}

void NetwMultiplayer::persist_tick_default(double p_delta) {
    if (!bindings_bound || !is_host()) {
        return;
    }
    persist_write(bindings.due(p_delta));
}

Ref<NetwPromise> NetwMultiplayer::persist_flush_all() {
    if (!is_host()) {
        return NetwPromise::resolved(int(ERR_UNAUTHORIZED));
    }
    persist_write(get_bindings()->enrolled());
    Ref<NetwPromise> answer;
    answer.instantiate();
    LocalVector<RID> writing;
    const LocalVector<RID> &enrolled = get_bindings()->enrolled();
    for (uint32_t at = 0; at < enrolled.size(); ++at) {
        const persist::Binding *held = bindings.at(enrolled[at]);
        if (held != nullptr && !writing.has(held->database)) {
            writing.push_back(held->database);
        }
    }
    if (writing.is_empty()) {
        persist_flush_judge(answer);
        return answer;
    }
    Dictionary tally;
    tally["left"] = int(writing.size());
    for (uint32_t at = 0; at < writing.size(); ++at) {
        database_flush(writing[at])
            ->when_settled(
                callable_mp(this, &NetwMultiplayer::persist_flush_waited)
                    .bind(answer, tally)
            );
    }
    return answer;
}

void NetwMultiplayer::persist_flush_waited(
    const Ref<NetwPromise> &p_answer,
    Dictionary p_tally
) {
    const int left = int(p_tally.get("left", 1)) - 1;
    p_tally["left"] = left;
    if (left == 0) {
        persist_flush_judge(p_answer);
    }
}

void NetwMultiplayer::persist_flush_judge(const Ref<NetwPromise> &p_answer) {
    const LocalVector<RID> &enrolled = get_bindings()->enrolled();
    for (uint32_t at = 0; at < enrolled.size(); ++at) {
        const persist::Binding *held = bindings.at(enrolled[at]);
        if (held != nullptr && held->failed != OK
            && bindings.is_dirty(enrolled[at])) {
            p_answer->resolve(int(held->failed));
            return;
        }
    }
    p_answer->resolve(int(OK));
}

void NetwMultiplayer::persist_dispose() {
    session_core.end_tenure();
    if (!bindings_bound) {
        return;
    }
    LocalVector<Ref<NetwPromise>> loading;
    const LocalVector<RID> &enrolled = bindings.enrolled();
    for (uint32_t at = 0; at < enrolled.size(); ++at) {
        const persist::Binding *held = bindings.at(enrolled[at]);
        if (held != nullptr && held->loading.is_valid()) {
            loading.push_back(held->loading);
        }
    }
    bindings.clear();
    for (uint32_t at = 0; at < loading.size(); ++at) {
        loading[at]->reject(
            ERR_UNAVAILABLE,
            "the session ended before the row was read"
        );
    }
}

Ref<NetwPromise> NetwMultiplayer::table_save(
    const RID &p_table,
    const RID &p_database,
    const StringName &p_key,
    const PackedStringArray &p_ids
) {
    if (!is_host()) {
        return NetwPromise::rejected(
            ERR_UNAUTHORIZED,
            "a peer holding no session authority saves no table"
        );
    }
    const SchemaRecord *schema
        = schema_core.record_of(table_get_schema(p_table));
    if (!table_core->is_valid(p_table) || schema == nullptr
        || !schema->sealed) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this handle names no table in this session"
        );
    }
    if (String(p_key).is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a snapshot key may not be empty"
        );
    }
    for (int at = 0; at < schema->column_count(); ++at) {
        if (schema->at(at)->type == SchemaCore::ENTITY) {
            return NetwPromise::rejected(
                ERR_INVALID_PARAMETER,
                vformat(
                    "column '%s' holds entity routes, which name rows of "
                    "this session only, so table '%s' cannot be saved",
                    String(schema->at(at)->key),
                    String(schema->name)
                )
            );
        }
    }
    const PackedInt64Array routes = table_core->read_routes(p_table);
    String detail;
    const Error named = persist::validate_ids(p_ids, routes.size(), detail);
    if (named != OK) {
        return NetwPromise::rejected(named, detail);
    }
    LocalVector<Variant> columns;
    columns.reserve(uint32_t(schema->column_count()));
    for (int at = 0; at < schema->column_count(); ++at) {
        columns.push_back(table_core->read_column(p_table, at));
    }
    Dictionary operation;
    operation["kind"] = "replace";
    operation["address"] = persist::address_of(
        persist::Kind::SNAPSHOT,
        schema->name,
        String(p_key)
    );
    operation["envelope"] = persist::seal_snapshot(
        *schema,
        schema->storage_version,
        p_ids,
        columns
    );
    Array operations;
    operations.push_back(operation);
    const Ref<NetwPromise> asked = database_submit(p_database, operations);
    Ref<NetwPromise> answer;
    answer.instantiate();
    asked->when_settled(callable_mp(this, &NetwMultiplayer::persist_table_saved)
                            .bind(asked, answer));
    return answer;
}

void NetwMultiplayer::persist_table_saved(
    const Ref<NetwPromise> &p_asked,
    const Ref<NetwPromise> &p_answer
) {
    if (p_asked.is_null() || p_answer.is_null()) {
        return;
    }
    if (p_asked->get_is_failed()) {
        p_answer->resolve(int(p_asked->get_code()));
        return;
    }
    const Dictionary result = p_asked->get_result();
    p_answer->resolve(int(result.get("error", int(ERR_INVALID_DATA))));
}

Ref<NetwPromise> NetwMultiplayer::table_load(
    const RID &p_table,
    const RID &p_database,
    const StringName &p_key
) {
    if (!is_host()) {
        return NetwPromise::resolved(table_load_failure(
            ERR_UNAUTHORIZED,
            "a peer holding no session authority loads no table"
        ));
    }
    if (!table_core->is_valid(p_table)) {
        return NetwPromise::resolved(table_load_failure(
            ERR_DOES_NOT_EXIST,
            "this handle names no table in this session"
        ));
    }
    const int64_t tenure = persist_tenure();
    const int64_t generation = databases.generation_of(p_database);
    const Ref<NetwPromise> asked = get_databases()->read_snapshot(
        p_database,
        table_get_schema(p_table),
        p_key
    );
    Ref<NetwPromise> answer;
    answer.instantiate();
    asked->when_settled(callable_mp(this, &NetwMultiplayer::persist_table_read)
                            .bind(
                                p_table,
                                table_core->revision(p_table),
                                asked,
                                answer,
                                tenure,
                                p_database,
                                generation
                            ));
    return answer;
}

void NetwMultiplayer::persist_table_read(
    const RID &p_table,
    int64_t p_revision,
    const Ref<NetwPromise> &p_asked,
    const Ref<NetwPromise> &p_answer,
    int64_t p_tenure,
    const RID &p_database,
    int64_t p_generation
) {
    if (p_asked.is_null() || p_answer.is_null()) {
        return;
    }
    if (!persist_current(p_tenure, p_database, p_generation)) {
        p_answer->resolve(table_load_failure(ERR_UNAVAILABLE, STALE_LOAD));
        return;
    }
    if (p_asked->get_is_failed()) {
        p_answer->resolve(
            table_load_failure(p_asked->get_code(), p_asked->get_detail())
        );
        return;
    }
    const Dictionary reply = p_asked->get_result();
    const Error reported = Error(int(reply.get("error", int(FAILED))));
    if (reported != OK) {
        p_answer->resolve(
            table_load_failure(reported, String(reply.get("detail", "")))
        );
        return;
    }
    if (!bool(reply.get("found", false))) {
        p_answer->resolve(table_load_miss());
        return;
    }
    const RID schema = table_get_schema(p_table);
    PackedStringArray ids;
    LocalVector<Variant> columns;
    String detail;
    const Error opened = persist::open_snapshot(
        schema_core,
        schema,
        reply.get("envelope", Dictionary()),
        ids,
        columns,
        detail
    );
    if (opened != OK) {
        p_answer->resolve(table_load_failure(opened, detail));
        return;
    }
    if (table_core->revision(p_table) != p_revision) {
        p_answer->resolve(table_load_failure(
            ERR_BUSY,
            "the table committed new rows while its snapshot was read, "
            "and the snapshot would replace them"
        ));
        return;
    }
    PackedInt64Array routes;
    routes.resize(ids.size());
    for (int at = 0; at < ids.size(); ++at) {
        routes.set(at, liveness_reserve_route());
    }
    const Error replaced = table_core->replace_rows(
        p_table,
        routes,
        columns,
        clock_engine().get_tick()
    );
    if (replaced != OK) {
        p_answer->resolve(table_load_failure(
            replaced,
            "the snapshot's columns do not fit the table's schema"
        ));
        return;
    }
    liveness_bind_routes_data(routes);
    const HashMap<RID, PackedInt64Array>::Iterator held
        = table_snapshot_routes.find(p_table);
    const PackedInt64Array previous = held != table_snapshot_routes.end()
        ? held->value
        : PackedInt64Array();
    table_snapshot_routes[p_table] = routes;
    persist_table_retire(p_table, previous);
    p_answer->resolve(table_load_of(ids, routes));
}

void NetwMultiplayer::persist_table_retire(
    const RID &p_table,
    const PackedInt64Array &p_routes
) {
    PackedInt64Array retiring;
    for (int at = 0; at < p_routes.size(); ++at) {
        if (table_core->row_of(p_table, p_routes[at]) < 0
            && !table_core->holds_elsewhere(p_table, p_routes[at])) {
            retiring.push_back(p_routes[at]);
        }
    }
    liveness_release_routes(retiring);
}

} // namespace netw

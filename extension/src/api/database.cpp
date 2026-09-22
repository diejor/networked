#include "netw/api/database.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"
#include "netw/persist/envelope.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_DATABASE_FAILED = "database_failed";

Ref<NetwPromise> no_session() {
    return NetwPromise::rejected(
        ERR_UNAVAILABLE,
        "this database handle outlived the session that issued it"
    );
}

PropertyInfo read_only(Variant::Type p_type, const char *p_name) {
    return PropertyInfo(
        p_type,
        p_name,
        PROPERTY_HINT_NONE,
        "",
        PROPERTY_USAGE_NONE
    );
}

} // namespace

NetwMultiplayer *NetwDatabase::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

void NetwDatabase::relay_failed(
    const RID &p_database,
    int64_t p_error,
    const String &p_detail
) {
    if (p_database == database) {
        emit_signal(StringName("failed"), p_error, p_detail);
    }
}

Ref<NetwDatabase> NetwDatabase::over(
    const RID &p_database,
    NetwMultiplayer *p_session
) {
    if (p_session == nullptr || !p_database.is_valid()) {
        return Ref<NetwDatabase>();
    }
    Ref<NetwDatabase> handle;
    handle.instantiate();
    handle->database = p_database;
    handle->session_id = gd::instance_id(p_session);
    p_session->connect(
        StringName(SIG_DATABASE_FAILED),
        callable_mp(handle.ptr(), &NetwDatabase::relay_failed)
    );
    return handle;
}

Ref<NetwDatabase> NetwDatabase::of(Node *p_node, const StringName &p_name) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::TABLE,
            "Netw.database: no session governs the given node"
        );
        return Ref<NetwDatabase>();
    }
    const RID found = api->database_find(p_name);
    if (!found.is_valid()) {
        NETW_ERROR(
            sys::TABLE,
            "Netw.database: no database named '%s' is declared in this "
            "session. Declare it with Netw.configure_database first",
            String(p_name).utf8().get_data()
        );
        return Ref<NetwDatabase>();
    }
    return api->database_handle(found);
}

Ref<NetwPromise> NetwDatabase::open(const StringName &p_slot) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_open(database, p_slot) : no_session();
}

Ref<NetwPromise> NetwDatabase::close() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_close(database) : no_session();
}

Ref<NetwPromise> NetwDatabase::flush() {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_flush(database) : no_session();
}

Ref<NetwPromise> NetwDatabase::read(
    const Ref<NetwSchema> &p_schema,
    const StringName &p_id
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwPromise::resolved(
            database_read_failure(
                p_id,
                ERR_UNAVAILABLE,
                "this database handle outlived the session that issued it"
            )
        );
    }
    return api->database_read(
        database,
        api->schema_of_declaration(p_schema),
        p_id
    );
}

Ref<NetwPromise> NetwDatabase::write(
    const Ref<NetwSchema> &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_write(
                                database,
                                api->schema_of_declaration(p_schema),
                                p_id,
                                p_values
                            )
                          : no_session();
}

Ref<NetwPromise> NetwDatabase::patch(
    const Ref<NetwSchema> &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_patch(
                                database,
                                api->schema_of_declaration(p_schema),
                                p_id,
                                p_values
                            )
                          : no_session();
}

Ref<NetwPromise> NetwDatabase::erase(
    const Ref<NetwSchema> &p_schema,
    const StringName &p_id
) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_erase(
                                database,
                                api->schema_of_declaration(p_schema),
                                p_id
                            )
                          : no_session();
}

Ref<NetwPromise> NetwDatabase::scan(
    const Ref<NetwSchema> &p_schema,
    const Dictionary &p_filter,
    const String &p_cursor,
    int p_limit
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwPromise::resolved(
            database_page_failure(
                ERR_UNAVAILABLE,
                "this database handle outlived the session that issued it"
            )
        );
    }
    return api->database_scan(
        database,
        api->schema_of_declaration(p_schema),
        p_filter,
        p_cursor,
        p_limit
    );
}

Ref<NetwPromise> NetwDatabase::list_slots() {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return NetwPromise::resolved(
            database_slots_failure(
                ERR_UNAVAILABLE,
                "this database handle outlived the session that issued it"
            )
        );
    }
    return api->database_list_slots(database);
}

Ref<NetwPromise> NetwDatabase::delete_slot(const StringName &p_slot) {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_delete_slot(database, p_slot)
                          : no_session();
}

Ref<NetwWriteBatch> NetwDatabase::batch() {
    return NetwWriteBatch::over(database, session());
}

StringName NetwDatabase::get_database_name() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_get_name(database) : StringName();
}

StringName NetwDatabase::get_slot() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_get_slot(database) : StringName();
}

NetwMultiplayer::DatabaseState NetwDatabase::get_state() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->database_get_state(database)
                          : NetwMultiplayer::DATABASE_CLOSED;
}

bool NetwDatabase::get_is_valid() const {
    NetwMultiplayer *api = session();
    return api != nullptr && api->database_is_valid(database);
}

void NetwDatabase::_bind_methods() {
    ClassDB::bind_method(D_METHOD("open", "slot"), &NetwDatabase::open);
    ClassDB::bind_method(D_METHOD("close"), &NetwDatabase::close);
    ClassDB::bind_method(D_METHOD("flush"), &NetwDatabase::flush);
    ClassDB::bind_method(
        D_METHOD("read", "schema", "id"),
        &NetwDatabase::read
    );
    ClassDB::bind_method(
        D_METHOD("write", "schema", "id", "values"),
        &NetwDatabase::write
    );
    ClassDB::bind_method(
        D_METHOD("patch", "schema", "id", "values"),
        &NetwDatabase::patch
    );
    ClassDB::bind_method(
        D_METHOD("erase", "schema", "id"),
        &NetwDatabase::erase
    );
    ClassDB::bind_method(
        D_METHOD("scan", "schema", "filter", "cursor", "limit"),
        &NetwDatabase::scan,
        DEFVAL(Dictionary()),
        DEFVAL(String()),
        DEFVAL(100)
    );
    ClassDB::bind_method(D_METHOD("list_slots"), &NetwDatabase::list_slots);
    ClassDB::bind_method(
        D_METHOD("delete_slot", "slot"),
        &NetwDatabase::delete_slot
    );
    ClassDB::bind_method(D_METHOD("batch"), &NetwDatabase::batch);

    ClassDB::bind_method(
        D_METHOD("get_database_name"),
        &NetwDatabase::get_database_name
    );
    ADD_PROPERTY(read_only(Variant::STRING_NAME, "name"), "", "get_database_name");
    ClassDB::bind_method(D_METHOD("get_slot"), &NetwDatabase::get_slot);
    ADD_PROPERTY(read_only(Variant::STRING_NAME, "slot"), "", "get_slot");
    ClassDB::bind_method(D_METHOD("get_state"), &NetwDatabase::get_state);
    ADD_PROPERTY(read_only(Variant::INT, "state"), "", "get_state");
    ClassDB::bind_method(D_METHOD("get_is_valid"), &NetwDatabase::get_is_valid);
    ADD_PROPERTY(read_only(Variant::BOOL, "is_valid"), "", "get_is_valid");

    ADD_SIGNAL(MethodInfo(
        "failed",
        PropertyInfo(Variant::INT, "error"),
        PropertyInfo(Variant::STRING, "detail")
    ));
}

NetwMultiplayer *NetwWriteBatch::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

Ref<NetwWriteBatch> NetwWriteBatch::over(
    const RID &p_database,
    NetwMultiplayer *p_session
) {
    Ref<NetwWriteBatch> made;
    made.instantiate();
    made->database = p_database;
    if (p_session != nullptr) {
        made->session_id = gd::instance_id(p_session);
    }
    return made;
}

Error NetwWriteBatch::write(
    const Ref<NetwSchema> &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        first_fault = first_fault == OK ? ERR_UNAVAILABLE : first_fault;
        return ERR_UNAVAILABLE;
    }
    if (sealed) {
        NETW_ERROR(
            sys::TABLE,
            "this batch was submitted already, so it takes no more work"
        );
        first_fault = first_fault == OK ? ERR_LOCKED : first_fault;
        return ERR_LOCKED;
    }
    const Error added = api->database_batch_write(
        operations,
        api->schema_of_declaration(p_schema),
        p_id,
        p_values
    );
    if (added != OK && first_fault == OK) {
        first_fault = added;
    }
    return added;
}

Error NetwWriteBatch::erase(
    const Ref<NetwSchema> &p_schema,
    const StringName &p_id
) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        first_fault = first_fault == OK ? ERR_UNAVAILABLE : first_fault;
        return ERR_UNAVAILABLE;
    }
    if (sealed) {
        NETW_ERROR(
            sys::TABLE,
            "this batch was submitted already, so it takes no more work"
        );
        first_fault = first_fault == OK ? ERR_LOCKED : first_fault;
        return ERR_LOCKED;
    }
    const Error added = api->database_batch_erase(
        operations,
        api->schema_of_declaration(p_schema),
        p_id
    );
    if (added != OK && first_fault == OK) {
        first_fault = added;
    }
    return added;
}

Ref<NetwPromise> NetwWriteBatch::submit() {
    NetwMultiplayer *api = session();
    const int count = operations.size();
    if (sealed) {
        return NetwPromise::resolved(
            database_batch_result_refused(
                count,
                ERR_LOCKED,
                "this batch was submitted already"
            )
        );
    }
    sealed = true;
    if (api == nullptr) {
        return NetwPromise::resolved(
            database_batch_result_refused(
                count,
                ERR_UNAVAILABLE,
                "this batch outlived the session that issued it"
            )
        );
    }
    if (first_fault != OK) {
        return NetwPromise::resolved(
            database_batch_result_refused(
                count,
                first_fault,
                "a builder call on this batch was refused, so none of it ran"
            )
        );
    }
    return api->database_submit(database, operations);
}

void NetwWriteBatch::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("write", "schema", "id", "values"),
        &NetwWriteBatch::write
    );
    ClassDB::bind_method(
        D_METHOD("erase", "schema", "id"),
        &NetwWriteBatch::erase
    );
    ClassDB::bind_method(D_METHOD("submit"), &NetwWriteBatch::submit);
    ClassDB::bind_method(D_METHOD("get_size"), &NetwWriteBatch::get_size);
    ADD_PROPERTY(read_only(Variant::INT, "size"), "", "get_size");
}

} // namespace netw

#include "netw/api/table.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *SIG_RECEIVED = "received";

} // namespace

NetwMultiplayer *NetwTable::session() const {
    return Object::cast_to<NetwMultiplayer>(gd::object_of(session_id));
}

Ref<NetwTable> NetwTable::over(const RID &p_table, NetwMultiplayer *p_session) {
    if (p_session == nullptr || !p_table.is_valid()) {
        return Ref<NetwTable>();
    }
    Ref<NetwTable> handle;
    handle.instantiate();
    handle->table = p_table;
    handle->session_id = gd::instance_id(p_session);
    return handle;
}

Ref<NetwTable> NetwTable::of(Node *p_node, const StringName &p_name) {
    NetwMultiplayer *api = NetwMultiplayer::of(p_node);
    if (api == nullptr) {
        NETW_ERROR(
            sys::TABLE,
            "NetwTable.of got a node that is not inside a session. Call it "
            "from a node in the scene tree under a MultiplayerTree."
        );
        return Ref<NetwTable>();
    }
    return named(api, p_name);
}

Ref<NetwTable> NetwTable::named(
    NetwMultiplayer *p_session,
    const StringName &p_name
) {
    const RID table = p_session->table_find_or_adopt(p_name);
    if (!table.is_valid()) {
        NETW_ERROR(
            sys::TABLE,
            "There is no schema named '%s'. Declare it with "
            "Netw.configure_schema first.",
            String(p_name).utf8().get_data()
        );
        return Ref<NetwTable>();
    }
    return p_session->get_table_handle(table);
}

void NetwTable::announce(int64_t p_tick) {
    emit_signal(SIG_RECEIVED, p_tick);
}

RID NetwTable::get_rid() const {
    return table;
}

StringName NetwTable::get_schema_name() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return StringName();
    }
    return api->schema_get_name(api->table_get_schema(table));
}

bool NetwTable::get_is_valid() const {
    return session() != nullptr && table.is_valid();
}

int NetwTable::get_wire_hash() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_get_wire_hash(table) : 0;
}

int64_t NetwTable::get_tick() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_get_tick(table) : -1;
}

void NetwTable::set_reliable(bool p_reliable) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return;
    }
    api->table_set_param(
        table,
        NetwMultiplayer::TABLE_PARAM_RELIABLE,
        p_reliable
    );
}

bool NetwTable::get_reliable() const {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return false;
    }
    return api->store_arrays().is_reliable(table);
}

Error NetwTable::write_routes(const PackedInt64Array &p_routes) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    return api->table_write_routes(table, p_routes);
}

Error NetwTable::write_column(int p_column, const Variant &p_data) {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    return api->table_write_column(table, p_column, p_data);
}

Error NetwTable::commit() {
    NetwMultiplayer *api = session();
    if (api == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    return api->table_commit(table);
}

PackedInt64Array NetwTable::read_routes() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_read_routes(table) : PackedInt64Array();
}

Variant NetwTable::read_column(int p_column) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_read_column(table, p_column) : Variant();
}

PackedInt64Array NetwTable::read_births() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_read_births(table) : PackedInt64Array();
}

PackedInt64Array NetwTable::read_deaths() const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_read_deaths(table) : PackedInt64Array();
}

int NetwTable::row_of(int64_t p_route) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_get_row(table, p_route) : -1;
}

PackedInt32Array NetwTable::rows_of(const PackedInt64Array &p_routes) const {
    NetwMultiplayer *api = session();
    return api != nullptr ? api->table_get_rows(table, p_routes)
                          : PackedInt32Array();
}

Ref<NetwPromise> NetwTable::save(
    const Ref<NetwDatabase> &p_database,
    const StringName &p_key,
    const PackedStringArray &p_ids
) {
    NetwMultiplayer *api = session();
    if (api == nullptr || p_database.is_null() || !p_database->serves(api)) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "the table and the database belong to different sessions"
        );
    }
    return api->table_save(table, p_database->get_database(), p_key, p_ids);
}

Ref<NetwPromise> NetwTable::load(
    const Ref<NetwDatabase> &p_database,
    const StringName &p_key
) {
    NetwMultiplayer *api = session();
    if (api == nullptr || p_database.is_null() || !p_database->serves(api)) {
        return NetwPromise::resolved(table_load_failure(
            ERR_DOES_NOT_EXIST,
            "the table and the database belong to different sessions"
        ));
    }
    return api->table_load(table, p_database->get_database(), p_key);
}

void NetwTable::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwTable",
        D_METHOD("of", "node", "name"),
        &NetwTable::of
    );

    ClassDB::bind_method(D_METHOD("get_rid"), &NetwTable::get_rid);
    ClassDB::bind_method(
        D_METHOD("get_schema_name"),
        &NetwTable::get_schema_name
    );
    ClassDB::bind_method(D_METHOD("get_is_valid"), &NetwTable::get_is_valid);
    ClassDB::bind_method(D_METHOD("get_wire_hash"), &NetwTable::get_wire_hash);
    ClassDB::bind_method(D_METHOD("get_tick"), &NetwTable::get_tick);

    ClassDB::bind_method(
        D_METHOD("set_reliable", "reliable"),
        &NetwTable::set_reliable
    );
    ClassDB::bind_method(D_METHOD("get_reliable"), &NetwTable::get_reliable);

    ClassDB::bind_method(
        D_METHOD("write_routes", "routes"),
        &NetwTable::write_routes
    );
    ClassDB::bind_method(
        D_METHOD("write_column", "column", "data"),
        &NetwTable::write_column
    );
    ClassDB::bind_method(D_METHOD("commit"), &NetwTable::commit);

    ClassDB::bind_method(D_METHOD("read_routes"), &NetwTable::read_routes);
    ClassDB::bind_method(
        D_METHOD("read_column", "column"),
        &NetwTable::read_column
    );
    ClassDB::bind_method(D_METHOD("read_births"), &NetwTable::read_births);
    ClassDB::bind_method(D_METHOD("read_deaths"), &NetwTable::read_deaths);
    ClassDB::bind_method(D_METHOD("row_of", "route"), &NetwTable::row_of);
    ClassDB::bind_method(D_METHOD("rows_of", "routes"), &NetwTable::rows_of);
    ClassDB::bind_method(
        D_METHOD("save", "database", "key", "ids"),
        &NetwTable::save
    );
    ClassDB::bind_method(D_METHOD("load", "database", "key"), &NetwTable::load);

    ADD_PROPERTY(PropertyInfo(Variant::RID, "rid"), "", "get_rid");
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "schema_name"),
        "",
        "get_schema_name"
    );
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "is_valid"), "", "get_is_valid");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "wire_hash"), "", "get_wire_hash");
    ADD_PROPERTY(PropertyInfo(Variant::INT, "tick"), "", "get_tick");
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "reliable"),
        "set_reliable",
        "get_reliable"
    );

    ADD_SIGNAL(MethodInfo(SIG_RECEIVED, PropertyInfo(Variant::INT, "tick")));
}

} // namespace netw

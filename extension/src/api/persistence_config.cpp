#include "netw/api/persistence_config.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Ref<NetwPersistenceConfig> NetwPersistenceConfig::database(
    const Ref<NetwDatabase> &p_database
) {
    db = p_database;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::table(
    const StringName &p_name
) {
    table_name = p_name;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::interval(double p_seconds) {
    default_interval = p_seconds;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::hydrate_on_spawn(
    bool p_enabled
) {
    hydrate_on_spawn_enabled = p_enabled;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::record_id(
    const Callable &p_callable
) {
    record_id_provider = p_callable.get_method();
    return Ref<NetwPersistenceConfig>(this);
}

void NetwPersistenceConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("database", "database"),
        &NetwPersistenceConfig::database
    );
    ClassDB::bind_method(
        D_METHOD("table", "name"),
        &NetwPersistenceConfig::table
    );
    ClassDB::bind_method(
        D_METHOD("interval", "seconds"),
        &NetwPersistenceConfig::interval
    );
    ClassDB::bind_method(
        D_METHOD("hydrate_on_spawn", "enabled"),
        &NetwPersistenceConfig::hydrate_on_spawn,
        DEFVAL(true)
    );
    ClassDB::bind_method(
        D_METHOD("record_id", "callable"),
        &NetwPersistenceConfig::record_id
    );

#define NETW_PERSISTENCE_CONFIG_PROPERTY(m_type, m_name) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, #m_name), \
        &NetwPersistenceConfig::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwPersistenceConfig::get_##m_name \
    ); \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name), "set_" #m_name, "get_" #m_name)

    ClassDB::bind_method(
        D_METHOD("set_db", "db"),
        &NetwPersistenceConfig::set_db
    );
    ClassDB::bind_method(D_METHOD("get_db"), &NetwPersistenceConfig::get_db);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "db",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDatabase"
        ),
        "set_db",
        "get_db"
    );

    NETW_PERSISTENCE_CONFIG_PROPERTY(Variant::STRING_NAME, table_name);
    NETW_PERSISTENCE_CONFIG_PROPERTY(Variant::FLOAT, default_interval);
    NETW_PERSISTENCE_CONFIG_PROPERTY(Variant::BOOL, hydrate_on_spawn_enabled);
    NETW_PERSISTENCE_CONFIG_PROPERTY(Variant::STRING_NAME, record_id_provider);

#undef NETW_PERSISTENCE_CONFIG_PROPERTY
}

} // namespace netw

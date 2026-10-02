#include "netw/api/persistence_config.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

Ref<NetwPersistenceConfig> NetwPersistenceConfig::database(
    const StringName &p_name
) {
    database_name = p_name;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::table(
    const StringName &p_name
) {
    table_name = p_name;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::storage_version(
    int p_version
) {
    NETW_ERR_COND_V(
        p_version < 1,
        Ref<NetwPersistenceConfig>(this),
        sys::TABLE,
        "NetwPersistenceConfig.storage_version got %d. Versions start at 1.",
        p_version
    );
    version = p_version;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::migrate(
    int p_from_version,
    const Callable &p_step
) {
    NETW_ERR_COND_V(
        p_from_version < 1 || p_step.is_null(),
        Ref<NetwPersistenceConfig>(this),
        sys::TABLE,
        "NetwPersistenceConfig.migrate needs a version of 1 or more and a "
        "valid step."
    );
    migrations[p_from_version] = p_step;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::record_id(
    const Callable &p_provider
) {
    id_provider = p_provider;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::interval(double p_seconds) {
    save_interval = p_seconds;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::load_on_spawn(
    bool p_enabled
) {
    load_at_spawn = p_enabled;
    return Ref<NetwPersistenceConfig>(this);
}

void NetwPersistenceConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("database", "name"),
        &NetwPersistenceConfig::database
    );
    ClassDB::bind_method(
        D_METHOD("table", "name"),
        &NetwPersistenceConfig::table
    );
    ClassDB::bind_method(
        D_METHOD("storage_version", "version"),
        &NetwPersistenceConfig::storage_version
    );
    ClassDB::bind_method(
        D_METHOD("migrate", "from_version", "step"),
        &NetwPersistenceConfig::migrate
    );
    ClassDB::bind_method(
        D_METHOD("record_id", "provider"),
        &NetwPersistenceConfig::record_id
    );
    ClassDB::bind_method(
        D_METHOD("interval", "seconds"),
        &NetwPersistenceConfig::interval
    );
    ClassDB::bind_method(
        D_METHOD("load_on_spawn", "enabled"),
        &NetwPersistenceConfig::load_on_spawn,
        DEFVAL(true)
    );
}

} // namespace netw

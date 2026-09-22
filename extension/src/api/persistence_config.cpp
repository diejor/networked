#include "netw/api/persistence_config.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Ref<NetwPersistenceConfig> NetwPersistenceConfig::database(
    const StringName &p_name
) {
    database_name = p_name;
    return Ref<NetwPersistenceConfig>(this);
}

Ref<NetwPersistenceConfig> NetwPersistenceConfig::schema(
    const Ref<NetwSchema> &p_schema
) {
    declared_schema = p_schema;
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
        D_METHOD("schema", "schema"),
        &NetwPersistenceConfig::schema
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

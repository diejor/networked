#include "netw/api/database_config.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwDatabaseConfig::copy_values_from(const NetwDatabaseConfig &p_other) {
    store = p_other.store;
}

Ref<NetwDatabaseConfig> NetwDatabaseConfig::backend(
    const Ref<NetwDatabaseBackend> &p_backend
) {
    store = p_backend;
    return Ref<NetwDatabaseConfig>(this);
}

void NetwDatabaseConfig::set_backend(const Ref<NetwDatabaseBackend> &p_backend
) {
    store = p_backend;
}

void NetwDatabaseConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("backend", "backend"),
        &NetwDatabaseConfig::backend
    );
    ClassDB::bind_method(
        D_METHOD("set_backend", "backend"),
        &NetwDatabaseConfig::set_backend
    );
    ClassDB::bind_method(
        D_METHOD("get_backend"),
        &NetwDatabaseConfig::get_backend
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "store",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwDatabaseBackend"
        ),
        "set_backend",
        "get_backend"
    );
}

} // namespace netw

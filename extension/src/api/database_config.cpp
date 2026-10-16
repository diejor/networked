#include "netw/api/database_config.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

Ref<NetwDatabaseConfig> NetwDatabaseConfig::backend(
    const Ref<NetwDatabaseBackend> &p_backend
) {
    store = p_backend;
    return Ref<NetwDatabaseConfig>(this);
}

void NetwDatabaseConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("backend", "backend"),
        &NetwDatabaseConfig::backend
    );
}

} // namespace netw

#pragma once

#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/database_backend.hpp"

namespace netw {

class NetwDatabaseConfig : public godot::Resource {
    GDCLASS(NetwDatabaseConfig, godot::Resource)

    godot::Ref<NetwDatabaseBackend> store;

protected:
    static void _bind_methods();

public:
    void copy_values_from(const NetwDatabaseConfig &p_other);

    godot::Ref<NetwDatabaseConfig> backend(
        const godot::Ref<NetwDatabaseBackend> &p_backend
    );
    void set_backend(const godot::Ref<NetwDatabaseBackend> &p_backend);
    godot::Ref<NetwDatabaseBackend> get_backend() const {
        return store;
    }
};

} // namespace netw

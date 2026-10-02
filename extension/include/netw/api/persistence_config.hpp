#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwPersistenceConfig : public godot::RefCounted {
    GDCLASS(NetwPersistenceConfig, godot::RefCounted)

private:
    godot::StringName database_name;
    godot::StringName table_name;
    int version = 1;
    godot::Dictionary migrations;
    godot::Callable id_provider;
    double save_interval = 0.0;
    bool load_at_spawn = true;

protected:
    static void _bind_methods();

public:
    godot::StringName get_database_name() const {
        return database_name;
    }

    godot::StringName get_table_name() const {
        return table_name;
    }

    int get_storage_version() const {
        return version;
    }

    godot::Dictionary get_migrations() const {
        return migrations;
    }

    godot::Callable get_id_provider() const {
        return id_provider;
    }

    double get_save_interval() const {
        return save_interval;
    }

    bool get_load_at_spawn() const {
        return load_at_spawn;
    }

    godot::Ref<NetwPersistenceConfig> database(const godot::StringName &p_name);
    godot::Ref<NetwPersistenceConfig> table(const godot::StringName &p_name);
    godot::Ref<NetwPersistenceConfig> storage_version(int p_version);
    godot::Ref<NetwPersistenceConfig> migrate(
        int p_from_version,
        const godot::Callable &p_step
    );
    godot::Ref<NetwPersistenceConfig> record_id(
        const godot::Callable &p_provider
    );
    godot::Ref<NetwPersistenceConfig> interval(double p_seconds);
    godot::Ref<NetwPersistenceConfig> load_on_spawn(bool p_enabled);
};

} // namespace netw

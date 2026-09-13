#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/database.hpp"

namespace netw {

class NetwPersistenceConfig : public godot::RefCounted {
    GDCLASS(NetwPersistenceConfig, godot::RefCounted)

private:
    godot::Ref<NetwDatabase> db;
    godot::StringName table_name;
    double default_interval = 5.0;
    bool hydrate_on_spawn_enabled = true;
    godot::StringName record_id_provider;

protected:
    static void _bind_methods();

public:
    void set_db(const godot::Ref<NetwDatabase> &p_db) {
        db = p_db;
    }
    godot::Ref<NetwDatabase> get_db() const {
        return db;
    }

    void set_table_name(const godot::StringName &p_table_name) {
        table_name = p_table_name;
    }
    godot::StringName get_table_name() const {
        return table_name;
    }

    void set_default_interval(double p_default_interval) {
        default_interval = p_default_interval;
    }
    double get_default_interval() const {
        return default_interval;
    }

    void set_hydrate_on_spawn_enabled(bool p_enabled) {
        hydrate_on_spawn_enabled = p_enabled;
    }
    bool get_hydrate_on_spawn_enabled() const {
        return hydrate_on_spawn_enabled;
    }

    void set_record_id_provider(const godot::StringName &p_provider) {
        record_id_provider = p_provider;
    }
    godot::StringName get_record_id_provider() const {
        return record_id_provider;
    }

    godot::Ref<NetwPersistenceConfig> database(
        const godot::Ref<NetwDatabase> &p_database
    );
    godot::Ref<NetwPersistenceConfig> table(const godot::StringName &p_name);
    godot::Ref<NetwPersistenceConfig> interval(double p_seconds);
    godot::Ref<NetwPersistenceConfig> hydrate_on_spawn(bool p_enabled);
    godot::Ref<NetwPersistenceConfig> record_id(
        const godot::Callable &p_callable
    );
};

} // namespace netw

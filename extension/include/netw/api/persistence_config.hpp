#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_model.hpp"

namespace netw {

class NetwPersistenceConfig : public godot::RefCounted {
    GDCLASS(NetwPersistenceConfig, godot::RefCounted)

private:
    godot::StringName database_name;
    godot::Ref<NetwSchema> declared_schema;
    godot::Callable id_provider;
    double save_interval = 0.0;
    bool load_at_spawn = true;

protected:
    static void _bind_methods();

public:
    void set_database_name(const godot::StringName &p_name) {
        database_name = p_name;
    }
    godot::StringName get_database_name() const {
        return database_name;
    }

    void set_schema(const godot::Ref<NetwSchema> &p_schema) {
        declared_schema = p_schema;
    }
    godot::Ref<NetwSchema> get_schema() const {
        return declared_schema;
    }

    void set_id_provider(const godot::Callable &p_provider) {
        id_provider = p_provider;
    }
    godot::Callable get_id_provider() const {
        return id_provider;
    }

    void set_save_interval(double p_seconds) {
        save_interval = p_seconds;
    }
    double get_save_interval() const {
        return save_interval;
    }

    void set_load_at_spawn(bool p_enabled) {
        load_at_spawn = p_enabled;
    }
    bool get_load_at_spawn() const {
        return load_at_spawn;
    }

    godot::Ref<NetwPersistenceConfig> database(const godot::StringName &p_name);
    godot::Ref<NetwPersistenceConfig> schema(
        const godot::Ref<NetwSchema> &p_schema
    );
    godot::Ref<NetwPersistenceConfig> record_id(
        const godot::Callable &p_provider
    );
    godot::Ref<NetwPersistenceConfig> interval(double p_seconds);
    godot::Ref<NetwPersistenceConfig> load_on_spawn(bool p_enabled);
};

} // namespace netw

#pragma once

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/database.hpp"
#include "netw/api/promise.hpp"
#include "netw/snapshot_book.hpp"

namespace netw {

struct PersistedWrite {
    godot::Ref<NetwDatabase> database;
    godot::StringName table;
    godot::StringName record;
    godot::Dictionary values;

    bool is_addressed() const {
        return database.is_valid() && table != godot::StringName();
    }
};

class NetwPersistenceEngine : public godot::RefCounted {
    GDCLASS(NetwPersistenceEngine, godot::RefCounted)

private:
    godot::ObjectID entity_id;
    godot::Ref<NetwDatabase> declared_database;
    godot::StringName declared_table;
    godot::StringName record_id_provider;
    bool hydrate_on_spawn = false;
    SnapshotBook book;
    godot::HashMap<godot::StringName, godot::ObjectID> column_nodes;
    bool schema_registered = false;

    godot::Object *entity() const;
    godot::Node *owner() const;
    godot::Node *column_node(const godot::StringName &p_property) const;
    godot::StringName table_name() const;

    void build_columns();
    void ensure_schema();
    void claim_record_id(const godot::Ref<NetwDatabase> &p_database);
    void warn_duplicate(const godot::StringName &p_property) const;
    void settle_hydrated(
        const godot::Dictionary &p_record,
        const godot::Ref<NetwPromise> &p_answer
    );
    void settle_flushed(
        const godot::Variant &p_result,
        const godot::Dictionary &p_subset,
        const godot::Ref<NetwPromise> &p_answer
    );
    void settle_refused(
        int p_code,
        const godot::String &p_detail,
        const godot::Ref<NetwPromise> &p_answer
    );

protected:
    static void _bind_methods();

public:
    static godot::StringName meta_database();
    static godot::StringName meta_table();
    static godot::StringName meta_columns();

    static void set_config_reader(const godot::Callable &p_reader);
    static void set_property_configs_reader(const godot::Callable &p_reader);
    static void set_schema_declarer(const godot::Callable &p_declarer);
    static void forget_claims();

    static godot::Dictionary config_of(godot::Object *p_owner);
    static godot::Ref<NetwPersistenceEngine> create(
        godot::Object *p_entity,
        const godot::Dictionary &p_declaration
    );

    godot::Node *owner_node() const;
    bool columns_empty() const;
    godot::Ref<NetwDatabase> database() const;
    godot::StringName record_id() const;
    bool wants_spawn_hydration() const;

    godot::Dictionary gather(const godot::Array &p_keys) const;
    void apply(const godot::Dictionary &p_data);
    bool is_dirty() const;

    godot::Ref<NetwPromise> hydrate();
    godot::Ref<NetwPromise> flush(const godot::Array &p_keys);
    PersistedWrite capture_write() const;
    godot::Ref<NetwPromise> submit(const PersistedWrite &p_write);

    godot::Dictionary snapshot_tick(double p_delta);
    void commit_snapshot(const godot::Dictionary &p_values);

    void lint();
};

} // namespace netw

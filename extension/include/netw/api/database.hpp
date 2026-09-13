#pragma once

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/hash_map.hpp"
#include "godot/multiplayer.hpp"
#include "godot/resource.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/record_table.hpp"
#include "netw/api/warm_policy.hpp"

namespace netw {

class NetwTransaction;

class NetwDatabase : public godot::Resource {
    GDCLASS(NetwDatabase, godot::Resource)

public:
    enum SchemaMismatchPolicy {
        PURGE,
        LOAD_PARTIAL,
        FAIL,
    };

private:
    godot::Ref<NetwDatabaseBackend> backend;
    SchemaMismatchPolicy mismatch_policy = LOAD_PARTIAL;
    godot::Ref<WarmPolicy> warm_policy;
    bool warm_policy_assigned = false;
    mutable godot::Ref<WarmPolicy> stock_warm_policy;

    const godot::Ref<WarmPolicy> &warm_policy_in_force() const;

    godot::HashMap<godot::StringName, godot::TypedArray<godot::StringName>>
        schema;
    godot::HashMap<godot::StringName, godot::HashMap<godot::StringName, int>>
        column_types;
    godot::HashMap<godot::StringName, godot::Ref<godot::Script>> table_scripts;

    godot::StringName slot = godot::StringName("default");
    bool slot_locked = false;
    bool initialized = false;

    void register_schema(
        const godot::StringName &table,
        const godot::TypedArray<godot::StringName> &columns
    );
    void initialize_backend();
    void refuse_init(int code, const godot::String &detail);
    void warm_declared(const godot::Variant &outcome);
    godot::Array build_warm_directives() const;

    godot::Dictionary diff_record(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &record
    );
    godot::Dictionary apply_mismatch_policy(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &record,
        const godot::Dictionary &diff,
        int &r_error
    );
    godot::Dictionary reject_mistyped(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &record
    ) const;
    void settle_found(
        const godot::Dictionary &record,
        const godot::Ref<NetwPromise> &answer,
        const godot::StringName &table,
        const godot::StringName &id
    );
    void settle_unread(
        int code,
        const godot::String &detail,
        const godot::Ref<NetwPromise> &answer,
        const godot::StringName &table,
        const godot::StringName &id
    );
    void announce_committed(
        const godot::Variant &outcome,
        int table_count,
        int row_count
    );
    void queue_table_record(
        const godot::Ref<NetwTransaction> &transaction,
        const godot::StringName &into,
        const godot::StringName &record,
        const godot::Dictionary &values
    );
    void settle_hydrated(
        const godot::Dictionary &data,
        const godot::Ref<NetwPromise> &answer,
        const godot::Ref<godot::MultiplayerAPI> &session,
        const godot::RID &table
    );
    void settle_unhydrated(
        int code,
        const godot::String &detail,
        const godot::Ref<NetwPromise> &answer,
        const godot::Dictionary &empty
    );

protected:
    static void _bind_methods();

public:
    void set_backend(const godot::Ref<NetwDatabaseBackend> &p_backend);
    godot::Ref<NetwDatabaseBackend> get_backend() const {
        return backend;
    }

    void set_mismatch_policy(SchemaMismatchPolicy p_policy);
    SchemaMismatchPolicy get_mismatch_policy() const {
        return mismatch_policy;
    }

    void set_warm_policy(const godot::Ref<WarmPolicy> &p_policy);
    godot::Ref<WarmPolicy> get_warm_policy() const {
        return warm_policy;
    }

    godot::Ref<NetwRecordTable> table(const godot::StringName &table_name);
    void declare_table(
        const godot::StringName &table_name,
        const godot::Variant &declaration = godot::Variant(),
        const godot::Ref<godot::Script> &record_script
        = godot::Ref<godot::Script>()
    );
    int get_column_type(
        const godot::StringName &table,
        const godot::StringName &column
    ) const;
    godot::TypedArray<godot::StringName> get_registered_columns(
        const godot::StringName &table
    ) const;

    godot::Ref<NetwPromise> find(
        const godot::StringName &table,
        const godot::StringName &id
    );
    godot::Ref<NetwPromise> find_all(
        const godot::StringName &table,
        const godot::Dictionary &filter = godot::Dictionary()
    );
    godot::Ref<NetwPromise> transaction(const godot::Callable &body);
    godot::Ref<NetwPromise> erase(
        const godot::StringName &table,
        const godot::StringName &id
    );
    godot::Ref<NetwPromise> warm(
        const godot::StringName &table,
        const godot::Ref<WarmRequest> &request
    );

    godot::Ref<NetwPromise> table_flush(
        const godot::Ref<godot::MultiplayerAPI> &session,
        const godot::RID &table,
        const godot::StringName &into,
        const godot::PackedStringArray &ids
    );
    godot::Ref<NetwPromise> table_hydrate(
        const godot::Ref<godot::MultiplayerAPI> &session,
        const godot::RID &table,
        const godot::StringName &into
    );

    void open_slot(const godot::StringName &p_slot);
    godot::StringName current_slot() const {
        return slot;
    }
    godot::Ref<NetwPromise> list_slots();
    godot::Ref<NetwPromise> delete_slot(const godot::StringName &p_slot);

    bool _get(const godot::StringName &property, godot::Variant &r_ret) const;
    void _get_property_list(godot::List<godot::PropertyInfo> *p_list) const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwDatabase::SchemaMismatchPolicy);

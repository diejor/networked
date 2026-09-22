#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/database_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/schema_model.hpp"

namespace netw {

class NetwWriteBatch;

class NetwDatabase : public godot::RefCounted {
    GDCLASS(NetwDatabase, godot::RefCounted)

    godot::ObjectID session_id;
    godot::RID database;

    NetwMultiplayer *session() const;
    void relay_failed(
        const godot::RID &p_database,
        int64_t p_error,
        const godot::String &p_detail
    );

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwDatabase> over(
        const godot::RID &p_database,
        NetwMultiplayer *p_session
    );
    static godot::Ref<NetwDatabase> of(
        godot::Node *p_node,
        const godot::StringName &p_name
    );

    godot::RID get_database() const {
        return database;
    }
    bool serves(const NetwMultiplayer *p_session) const {
        return p_session != nullptr && session() == p_session;
    }

    godot::Ref<NetwPromise> open(const godot::StringName &p_slot);
    godot::Ref<NetwPromise> close();
    godot::Ref<NetwPromise> flush();

    godot::Ref<NetwPromise> read(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::StringName &p_id
    );
    godot::Ref<NetwPromise> write(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::StringName &p_id,
        const godot::Dictionary &p_values
    );
    godot::Ref<NetwPromise> patch(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::StringName &p_id,
        const godot::Dictionary &p_values
    );
    godot::Ref<NetwPromise> erase(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::StringName &p_id
    );
    godot::Ref<NetwPromise> scan(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::Dictionary &p_filter,
        const godot::String &p_cursor,
        int p_limit
    );
    godot::Ref<NetwPromise> list_slots();
    godot::Ref<NetwPromise> delete_slot(const godot::StringName &p_slot);

    godot::Ref<NetwWriteBatch> batch();

    godot::StringName get_database_name() const;
    godot::StringName get_slot() const;
    NetwMultiplayer::DatabaseState get_state() const;
    bool get_is_valid() const;
};

class NetwWriteBatch : public godot::RefCounted {
    GDCLASS(NetwWriteBatch, godot::RefCounted)

    godot::ObjectID session_id;
    godot::RID database;
    godot::Array operations;
    godot::Error first_fault = godot::OK;
    bool sealed = false;

    NetwMultiplayer *session() const;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwWriteBatch> over(
        const godot::RID &p_database,
        NetwMultiplayer *p_session
    );

    godot::Error write(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::StringName &p_id,
        const godot::Dictionary &p_values
    );
    godot::Error erase(
        const godot::Ref<NetwSchema> &p_schema,
        const godot::StringName &p_id
    );
    godot::Ref<NetwPromise> submit();
    int get_size() const {
        return operations.size();
    }
};

} // namespace netw

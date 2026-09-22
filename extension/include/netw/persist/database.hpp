#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/database_connection.hpp"
#include "netw/api/database_result.hpp"
#include "netw/api/promise.hpp"
#include "netw/table/schema_record.hpp"

namespace netw {
class SchemaCore;
} // namespace netw

namespace netw::persist {

using netw::table::SchemaRecord;

enum class State {
    CLOSED = 0,
    OPENING = 1,
    OPEN = 2,
    CLOSING = 3,
    FAULTED = 4,
};

enum class Op {
    READ,
    WRITE,
    PATCH,
    ERASE,
    SCAN,
    BATCH,
    SNAPSHOT,
};

struct Pending {
    int64_t sequence = 0;
    Op op = Op::READ;
    godot::LocalVector<godot::String> locks;
    godot::Dictionary address;
    godot::Dictionary envelope;
    godot::Dictionary request;
    godot::Array operations;
    godot::Dictionary patch_values;
    godot::RID schema;
    godot::StringName id;
    godot::Ref<NetwPromise> answer;
    godot::Ref<NetwPromise> outstanding;
    int64_t generation = 0;
    bool issued = false;
    bool patching = false;
};

class Databases {
    struct Instance {
        godot::StringName name;
        godot::StringName slot;
        godot::StringName opening_slot;
        State state = State::CLOSED;
        godot::Ref<NetwDatabaseConnection> connection;
        godot::Ref<NetwPromise> opening;
        godot::Ref<NetwPromise> connecting;
        godot::Ref<NetwPromise> closing;
        godot::HashMap<int64_t, godot::Ref<NetwPromise>> abandoned;
        int64_t generation = 1;
        int64_t next_sequence = 1;
        godot::LocalVector<Pending> queue;
        godot::HashMap<godot::String, int64_t> held;
        godot::LocalVector<godot::Ref<NetwPromise>> flush_waiters;
    };

    mutable godot::RID_PtrOwner<Instance> instances;
    godot::HashMap<godot::StringName, godot::RID> by_name;
    SchemaCore *schemas = nullptr;
    godot::Callable on_settled;
    godot::Callable on_connected;
    int queue_limit = 4096;

    Instance *at(const godot::RID &p_database);
    const Instance *at(const godot::RID &p_database) const;

    void pump(const godot::RID &p_database);
    void issue(const godot::RID &p_database, int p_index);
    godot::Ref<NetwPromise> admit(
        const godot::RID &p_database,
        Pending &p_pending
    );
    void retire(Instance *p_instance, int p_index);
    void release_waiters(Instance *p_instance);
    void discard(Instance *p_instance, int64_t p_generation);
    void fail_queue(
        Instance *p_instance,
        godot::Error p_error,
        const godot::String &p_detail
    );
    const SchemaRecord *sealed(const godot::RID &p_schema) const;
    godot::Error stage_patch(
        Instance *p_instance,
        int p_index,
        const godot::Dictionary &p_reply
    );
    godot::Variant unpack(
        Op p_op,
        const godot::RID &p_schema,
        const godot::StringName &p_id,
        int p_count,
        const godot::Dictionary &p_reply
    );

public:
    Databases();
    ~Databases();

    void bind(
        SchemaCore *p_schemas,
        const godot::Callable &p_settled,
        const godot::Callable &p_connected
    );

    godot::RID create(const godot::StringName &p_name);
    godot::RID find(const godot::StringName &p_name) const;
    bool is_valid(const godot::RID &p_database) const;
    void clear();

    godot::StringName name_of(const godot::RID &p_database) const;
    godot::StringName slot_of(const godot::RID &p_database) const;
    State state_of(const godot::RID &p_database) const;
    int64_t generation_of(const godot::RID &p_database) const;
    godot::Ref<NetwDatabaseConnection> connection_of(
        const godot::RID &p_database
    ) const;

    godot::Ref<NetwPromise> open(
        const godot::RID &p_database,
        const godot::StringName &p_slot,
        const godot::Ref<NetwPromise> &p_connecting
    );
    void connected(const godot::RID &p_database, int64_t p_generation);
    void settled(const godot::RID &p_database, int64_t p_sequence);

    godot::Ref<NetwPromise> close(const godot::RID &p_database);
    godot::Ref<NetwPromise> flush(const godot::RID &p_database);

    godot::Ref<NetwPromise> read(
        const godot::RID &p_database,
        const godot::RID &p_schema,
        const godot::StringName &p_id
    );
    godot::Ref<NetwPromise> read_snapshot(
        const godot::RID &p_database,
        const godot::RID &p_schema,
        const godot::StringName &p_key
    );
    godot::Ref<NetwPromise> write(
        const godot::RID &p_database,
        const godot::RID &p_schema,
        const godot::StringName &p_id,
        const godot::Dictionary &p_values
    );
    godot::Ref<NetwPromise> patch(
        const godot::RID &p_database,
        const godot::RID &p_schema,
        const godot::StringName &p_id,
        const godot::Dictionary &p_values
    );
    godot::Ref<NetwPromise> erase(
        const godot::RID &p_database,
        const godot::RID &p_schema,
        const godot::StringName &p_id
    );
    godot::Ref<NetwPromise> scan(
        const godot::RID &p_database,
        const godot::RID &p_schema,
        const godot::Dictionary &p_filter,
        const godot::String &p_cursor,
        int p_limit
    );
    godot::Ref<NetwPromise> submit(
        const godot::RID &p_database,
        const godot::Array &p_operations
    );
};

} // namespace netw::persist

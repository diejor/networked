#pragma once

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/database.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/promise.hpp"

namespace netw_test {

class NetwTestPersistenceEngine : public godot::RefCounted {
    GDCLASS(NetwTestPersistenceEngine, godot::RefCounted)

    godot::Object *owner = nullptr;
    godot::Dictionary due;
    godot::Array committed_values;
    double advanced = 0.0;
    int tick_calls = 0;
    int flush_calls = 0;

protected:
    static void _bind_methods() {
        godot::ClassDB::bind_method(
            D_METHOD("owner_node"),
            &NetwTestPersistenceEngine::owner_node
        );
        godot::ClassDB::bind_method(
            D_METHOD("snapshot_tick", "delta"),
            &NetwTestPersistenceEngine::snapshot_tick
        );
        godot::ClassDB::bind_method(
            D_METHOD("flush"),
            &NetwTestPersistenceEngine::flush
        );
        godot::ClassDB::bind_method(
            D_METHOD("commit_snapshot", "values"),
            &NetwTestPersistenceEngine::commit_snapshot
        );
    }

public:
    void set_owner(godot::Object *p_owner) {
        owner = p_owner;
    }

    void set_due(const godot::Dictionary &p_due) {
        due = p_due;
    }

    godot::Object *owner_node() const {
        return owner;
    }

    godot::Dictionary snapshot_tick(double p_delta) {
        tick_calls += 1;
        advanced += p_delta;
        return due;
    }

    void flush() {
        flush_calls += 1;
    }

    void commit_snapshot(const godot::Dictionary &p_values) {
        committed_values.push_back(p_values);
    }

    int tick_count() const {
        return tick_calls;
    }

    int flush_count() const {
        return flush_calls;
    }

    double advanced_by() const {
        return advanced;
    }

    godot::Array committed() const {
        return committed_values;
    }
};

class RecordingBackend : public netw::NetwDatabaseBackend {
    godot::Array commit_rows;
    godot::Array read_ids;
    godot::Dictionary stored;
    int commit_calls = 0;
    int read_calls = 0;
    int outcome = 0;

public:
    void set_outcome(int p_outcome) {
        outcome = p_outcome;
    }

    void set_stored(const godot::Dictionary &p_stored) {
        stored = p_stored;
    }

    int transaction_count() const {
        return commit_calls;
    }

    godot::Array upserts() const {
        return commit_rows;
    }

    int read_count() const {
        return read_calls;
    }

    godot::Array reads() const {
        return read_ids;
    }

    godot::Ref<netw::NetwPromise> commit(
        const godot::Array &p_operations
    ) override {
        commit_calls += 1;
        for (int at = 0; at < p_operations.size(); ++at) {
            commit_rows.push_back(p_operations[at]);
        }
        return netw::NetwPromise::resolved(int64_t(outcome));
    }

    godot::Ref<netw::NetwPromise> find_by_id(
        const godot::StringName &p_table,
        const godot::StringName &p_id
    ) override {
        read_calls += 1;
        read_ids.push_back(p_id);
        return netw::NetwPromise::resolved(stored);
    }
};

struct DatabaseStand {
    godot::Ref<netw::NetwDatabase> db;
    godot::Ref<RecordingBackend> backend;

    DatabaseStand() {
        db.instantiate();
        backend = godot::Ref<RecordingBackend>(memnew(RecordingBackend));
        db->set_backend(backend);
    }

    void declare(
        const godot::StringName &p_table,
        const godot::Array &p_columns
    ) {
        db->declare_table(p_table, p_columns);
    }

    void set_outcome(int p_outcome) {
        backend->set_outcome(p_outcome);
    }

    void set_stored(const godot::Dictionary &p_stored) {
        backend->set_stored(p_stored);
    }

    int transaction_count() const {
        return backend->transaction_count();
    }

    godot::Array upserts() const {
        return backend->upserts();
    }

    int read_count() const {
        return backend->read_count();
    }

    godot::Array reads() const {
        return backend->reads();
    }
};

} // namespace netw_test

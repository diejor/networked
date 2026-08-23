#pragma once

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw_test {

using namespace godot;

class NetwTestPersistenceEngine : public RefCounted {
    GDCLASS(NetwTestPersistenceEngine, RefCounted)

    Object *owner = nullptr;
    Dictionary due;
    Array committed_values;
    double advanced = 0.0;
    int tick_calls = 0;
    int flush_calls = 0;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(
            D_METHOD("owner_node"),
            &NetwTestPersistenceEngine::owner_node
        );
        ClassDB::bind_method(
            D_METHOD("snapshot_tick", "delta"),
            &NetwTestPersistenceEngine::snapshot_tick
        );
        ClassDB::bind_method(
            D_METHOD("flush"),
            &NetwTestPersistenceEngine::flush
        );
        ClassDB::bind_method(
            D_METHOD("commit_snapshot", "values"),
            &NetwTestPersistenceEngine::commit_snapshot
        );
    }

public:
    void set_owner(Object *p_owner) {
        owner = p_owner;
    }

    void set_due(const Dictionary &p_due) {
        due = p_due;
    }

    Object *owner_node() const {
        return owner;
    }

    Dictionary snapshot_tick(double p_delta) {
        tick_calls += 1;
        advanced += p_delta;
        return due;
    }

    void flush() {
        flush_calls += 1;
    }

    void commit_snapshot(const Dictionary &p_values) {
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

    Array committed() const {
        return committed_values;
    }
};

class NetwTestPersistenceDatabase : public RefCounted {
    GDCLASS(NetwTestPersistenceDatabase, RefCounted)

    Array upsert_rows;
    Array read_ids;
    Dictionary stored;
    int transaction_calls = 0;
    int read_calls = 0;
    int outcome = 0;
    int read_refusal = 0;
    bool silent_reads = false;

protected:
    static void _bind_methods() {
        ClassDB::bind_method(
            D_METHOD("transaction_promise", "body"),
            &NetwTestPersistenceDatabase::transaction_promise
        );
        ClassDB::bind_method(
            D_METHOD("queue_upsert", "table", "id", "data"),
            &NetwTestPersistenceDatabase::queue_upsert
        );
        ClassDB::bind_method(
            D_METHOD("find_promise", "table", "id"),
            &NetwTestPersistenceDatabase::find_promise
        );
    }

public:
    void set_outcome(int p_outcome) {
        outcome = p_outcome;
    }

    Ref<netw::NetwPromise> transaction_promise(const Callable &p_body) {
        transaction_calls += 1;
        Array args;
        args.push_back(this);
        p_body.callv(args);
        return netw::NetwPromise::resolved(outcome);
    }

    void queue_upsert(
        const StringName &p_table,
        const StringName &p_id,
        const Dictionary &p_data
    ) {
        Dictionary row;
        row["table"] = p_table;
        row["id"] = p_id;
        row["data"] = p_data;
        upsert_rows.push_back(row);
    }

    int transaction_count() const {
        return transaction_calls;
    }

    Array upserts() const {
        return upsert_rows;
    }

    void set_stored(const Dictionary &p_stored) {
        stored = p_stored;
    }

    void refuse_reads(int p_code) {
        read_refusal = p_code;
    }

    void answer_no_read_promise(bool p_silent) {
        silent_reads = p_silent;
    }

    Ref<netw::NetwPromise> find_promise(
        const StringName &p_table,
        const StringName &p_id
    ) {
        read_calls += 1;
        read_ids.push_back(p_id);
        if (silent_reads) {
            return Ref<netw::NetwPromise>();
        }
        if (read_refusal != 0) {
            return netw::NetwPromise::rejected(read_refusal, String());
        }
        return netw::NetwPromise::resolved(stored);
    }

    int read_count() const {
        return read_calls;
    }

    Array reads() const {
        return read_ids;
    }
};

} // namespace netw_test

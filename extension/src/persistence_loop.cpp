#include "netw/persistence_loop.hpp"

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/api/promise.hpp"

using namespace godot;

namespace netw::persist {

namespace {

Object *owner_of(const Ref<RefCounted> &p_engine) {
    const Variant owner = p_engine->call("owner_node");
    Object *node = owner;
    return node;
}

Object *database_of(const Dictionary &p_due) {
    const Variant held = p_due.get("db", Variant());
    Object *database = held;
    return database;
}

void queue_batch(
    const Variant &p_transaction,
    const PackedInt32Array &p_batch,
    const Array &p_due_rows
) {
    Object *transaction = p_transaction;
    if (transaction == nullptr) {
        return;
    }
    for (int at = 0; at < p_batch.size(); ++at) {
        const Dictionary due = p_due_rows[p_batch[at]];
        transaction->call(
            "queue_upsert",
            due.get("table", Variant()),
            due.get("id", Variant()),
            due.get("values", Variant())
        );
    }
}

void commit_batch(
    const Variant &p_result,
    const PackedInt32Array &p_batch,
    const Array &p_engines,
    const Array &p_due_rows
) {
    const int64_t code = p_result;
    if (code != int64_t(OK)) {
        return;
    }
    for (int at = 0; at < p_batch.size(); ++at) {
        const int row = p_batch[at];
        const Ref<RefCounted> engine = p_engines[row];
        if (engine.is_null()) {
            continue;
        }
        const Dictionary due = p_due_rows[row];
        engine->call("commit_snapshot", due.get("values", Dictionary()));
    }
}

} // namespace

void snapshot_tick(
    PersistenceBook &r_book,
    double p_delta,
    bool p_is_server
) {
    if (!p_is_server) {
        return;
    }
    NETW_ZONE_NC("persistence snapshot tick", colors::TABLE);
    Array engines;
    Array due_rows;
    const TypedArray<RID> entities = r_book.entities();
    for (int at = 0; at < entities.size(); ++at) {
        const RID entity = entities[at];
        const Ref<RefCounted> engine = r_book.engine_of(entity);
        if (engine.is_null() || owner_of(engine) == nullptr) {
            r_book.drop(entity);
            continue;
        }
        const Dictionary due = engine->call("snapshot_tick", p_delta);
        if (due.is_empty()) {
            continue;
        }
        engines.push_back(engine);
        due_rows.push_back(due);
    }
    const TypedArray<PackedInt32Array> batches
        = PersistenceBook::group_by_database(due_rows);
    for (int at = 0; at < batches.size(); ++at) {
        const PackedInt32Array batch = batches[at];
        Object *database = database_of(due_rows[batch[0]]);
        if (database == nullptr) {
            continue;
        }
        const Ref<NetwPromise> written = database->call(
            "transaction_promise",
            callable_mp_static(&queue_batch).bind(batch, due_rows)
        );
        if (written.is_null()) {
            continue;
        }
        written->then(
            callable_mp_static(&commit_batch).bind(batch, engines, due_rows)
        );
    }
    NETW_TRACE(
        sys::TABLE,
        "persistence tick wrote %d due rows in %d batches",
        int(due_rows.size()),
        int(batches.size())
    );
}

void flush_all(PersistenceBook &r_book, bool p_is_server) {
    if (!p_is_server) {
        return;
    }
    NETW_ZONE_NC("persistence flush all", colors::TABLE);
    const TypedArray<RID> entities = r_book.entities();
    for (int at = 0; at < entities.size(); ++at) {
        const Ref<RefCounted> engine = r_book.engine_of(entities[at]);
        if (engine.is_null() || owner_of(engine) == nullptr) {
            continue;
        }
        engine->call("flush");
    }
}

void owner_exiting(
    PersistenceBook &r_book,
    const RID &p_entity,
    bool p_is_server
) {
    const Ref<RefCounted> engine = r_book.engine_of(p_entity);
    if (engine.is_valid() && p_is_server) {
        engine->call("flush");
    }
    r_book.drop(p_entity);
}

} // namespace netw::persist

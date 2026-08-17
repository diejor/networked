#include "netw/persistence_book.hpp"

#include "godot/class_db.hpp"
#include "godot/local_vector.hpp"

using namespace godot;

namespace netw {

bool NetwPersistenceBook::enroll(
    const RID &entity,
    const Ref<RefCounted> &engine
) {
    if (!entity.is_valid() || engine.is_null()) {
        return false;
    }
    const bool fresh = !rows.has(entity);
    rows[entity] = engine;
    if (fresh) {
        order.push_back(entity);
    }
    return fresh;
}

Ref<RefCounted> NetwPersistenceBook::engine_of(const RID &entity) const {
    HashMap<RID, Ref<RefCounted>>::ConstIterator held = rows.find(entity);
    return held == rows.end() ? Ref<RefCounted>() : held->value;
}

bool NetwPersistenceBook::has(const RID &entity) const {
    return rows.has(entity);
}

bool NetwPersistenceBook::drop(const RID &entity) {
    if (!rows.erase(entity)) {
        return false;
    }
    for (uint32_t at = 0; at < order.size(); ++at) {
        if (order[at] == entity) {
            order.remove_at(at);
            break;
        }
    }
    return true;
}

TypedArray<RID> NetwPersistenceBook::entities() const {
    TypedArray<RID> out;
    for (const RID &entity : order) {
        out.push_back(entity);
    }
    return out;
}

int NetwPersistenceBook::size() const {
    return int(rows.size());
}

TypedArray<PackedInt32Array> NetwPersistenceBook::group_by_database(
    const Array &due_rows
) {
    TypedArray<PackedInt32Array> out;
    HashMap<uint64_t, int> slot_of;
    for (int at = 0; at < due_rows.size(); ++at) {
        const Dictionary row = due_rows[at];
        Object *database = Object::cast_to<Object>(row.get("db", Variant()));
        if (database == nullptr) {
            continue;
        }
        const uint64_t id = uint64_t(database->get_instance_id());
        HashMap<uint64_t, int>::Iterator held = slot_of.find(id);
        if (held == slot_of.end()) {
            PackedInt32Array batch;
            batch.push_back(at);
            slot_of[id] = int(out.size());
            out.push_back(batch);
            continue;
        }
        PackedInt32Array batch = out[held->value];
        batch.push_back(at);
        out[held->value] = batch;
    }
    return out;
}

void NetwPersistenceBook::clear() {
    rows.clear();
    order.clear();
}

void NetwPersistenceBook::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("enroll", "entity", "engine"),
        &NetwPersistenceBook::enroll
    );
    ClassDB::bind_method(
        D_METHOD("engine_of", "entity"),
        &NetwPersistenceBook::engine_of
    );
    ClassDB::bind_method(D_METHOD("has", "entity"), &NetwPersistenceBook::has);
    ClassDB::bind_method(
        D_METHOD("drop", "entity"),
        &NetwPersistenceBook::drop
    );
    ClassDB::bind_method(
        D_METHOD("entities"),
        &NetwPersistenceBook::entities
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwPersistenceBook::size);
    ClassDB::bind_static_method(
        "NetwPersistenceBook",
        D_METHOD("group_by_database", "due_rows"),
        &NetwPersistenceBook::group_by_database
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwPersistenceBook::clear);
}

} // namespace netw

#include "netw/persist/book.hpp"

#include "godot/local_vector.hpp"

using namespace godot;

namespace netw::persist {

bool Book::enroll(const RID &entity, const Ref<RefCounted> &engine) {
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

Ref<RefCounted> Book::engine_of(const RID &entity) const {
    HashMap<RID, Ref<RefCounted>>::ConstIterator held = rows.find(entity);
    return held == rows.end() ? Ref<RefCounted>() : held->value;
}

bool Book::has(const RID &entity) const {
    return rows.has(entity);
}

bool Book::drop(const RID &entity) {
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

TypedArray<RID> Book::entities() const {
    TypedArray<RID> out;
    for (const RID &entity : order) {
        out.push_back(entity);
    }
    return out;
}

int Book::size() const {
    return int(rows.size());
}

TypedArray<PackedInt32Array> Book::group_by_database(const Array &due_rows) {
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

void Book::clear() {
    rows.clear();
    order.clear();
}

} // namespace netw::persist

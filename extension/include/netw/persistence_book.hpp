#pragma once

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class PersistenceBook {
    godot::HashMap<godot::RID, godot::Ref<godot::RefCounted>> rows;
    godot::LocalVector<godot::RID> order;

public:
    bool enroll(
        const godot::RID &entity,
        const godot::Ref<godot::RefCounted> &engine
    );

    godot::Ref<godot::RefCounted> engine_of(const godot::RID &entity) const;

    bool has(const godot::RID &entity) const;

    bool drop(const godot::RID &entity);

    godot::TypedArray<godot::RID> entities() const;

    int size() const;

    static godot::TypedArray<godot::PackedInt32Array> group_by_database(
        const godot::Array &due_rows
    );

    void clear();
};

} // namespace netw

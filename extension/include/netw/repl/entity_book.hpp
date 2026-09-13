#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"

namespace netw::repl {

struct EntitySlot {
    int64_t route = 0;
    uint8_t comp = 0;
    uint32_t column = 0;
    uint32_t element = 0;

    bool same_as(const EntitySlot &p_other) const {
        return route == p_other.route && comp == p_other.comp
            && column == p_other.column && element == p_other.element;
    }
};

struct EntityWrite {
    EntitySlot slot;
    int64_t route = 0;
};

class EntityBook {
    struct Entry {
        EntitySlot slot;
        int64_t wanted = 0;
    };

    godot::LocalVector<Entry> waiting;
    godot::HashMap<int64_t, bool> bound;

    void drop(const EntitySlot &p_slot);

public:
    void bind_route(int64_t p_route);
    void tombstone_route(int64_t p_route);
    bool is_bound(int64_t p_route) const;

    int64_t resolve(const EntitySlot &p_slot, int64_t p_wanted);

    void release(int64_t p_route, godot::LocalVector<EntityWrite> &r_completed);

    void forget_slot(int64_t p_route, uint8_t p_comp);
    uint32_t pending_count() const;
    void clear();
};

} // namespace netw::repl

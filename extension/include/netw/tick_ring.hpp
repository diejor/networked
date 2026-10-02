#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/held_bytes.hpp"

namespace netw {

class TickRing {
    godot::LocalVector<godot::Variant> slots;
    godot::LocalVector<int64_t> ticks;
    int64_t head = 0;
    int64_t count = 0;
    int64_t capacity = 0;
    int64_t mask = 0;

public:
    static constexpr int64_t DEFAULT_CAPACITY = 16;

    explicit TickRing(int64_t p_capacity = DEFAULT_CAPACITY);

    void record(int64_t p_tick, const godot::Variant &p_value);
    godot::Variant get_at(int64_t p_tick) const;
    godot::Vector2i bracketing_ticks(int64_t p_tick) const;
    bool has_tick_after(int64_t p_tick) const;
    int64_t oldest_tick() const;
    int64_t newest_tick() const;
    int64_t tick_of_age(int64_t p_age) const;
    const godot::Variant &value_of_age(int64_t p_age) const;
    void clear();
    void release();
    bool holds_storage() const {
        return !slots.is_empty();
    }
    int64_t held_bytes() const {
        return held::vector(slots) + held::vector(ticks);
    }
    int64_t size() const;
    int64_t get_capacity() const;
    bool is_empty() const;
};

} // namespace netw

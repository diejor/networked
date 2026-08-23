#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwRingBuffer : public godot::RefCounted {
    GDCLASS(NetwRingBuffer, godot::RefCounted)

    godot::Array slots;
    godot::PackedInt32Array ticks;
    int64_t head = 0;
    int64_t count = 0;
    int64_t capacity = 0;
    int64_t mask = 0;

    void allocate(int64_t requested);

protected:
    static void _bind_methods();

public:
    NetwRingBuffer();

    static godot::Ref<NetwRingBuffer> create(int64_t requested);

    void record(int64_t tick, const godot::Variant &value);
    godot::Variant get_at(int64_t tick) const;
    godot::Vector2i bracketing_ticks(int64_t tick) const;
    bool has_tick_after(int64_t tick) const;
    int64_t oldest_tick() const;
    int64_t newest_tick() const;
    void clear();
    int64_t size() const;
    bool is_empty() const;
};

} // namespace netw

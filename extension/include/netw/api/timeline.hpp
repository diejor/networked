#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/ring_buffer.hpp"

namespace netw {

class NetwTimeline : public godot::RefCounted {
    GDCLASS(NetwTimeline, godot::RefCounted)

    godot::Ref<NetwRingBuffer> state_ring;
    godot::Ref<NetwRingBuffer> input_ring;
    int64_t floor_tick = -1;

    godot::Dictionary exact_at(
        const godot::Ref<NetwRingBuffer> &p_ring,
        int64_t p_tick
    ) const;

protected:
    static void _bind_methods();

public:
    static constexpr int64_t DEFAULT_LIMIT = 64;

    NetwTimeline();

    static godot::Ref<NetwTimeline> create(int64_t p_limit);

    void record_state(int64_t p_tick, const godot::Dictionary &p_snapshot);
    void record_input(int64_t p_tick, const godot::Dictionary &p_snapshot);

    godot::Dictionary state_at(int64_t p_tick) const;
    godot::Dictionary latest_state_at_or_before(int64_t p_tick) const;
    int64_t latest_state_tick_at_or_before(int64_t p_tick) const;

    godot::Dictionary input_at(int64_t p_tick) const;
    bool has_input_at(int64_t p_tick) const;
    int64_t newest_input_tick() const;
    godot::Array inputs_in_range(int64_t p_from, int64_t p_to) const;

    void trim_before(int64_t p_tick);
    int64_t floor() const;
};

} // namespace netw

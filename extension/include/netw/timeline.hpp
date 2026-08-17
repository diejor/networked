#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/ring_buffer.hpp"

namespace netw {

using namespace godot;

class NetwTimeline : public RefCounted {
    GDCLASS(NetwTimeline, RefCounted)

    Ref<NetwRingBuffer> state_ring;
    Ref<NetwRingBuffer> input_ring;
    int64_t floor_tick = -1;

    Dictionary exact_at(const Ref<NetwRingBuffer> &p_ring, int64_t p_tick)
        const;

protected:
    static void _bind_methods();

public:
    static constexpr int64_t DEFAULT_LIMIT = 64;

    NetwTimeline();

    static Ref<NetwTimeline> create(int64_t p_limit);

    void record_state(int64_t p_tick, const Dictionary &p_snapshot);
    void record_input(int64_t p_tick, const Dictionary &p_snapshot);

    Dictionary state_at(int64_t p_tick) const;
    Dictionary latest_state_at_or_before(int64_t p_tick) const;
    int64_t latest_state_tick_at_or_before(int64_t p_tick) const;

    Dictionary input_at(int64_t p_tick) const;
    bool has_input_at(int64_t p_tick) const;
    int64_t newest_input_tick() const;
    Array inputs_in_range(int64_t p_from, int64_t p_to) const;

    void trim_before(int64_t p_tick);
    int64_t floor() const;
};

} // namespace netw

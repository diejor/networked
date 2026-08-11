#pragma once

/* Per-entity, tick-keyed record of whole-entity state and input snapshots.
 *
 * State carries forward so a missing tick reads as unchanged, while input is
 * exact so a missing tick reads as no action. Each side has exactly one
 * writer, the server for state and the owning peer for input, so there is
 * never a merge and no authority flag is needed.
 *
 * One timeline per entity serves BOTH planes: the prediction engine reads its
 * own state and input off it while the server's history recorder writes state
 * into it. A second store for either plane would be a second history that
 * agrees only by accident.
 */

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
    // Entries recorded strictly before this tick read as absent, so a trim
    // needs no physical eviction: the ring self-bounds by capacity.
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

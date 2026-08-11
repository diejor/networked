#include "netw/timeline.hpp"

#include <algorithm>

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

NetwTimeline::NetwTimeline() {
    state_ring = NetwRingBuffer::create(DEFAULT_LIMIT);
    input_ring = NetwRingBuffer::create(DEFAULT_LIMIT);
}

Ref<NetwTimeline> NetwTimeline::create(int64_t p_limit) {
    Ref<NetwTimeline> out;
    out.instantiate();
    const int64_t limit = std::max<int64_t>(1, p_limit);
    out->state_ring = NetwRingBuffer::create(limit);
    out->input_ring = NetwRingBuffer::create(limit);
    return out;
}

Dictionary NetwTimeline::exact_at(
    const Ref<NetwRingBuffer> &p_ring,
    int64_t p_tick
) const {
    if (p_tick < floor_tick) {
        return Dictionary();
    }
    const Variant value = p_ring->get_at(p_tick);
    return value.get_type() == Variant::DICTIONARY ? Dictionary(value)
                                                   : Dictionary();
}

void NetwTimeline::record_state(int64_t p_tick, const Dictionary &p_snapshot) {
    NETW_ZONE_NC("NetwTimeline record state", colors::INTERP);
    state_ring->record(p_tick, p_snapshot);
}

void NetwTimeline::record_input(int64_t p_tick, const Dictionary &p_snapshot) {
    NETW_ZONE_NC("NetwTimeline record input", colors::INTERP);
    input_ring->record(p_tick, p_snapshot);
}

Dictionary NetwTimeline::state_at(int64_t p_tick) const {
    return exact_at(state_ring, p_tick);
}

Dictionary NetwTimeline::latest_state_at_or_before(int64_t p_tick) const {
    const int64_t at = latest_state_tick_at_or_before(p_tick);
    return at < 0 ? Dictionary() : Dictionary(state_ring->get_at(at));
}

int64_t NetwTimeline::latest_state_tick_at_or_before(int64_t p_tick) const {
    if (p_tick < floor_tick) {
        return -1;
    }
    const int64_t previous = state_ring->bracketing_ticks(p_tick).x;
    if (previous < floor_tick) {
        return -1;
    }
    return state_ring->get_at(previous).get_type() == Variant::DICTIONARY
        ? previous
        : -1;
}

Dictionary NetwTimeline::input_at(int64_t p_tick) const {
    return exact_at(input_ring, p_tick);
}

bool NetwTimeline::has_input_at(int64_t p_tick) const {
    return !input_at(p_tick).is_empty();
}

int64_t NetwTimeline::newest_input_tick() const {
    return input_ring->newest_tick();
}

Array NetwTimeline::inputs_in_range(int64_t p_from, int64_t p_to) const {
    NETW_ZONE_NC("NetwTimeline input window", colors::INTERP);
    Array out;
    for (int64_t tick = std::max(p_from, floor_tick); tick <= p_to; ++tick) {
        const Variant value = input_ring->get_at(tick);
        if (value.get_type() != Variant::DICTIONARY) {
            continue;
        }
        Dictionary entry;
        entry[StringName("tick")] = tick;
        entry[StringName("input")] = value;
        out.push_back(entry);
    }
    return out;
}

void NetwTimeline::trim_before(int64_t p_tick) {
    floor_tick = std::max(floor_tick, p_tick);
}

int64_t NetwTimeline::floor() const {
    return floor_tick;
}

void NetwTimeline::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwTimeline",
        D_METHOD("create", "limit"),
        &NetwTimeline::create,
        DEFVAL(DEFAULT_LIMIT)
    );
    ClassDB::bind_method(
        D_METHOD("record_state", "tick", "snapshot"),
        &NetwTimeline::record_state
    );
    ClassDB::bind_method(
        D_METHOD("record_input", "tick", "snapshot"),
        &NetwTimeline::record_input
    );
    ClassDB::bind_method(
        D_METHOD("state_at", "tick"),
        &NetwTimeline::state_at
    );
    ClassDB::bind_method(
        D_METHOD("latest_state_at_or_before", "tick"),
        &NetwTimeline::latest_state_at_or_before
    );
    ClassDB::bind_method(
        D_METHOD("latest_state_tick_at_or_before", "tick"),
        &NetwTimeline::latest_state_tick_at_or_before
    );
    ClassDB::bind_method(
        D_METHOD("input_at", "tick"),
        &NetwTimeline::input_at
    );
    ClassDB::bind_method(
        D_METHOD("has_input_at", "tick"),
        &NetwTimeline::has_input_at
    );
    ClassDB::bind_method(
        D_METHOD("newest_input_tick"),
        &NetwTimeline::newest_input_tick
    );
    ClassDB::bind_method(
        D_METHOD("inputs_in_range", "from", "to"),
        &NetwTimeline::inputs_in_range
    );
    ClassDB::bind_method(
        D_METHOD("trim_before", "tick"),
        &NetwTimeline::trim_before
    );
    ClassDB::bind_method(D_METHOD("floor"), &NetwTimeline::floor);
    ClassDB::bind_integer_constant(
        "NetwTimeline",
        StringName(),
        "DEFAULT_LIMIT",
        DEFAULT_LIMIT
    );
}

} // namespace netw

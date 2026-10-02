#include "netw/tick_ring.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

TickRing::TickRing(int64_t p_capacity) {
    capacity = 1;
    while (capacity < p_capacity) {
        capacity <<= 1;
    }
    mask = capacity - 1;
}

void TickRing::record(int64_t p_tick, const Variant &p_value) {
    NETW_ZONE_LEAF(
        "Tick ring record",
        colors::INTERP,
        profile::SUBSYSTEM_INTERPOLATION
    );
    if (slots.is_empty()) {
        slots.resize(uint32_t(capacity));
        ticks.resize(uint32_t(capacity));
        for (uint32_t at = 0; at < ticks.size(); ++at) {
            ticks[at] = -1;
        }
    }
    if (count > 0 && p_tick <= newest_tick()) {
        for (int64_t at = count - 1; at >= 0; --at) {
            const int64_t held = (head + at) & mask;
            if (ticks[uint32_t(held)] == p_tick) {
                slots[uint32_t(held)] = p_value;
                return;
            }
        }
    }
    int64_t index;
    if (count < capacity) {
        index = (head + count) & mask;
        count += 1;
    } else {
        index = head;
        head = (head + 1) & mask;
    }
    slots[uint32_t(index)] = p_value;
    ticks[uint32_t(index)] = p_tick;
}

Variant TickRing::get_at(int64_t p_tick) const {
    for (int64_t i = 0; i < count; ++i) {
        const int64_t index = (head + i) & mask;
        if (ticks[uint32_t(index)] == p_tick) {
            return slots[uint32_t(index)];
        }
    }
    return Variant();
}

Vector2i TickRing::bracketing_ticks(int64_t p_tick) const {
    NETW_ZONE_LEAF(
        "Tick ring bracket",
        colors::INTERP,
        profile::SUBSYSTEM_INTERPOLATION
    );
    int64_t previous = -1;
    int64_t next = -1;
    for (int64_t i = 0; i < count; ++i) {
        const int64_t stored = ticks[uint32_t((head + i) & mask)];
        if (stored <= p_tick) {
            if (stored > previous) {
                previous = stored;
            }
        } else if (next == -1 || stored < next) {
            next = stored;
            break;
        }
    }
    return Vector2i(int32_t(previous), int32_t(next));
}

bool TickRing::has_tick_after(int64_t p_tick) const {
    return newest_tick() > p_tick;
}

int64_t TickRing::oldest_tick() const {
    return count > 0 ? ticks[uint32_t(head)] : -1;
}

int64_t TickRing::newest_tick() const {
    return count > 0 ? ticks[uint32_t((head + count - 1) & mask)] : -1;
}

int64_t TickRing::tick_of_age(int64_t p_age) const {
    if (p_age < 0 || p_age >= count) {
        return -1;
    }
    return ticks[uint32_t((head + count - 1 - p_age) & mask)];
}

const Variant &TickRing::value_of_age(int64_t p_age) const {
    return slots[uint32_t((head + count - 1 - p_age) & mask)];
}

void TickRing::clear() {
    count = 0;
    head = 0;
}

void TickRing::release() {
    clear();
    slots.reset();
    ticks.reset();
}

int64_t TickRing::size() const {
    return count;
}

int64_t TickRing::get_capacity() const {
    return capacity;
}

bool TickRing::is_empty() const {
    return count == 0;
}

} // namespace netw

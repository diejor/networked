#include "netw/ring_buffer.hpp"

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr int64_t DEFAULT_CAPACITY = 16;

} // namespace

void NetwRingBuffer::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwRingBuffer",
        D_METHOD("create", "capacity"),
        &NetwRingBuffer::create,
        DEFVAL(DEFAULT_CAPACITY)
    );
    ClassDB::bind_method(
        D_METHOD("record", "tick", "value"),
        &NetwRingBuffer::record
    );
    ClassDB::bind_method(D_METHOD("get_at", "tick"), &NetwRingBuffer::get_at);
    ClassDB::bind_method(
        D_METHOD("bracketing_ticks", "tick"),
        &NetwRingBuffer::bracketing_ticks
    );
    ClassDB::bind_method(
        D_METHOD("has_tick_after", "tick"),
        &NetwRingBuffer::has_tick_after
    );
    ClassDB::bind_method(D_METHOD("oldest_tick"), &NetwRingBuffer::oldest_tick);
    ClassDB::bind_method(D_METHOD("newest_tick"), &NetwRingBuffer::newest_tick);
    ClassDB::bind_method(D_METHOD("clear"), &NetwRingBuffer::clear);
    ClassDB::bind_method(D_METHOD("size"), &NetwRingBuffer::size);
    ClassDB::bind_method(D_METHOD("is_empty"), &NetwRingBuffer::is_empty);
}

NetwRingBuffer::NetwRingBuffer() {
    allocate(DEFAULT_CAPACITY);
}

// A power-of-two capacity is what keeps index wrapping a mask rather than a
// modulo, which is the whole reason this type exists.
void NetwRingBuffer::allocate(int64_t requested) {
    capacity = 1;
    while (capacity < requested) {
        capacity <<= 1;
    }
    mask = capacity - 1;
    head = 0;
    count = 0;
    slots.clear();
    slots.resize(capacity);
    ticks.resize(capacity);
    ticks.fill(-1);
}

Ref<NetwRingBuffer> NetwRingBuffer::create(int64_t requested) {
    Ref<NetwRingBuffer> buffer;
    buffer.instantiate();
    buffer->allocate(requested);
    return buffer;
}

void NetwRingBuffer::record(int64_t tick, const Variant &value) {
    NETW_ZONE_NC("NetwRingBuffer record", colors::INTERP);
    int64_t index;
    if (count < capacity) {
        index = (head + count) & mask;
        count += 1;
    } else {
        index = head;
        head = (head + 1) & mask;
    }
    slots[index] = value;
    ticks.set(index, static_cast<int32_t>(tick));
}

Variant NetwRingBuffer::get_at(int64_t tick) const {
    for (int64_t i = 0; i < count; ++i) {
        const int64_t index = (head + i) & mask;
        if (ticks[index] == tick) {
            return slots[index];
        }
    }
    return Variant();
}

Vector2i NetwRingBuffer::bracketing_ticks(int64_t tick) const {
    NETW_ZONE_NC("NetwRingBuffer bracketing_ticks", colors::INTERP);
    int32_t previous = -1;
    int32_t next = -1;
    for (int64_t i = 0; i < count; ++i) {
        const int64_t index = (head + i) & mask;
        const int32_t stored = ticks[index];
        if (stored <= tick) {
            if (stored > previous) {
                previous = stored;
            }
        } else if (next == -1 || stored < next) {
            next = stored;
            break;
        }
    }
    return Vector2i(previous, next);
}

bool NetwRingBuffer::has_tick_after(int64_t tick) const {
    return newest_tick() > tick;
}

int64_t NetwRingBuffer::oldest_tick() const {
    return count > 0 ? ticks[head] : -1;
}

int64_t NetwRingBuffer::newest_tick() const {
    return count > 0 ? ticks[(head + count - 1) & mask] : -1;
}

void NetwRingBuffer::clear() {
    count = 0;
    head = 0;
}

int64_t NetwRingBuffer::size() const {
    return count;
}

bool NetwRingBuffer::is_empty() const {
    return count == 0;
}

} // namespace netw

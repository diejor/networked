// Unit tests for NetwRingBuffer.
//
// Contract examples cover record/retrieve, bracketing search, and eviction
// under a fixed capacity. Oracle tests cross-check the same invariants against
// a brute-force model.

#include "support/netw_test.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "netw/ring_buffer.hpp"

namespace TestNetwRingBuffer {

using namespace godot;
using netw::NetwRingBuffer;

// The oracle the fuzzing cases cross-check against: a plain list that evicts
// from the front, which is what the ring is supposed to be equivalent to.
struct Oracle {
    std::vector<std::pair<int64_t, String>> rows;
    int64_t capacity;

    explicit Oracle(int64_t p_capacity) : capacity(p_capacity) {
    }

    void record(int64_t tick, const String &value) {
        rows.push_back({tick, value});
        if (static_cast<int64_t>(rows.size()) > capacity) {
            rows.erase(rows.begin());
        }
    }
};

// A deterministic sequence stands in for the GDScript fuzzer, so a failure
// names the same inputs every run instead of a seed nobody recorded.
int64_t next_value(int64_t &state, int64_t low, int64_t high) {
    state = (state * 6364136223846793005LL + 1442695040888963407LL);
    const int64_t span = high - low + 1;
    return low + ((state >> 33) % span + span) % span;
}

TEST_CASE(
    "[Networked][Ring][Hosted] ring buffer reports empty state and records a "
    "value"
) {
    Ref<NetwRingBuffer> empty = NetwRingBuffer::create(4);
    CHECK(empty->is_empty());
    CHECK(empty->size() == 0);
    CHECK(empty->oldest_tick() == -1);
    CHECK(empty->newest_tick() == -1);
    CHECK(empty->get_at(0).get_type() == Variant::NIL);
    CHECK(empty->get_at(123).get_type() == Variant::NIL);
    CHECK_FALSE(empty->has_tick_after(0));

    Ref<NetwRingBuffer> buffer = NetwRingBuffer::create(4);
    buffer->record(10, String("a"));
    CHECK_FALSE(buffer->is_empty());
    CHECK(buffer->size() == 1);
    CHECK(buffer->get_at(10) == Variant(String("a")));
    CHECK(buffer->get_at(11).get_type() == Variant::NIL);
    CHECK(buffer->oldest_tick() == 10);
    CHECK(buffer->newest_tick() == 10);
    CHECK(buffer->has_tick_after(5));
    CHECK_FALSE(buffer->has_tick_after(10));
    CHECK_FALSE(buffer->has_tick_after(15));
}

TEST_CASE("[Networked][Ring][Hosted] ring buffer stores any variant type") {
    struct Row {
        int64_t tick;
        Variant value;
    };
    Dictionary mapping;
    mapping[StringName("k")] = StringName("v");
    const Row rows[] = {
        {0, String("string-value")},
        {10, 42},
        {20, Vector2(3.0, 4.0)},
        {1000, mapping},
    };
    for (const Row &row : rows) {
        Ref<NetwRingBuffer> buffer = NetwRingBuffer::create(4);
        buffer->record(row.tick, row.value);
        CHECK(buffer->size() == 1);
        CHECK(buffer->get_at(row.tick) == row.value);
        CHECK(buffer->get_at(row.tick + 1).get_type() == Variant::NIL);
        CHECK(buffer->oldest_tick() == row.tick);
        CHECK(buffer->newest_tick() == row.tick);
    }
}

TEST_CASE(
    "[Networked][Ring][Hosted] ring buffer brackets a tick between its "
    "neighbours"
) {
    struct Row {
        std::vector<int64_t> recorded;
        int64_t query;
        Vector2i expected;
    };
    const Row rows[] = {
        {{}, 10, Vector2i(-1, -1)},
        {{10, 20}, 10, Vector2i(10, 20)},
        {{10, 20}, 15, Vector2i(10, 20)},
        {{10}, 5, Vector2i(-1, 10)},
        {{10}, 15, Vector2i(10, -1)},
        {{5, 10, 20, 30}, 17, Vector2i(10, 20)},
    };
    for (const Row &row : rows) {
        Ref<NetwRingBuffer> buffer = NetwRingBuffer::create(8);
        for (int64_t tick : row.recorded) {
            buffer->record(tick, tick);
        }
        CHECK(buffer->bracketing_ticks(row.query) == row.expected);
    }
}

TEST_CASE(
    "[Networked][Ring][Hosted] ring buffer evicts the oldest entry past "
    "capacity"
) {
    // A requested 3 rounds up to 4, so the fourth entry still fits.
    Ref<NetwRingBuffer> buffer = NetwRingBuffer::create(3);
    for (int64_t tick = 1; tick < 5; ++tick) {
        buffer->record(tick, String("v") + String::num_int64(tick));
    }
    CHECK(buffer->size() == 4);
    CHECK(buffer->get_at(1) == Variant(String("v1")));

    buffer->record(5, String("v5"));
    CHECK(buffer->get_at(1).get_type() == Variant::NIL);
    CHECK(buffer->size() == 4);

    const int64_t capacity = 4;
    const int64_t inserts = 12;
    const int64_t base = 100;
    buffer = NetwRingBuffer::create(capacity);
    for (int64_t i = 0; i < inserts; ++i) {
        buffer->record(base + i, base + i);
    }
    for (int64_t tick = base; tick < base + inserts - capacity; ++tick) {
        CHECK(buffer->get_at(tick).get_type() == Variant::NIL);
    }
}

TEST_CASE(
    "[Networked][Ring][Hosted] ring buffer view matches an evicting-list oracle"
) {
    int64_t state = 1;
    for (int run = 0; run < 20; ++run) {
        const int64_t capacity = 4;
        const int64_t inserts = 12;
        Ref<NetwRingBuffer> buffer = NetwRingBuffer::create(capacity);
        Oracle oracle(capacity);

        const int64_t base = next_value(state, 1, 1000000);
        for (int64_t i = 0; i < inserts; ++i) {
            const int64_t tick = base + i;
            const String value = String("value_") + String::num_int64(tick);
            buffer->record(tick, value);
            oracle.record(tick, value);
        }

        CHECK(buffer->size() == static_cast<int64_t>(oracle.rows.size()));
        CHECK(buffer->oldest_tick() == oracle.rows.front().first);
        CHECK(buffer->newest_tick() == oracle.rows.back().first);
        for (const auto &row : oracle.rows) {
            CHECK(buffer->get_at(row.first) == Variant(row.second));
        }
    }
}

TEST_CASE(
    "[Networked][Ring][Hosted] ring buffer bracketing matches a linear scan"
) {
    int64_t state = 7;
    for (int run = 0; run < 20; ++run) {
        const int64_t capacity = 8;
        Ref<NetwRingBuffer> buffer = NetwRingBuffer::create(capacity);
        std::vector<int64_t> recorded;

        int64_t tick = 0;
        for (int64_t i = 0; i < capacity; ++i) {
            tick += 1 + next_value(state, 0, 50) % 5;
            buffer->record(tick, tick);
            recorded.push_back(tick);
        }

        const int64_t query = next_value(state, 0, 50);
        Vector2i expected(-1, -1);
        for (int64_t stored : recorded) {
            if (stored <= query && stored > expected.x) {
                expected.x = static_cast<int32_t>(stored);
            }
            if (stored > query && (expected.y == -1 || stored < expected.y)) {
                expected.y = static_cast<int32_t>(stored);
            }
        }
        CHECK(buffer->bracketing_ticks(query) == expected);
    }
}

} // namespace TestNetwRingBuffer

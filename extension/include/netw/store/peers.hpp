#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw::store {

enum class Stream : uint8_t { NONE, OPENING, READY };

struct Bits {
    godot::LocalVector<uint64_t> words;
    uint32_t count = 0;

    bool get(uint32_t p_at) const {
        return ((words[p_at >> 6] >> (p_at & 63)) & 1) != 0;
    }

    void set(uint32_t p_at, bool p_on) {
        const uint64_t bit = uint64_t(1) << (p_at & 63);
        uint64_t &word = words[p_at >> 6];
        word = p_on ? (word | bit) : (word & ~bit);
    }

    void resize(uint32_t p_count);

    void clear();

    void fill();

    uint32_t population() const;

    static uint32_t lowest(uint64_t p_word) {
        static const uint8_t order[64] = {
            0,  1,  48, 2,  57, 49, 28, 3,  61, 58, 50, 42, 38, 29, 17, 4,
            62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12, 5,
            63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11,
            46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9,  13, 8,  7,  6,
        };
        const uint64_t alone = p_word & (~p_word + 1);
        return order[(alone * 0x03f79d71b4cb0a89ULL) >> 58];
    }
};

struct PeerRows {
    Bits admitted;
    Bits sendable;
    Bits owed;
    Bits queued;
    Bits due;
    godot::LocalVector<uint8_t> stream;
    godot::LocalVector<uint64_t> request;
    godot::LocalVector<uint64_t> token;
    godot::LocalVector<wire::StreamTenure> opened;
    godot::LocalVector<uint64_t> next_rev;
    godot::LocalVector<uint64_t> acked_rev;
    godot::LocalVector<uint64_t> exposed_rev;
    godot::LocalVector<uint64_t> baseline;
    godot::LocalVector<godot::Variant> baseline_carried;
    godot::LocalVector<float> acc;
    godot::LocalVector<int64_t> last_sent;
    godot::LocalVector<int64_t> refresh_at;
    godot::LocalVector<int64_t> repair_at;
};

struct Flight {
    uint32_t slot = 0;
    uint32_t gen = 0;
    uint32_t table = 0;
    uint8_t ordinal = 0;
    uint8_t family = 0;
    bool pinned = false;
    uint64_t revision = 0;
    wire::CodeRow image;
};

struct Refresh {
    int64_t at = 0;
    uint64_t lane = 0;
};

struct PeerSide {
    int id = 0;
    bool joined = false;
    bool reachable = false;
    Bits visible;
    Bits held;
    godot::HashMap<uint64_t, godot::LocalVector<Flight>> flights;
    godot::HashMap<uint64_t, uint64_t> requests;
    godot::HashMap<uint64_t, uint64_t> tokens;
    godot::LocalVector<Refresh> refreshes;
    godot::LocalVector<uint64_t> due;
};

struct ReceiptCounts {
    int64_t promoted = 0;
    int64_t ignored = 0;
    int64_t unexposed = 0;
    int64_t evicted = 0;
    int64_t stale = 0;
    int64_t unknown = 0;
};

} // namespace netw::store

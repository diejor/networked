#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"

namespace netw {

class SlotIds {
    godot::LocalVector<uint32_t> gens;
    godot::LocalVector<uint8_t> live;
    godot::LocalVector<uint32_t> vacant;
    uint32_t held = 0;

    static constexpr uint32_t GEN_MASK = 0x7fffffffu;

    static int64_t compose(uint32_t p_index, uint32_t p_gen) {
        return int64_t((uint64_t(p_gen & GEN_MASK) << 32) | uint64_t(p_index));
    }

public:
    static uint32_t index_of(int64_t p_id) {
        return uint32_t(uint64_t(p_id) & 0xffffffffu);
    }

    int64_t acquire() {
        if (gens.is_empty()) {
            gens.push_back(0);
            live.push_back(0);
        }
        uint32_t index = 0;
        if (!vacant.is_empty()) {
            index = vacant[vacant.size() - 1];
            vacant.resize(vacant.size() - 1);
        } else {
            index = gens.size();
            gens.push_back(0);
            live.push_back(0);
        }
        live[index] = 1;
        held += 1;
        return compose(index, gens[index]);
    }

    int64_t seat(int64_t p_id) const {
        if (p_id <= 0) {
            return -1;
        }
        const uint32_t index = index_of(p_id);
        if (index == 0 || index >= gens.size() || live[index] == 0
            || compose(index, gens[index]) != p_id) {
            return -1;
        }
        return int64_t(index);
    }

    bool release(int64_t p_id) {
        const int64_t index = seat(p_id);
        if (index < 0) {
            return false;
        }
        live[uint32_t(index)] = 0;
        gens[uint32_t(index)] = (gens[uint32_t(index)] + 1) & GEN_MASK;
        vacant.push_back(uint32_t(index));
        held -= 1;
        return true;
    }

    int64_t id_at(uint32_t p_index) const {
        if (p_index == 0 || p_index >= gens.size() || live[p_index] == 0) {
            return -1;
        }
        return compose(p_index, gens[p_index]);
    }

    uint32_t capacity() const {
        return gens.size();
    }

    uint32_t count() const {
        return held;
    }

    void clear() {
        gens.clear();
        live.clear();
        vacant.clear();
        held = 0;
    }
};

} // namespace netw

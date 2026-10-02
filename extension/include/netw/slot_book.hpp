#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/held_bytes.hpp"

namespace netw {

template <typename T>
class SlotBook {
    struct Slot {
        T *held = nullptr;
        uint32_t gen = 0;
    };

    godot::LocalVector<Slot> slots;
    godot::LocalVector<uint32_t> vacant;

public:
    static constexpr uint64_t NONE = 0;

    SlotBook() = default;
    SlotBook(const SlotBook &) = delete;
    SlotBook &operator=(const SlotBook &) = delete;

    ~SlotBook() {
        clear();
    }

    uint64_t create() {
        uint32_t at = 0;
        if (!vacant.is_empty()) {
            at = vacant[vacant.size() - 1];
            vacant.remove_at(vacant.size() - 1);
        } else {
            at = slots.size();
            slots.push_back(Slot());
        }
        Slot &slot = slots[at];
        slot.gen = slot.gen == UINT32_MAX ? 1 : slot.gen + 1;
        slot.held = new T();
        return uint64_t(at) | (uint64_t(slot.gen) << 32);
    }

    T *get(uint64_t p_id) const {
        const uint32_t at = uint32_t(p_id & 0xffffffffu);
        const uint32_t gen = uint32_t(p_id >> 32);
        if (gen == 0 || at >= slots.size() || slots[at].gen != gen) {
            return nullptr;
        }
        return slots[at].held;
    }

    bool free(uint64_t p_id) {
        T *held = get(p_id);
        if (held == nullptr) {
            return false;
        }
        const uint32_t at = uint32_t(p_id & 0xffffffffu);
        slots[at].held = nullptr;
        vacant.push_back(at);
        delete held;
        return true;
    }

    uint32_t count() const {
        return slots.size() - vacant.size();
    }

    template <typename F>
    int64_t held_bytes(F p_each) const {
        int64_t bytes = netw::held::vector(slots) + netw::held::vector(vacant);
        for (const Slot &slot : slots) {
            if (slot.held != nullptr) {
                bytes += int64_t(sizeof(T)) + p_each(*slot.held);
            }
        }
        return bytes;
    }

    void clear() {
        for (uint32_t at = 0; at < slots.size(); ++at) {
            T *held = slots[at].held;
            if (held == nullptr) {
                continue;
            }
            slots[at].held = nullptr;
            vacant.push_back(at);
            delete held;
        }
    }
};

} // namespace netw

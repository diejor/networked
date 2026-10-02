#pragma once

#include <cstdint>

namespace netw::store {

struct EntityRef {
    static constexpr uint32_t NONE = 0xffffffffu;

    uint32_t slot = NONE;
    uint32_t gen = 0;

    bool operator==(const EntityRef &p_other) const {
        return slot == p_other.slot && gen == p_other.gen;
    }

    bool operator!=(const EntityRef &p_other) const {
        return !(*this == p_other);
    }

    bool bound() const {
        return slot != NONE;
    }
};

} // namespace netw::store

#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

class BaselineRing {
    struct Slot {
        uint16_t seq = 0;
        bool held = false;
        wire::CodeRow row;
    };

    godot::LocalVector<Slot> slots;
    uint16_t newest = 0;
    bool seated = false;

public:
    static constexpr uint32_t DEPTH = 64;

    void record(uint16_t p_seq, const wire::CodeRow &p_row);

    const wire::CodeRow *resolve(uint16_t p_seq, uint8_t p_base_low) const;

    uint32_t count() const;

    void clear();
};

} // namespace netw::repl

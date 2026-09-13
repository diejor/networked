#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw {

struct StagedWrites {
    int64_t ordinal = 0;
    int64_t tick = -1;
    int64_t ack = -1;

    godot::Array keys;
    godot::Array values;

    godot::Dictionary row;

    bool whole = true;

    godot::Array samples;

    bool is_valid() const;

    godot::Dictionary header() const;
};

} // namespace netw

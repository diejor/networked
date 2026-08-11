#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/registry.hpp"

namespace netw::wire {

struct FitCandidate {
    uint8_t channel_id = 0;
    int64_t payload_bits = 0;
    float priority = 1.0f;
    float accumulated_priority = 1.0f;
    int64_t send_id = 0;
};

struct FitResult {
    godot::LocalVector<FitCandidate> packed;
    godot::LocalVector<FitCandidate> deferred;
    int64_t total_bits = 0;
};

// Priority packing of frame candidates against MTU budget constants.
class WireFitter {
public:
    static FitResult fit(
        const WireRegistry &registry,
        godot::LocalVector<FitCandidate> &candidates,
        int64_t max_bits
    );
};

} // namespace netw::wire

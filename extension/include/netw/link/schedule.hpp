#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/random.hpp"
#include "godot/variant.hpp"

namespace netw {
class NetwLinkConditions;
}

namespace netw::link {

struct Draws {
    godot::HashMap<godot::String, godot::Ref<godot::RandomNumberGenerator>>
        rng_by_stream;
    godot::HashMap<godot::String, double> reliable_due_by_channel;
    double stall_until_ms = 0.0;
};

struct Send {
    int sender = 0;
    int channel = 0;
    bool reliable = false;
};

struct Arrival {
    bool dropped = false;
    double due_ms = 0.0;
    double duplicate_due_ms = -1.0;
};

Arrival schedule(
    Draws &p_draws,
    const NetwLinkConditions &p_conditions,
    Send p_send,
    double p_now_ms,
    double p_frame_ms
);
bool accept_ordered(
    godot::HashMap<godot::String, uint64_t> &p_delivered,
    int p_sender,
    int p_channel,
    uint64_t p_sequence
);
void forget_ordered_sender(
    godot::HashMap<godot::String, uint64_t> &p_delivered,
    int p_sender
);
void forget_sender(Draws &p_draws, int p_sender);
void prune_reliable_due(Draws &p_draws, double p_now_ms);

} // namespace netw::link

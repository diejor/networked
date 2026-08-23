#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

class BaselineBook {
    struct Staged {
        uint16_t seq = 0;
        CodeRow row;
    };

    struct Peer {
        CodeRow confirmed;
        bool has_confirmed = false;
        uint64_t sticky = 0;
        godot::LocalVector<Staged> in_flight;
    };

    godot::HashMap<int, Peer> peers;

public:
    static constexpr uint32_t MAX_IN_FLIGHT = 64;

    uint64_t mask_to_send(int peer, const WirePlan &plan, const CodeRow &row);

    void stage(int peer, uint16_t seq, const CodeRow &row);

    void acknowledge(int peer, uint16_t acked_seq);

    void retain(const godot::LocalVector<int> &recipients);

    void forget(int peer);
    void clear();

    bool has_baseline(int peer) const;
    uint32_t in_flight_count(int peer) const;
};

} // namespace netw::wire

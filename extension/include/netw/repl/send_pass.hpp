#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/ack_book.hpp"
#include "netw/wire/fitter.hpp"
#include "netw/wire/registry.hpp"

namespace netw::repl {

struct PassResult {
    godot::LocalVector<wire::FitCandidate> sent;
    godot::LocalVector<wire::FitCandidate> deferred;
    int64_t sent_bits = 0;
    bool untrackable = false;
};

class SendPass {
    wire::AckBook acks;

public:
    PassResult run(
        const wire::WireRegistry &p_registry,
        godot::LocalVector<wire::FitCandidate> &p_offers,
        int64_t p_max_bits,
        uint16_t p_seq,
        int64_t p_send_id
    );

    void acknowledge(
        uint16_t p_ack_seq,
        godot::LocalVector<wire::AckEntry> &r_delivered,
        godot::LocalVector<wire::AckEntry> &r_lost
    );

    int outstanding() const {
        return acks.active_count();
    }

    void clear() {
        acks.clear();
    }
};

} // namespace netw::repl

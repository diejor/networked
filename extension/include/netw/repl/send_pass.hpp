#pragma once

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "netw/wire/ack_book.hpp"
#include "netw/wire/fitter.hpp"
#include "netw/wire/registry.hpp"

namespace netw::repl {

struct PassResult {
    godot::LocalVector<wire::FitCandidate> sent;
    godot::LocalVector<wire::FitCandidate> deferred;
    int64_t sent_bits = 0;
};

class SendPass {
    godot::HashMap<int, wire::AckBook> books;

public:
    PassResult run(
        const wire::WireRegistry &p_registry,
        int p_peer,
        godot::LocalVector<wire::FitCandidate> &p_offers,
        int64_t p_max_bits
    );

    bool record_datagram(
        int p_peer,
        uint16_t p_seq,
        uint16_t p_frames,
        int64_t p_bits
    );

    void acknowledge(
        int p_peer,
        uint16_t p_ack_seq,
        uint32_t p_history,
        godot::LocalVector<wire::AckEntry> &r_delivered,
        godot::LocalVector<wire::AckEntry> &r_lost
    );

    void forget(int p_peer);

    int outstanding() const;
    int outstanding(int p_peer) const;

    void clear() {
        books.clear();
    }
};

} // namespace netw::repl

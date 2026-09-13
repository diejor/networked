#pragma once

#include "netw_test.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/wire/registry.hpp"

namespace netw_test {

inline netw::repl::SessionResult drive_send(
    netw::repl::SessionSend &p_session,
    const netw::wire::WireRegistry &p_registry,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers,
    int64_t p_max_bits,
    uint16_t p_seq,
    int64_t p_base_tick = 0
) {
    netw::repl::SessionResult out
        = p_session.run(p_registry, p_offers, p_max_bits, p_base_tick);
    godot::LocalVector<int> peers;
    for (uint32_t at = 0; at < out.sends.size(); ++at) {
        p_session.defer(out.sends[at]);
        const int peer = out.sends[at].peer;
        bool known = false;
        for (uint32_t slot = 0; slot < peers.size(); ++slot) {
            known = known || peers[slot] == peer;
        }
        if (!known) {
            peers.push_back(peer);
        }
    }
    for (uint32_t slot = 0; slot < peers.size(); ++slot) {
        p_session.commit(peers[slot], p_seq);
    }
    return out;
}

} // namespace netw_test

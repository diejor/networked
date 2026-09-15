#pragma once

#include "netw_test.h"

#include "stream_seat.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/wire/registry.hpp"

namespace netw_test {

inline netw::CarrierBatch batch_of(
    netw::repl::SessionSend &p_session,
    const netw::repl::SessionResult &p_result,
    int p_peer
) {
    netw::CarrierBatch out;
    godot::PackedByteArray frame;
    frame.resize(1);
    for (uint32_t at = 0; at < p_result.sends.size(); ++at) {
        if (p_result.sends[at].peer != p_peer) {
            continue;
        }
        netw::CarrierRow row;
        if (!p_session.describe(p_result.sends[at], row)) {
            continue;
        }
        out.take_frame(frame);
        out.attach(row);
    }
    return out;
}

inline void settle_batch(
    netw::repl::SessionSend &p_session,
    const netw::CarrierBatch &p_batch,
    int p_peer,
    uint16_t p_seq
) {
    p_session.commit(
        p_peer,
        p_seq,
        p_batch.rows(),
        p_batch.frame_count(),
        p_batch.bit_count()
    );
}

inline netw::repl::SessionResult drive_send(
    netw::repl::SessionSend &p_session,
    const netw::wire::WireRegistry &p_registry,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers,
    int64_t p_max_bits,
    uint16_t p_seq,
    int64_t p_base_tick = 0
) {
    seat_streams(p_session, p_offers);
    netw::repl::SessionResult out
        = p_session.run(p_registry, p_offers, p_max_bits, p_base_tick);
    godot::LocalVector<int> peers;
    for (uint32_t at = 0; at < out.sends.size(); ++at) {
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
        settle_batch(
            p_session,
            batch_of(p_session, out, peers[slot]),
            peers[slot],
            p_seq
        );
    }
    return out;
}

} // namespace netw_test

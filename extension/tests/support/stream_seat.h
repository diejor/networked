#pragma once

#include "send_rig.h"

#include "godot/local_vector.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/replication_send.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw_test {

inline netw::wire::StreamFamily family_of(const RowOffer &p_offer) {
    return p_offer.family();
}

template <typename Send>
inline void seat_stream(Send &p_send, int p_peer, const RowOffer &p_offer) {
    netw::store::World *world = p_send.store();
    if (world == nullptr || world->token_of(p_peer, p_offer.lane()) != 0) {
        return;
    }
    const uint64_t request
        = p_send.stream_open(p_peer, p_offer.lane(), p_offer.tenure);
    p_send.stream_ready(p_peer, request, netw::wire::next_stream_token());
}

template <typename Send>
inline void seat_sent(
    Send &p_send,
    const godot::LocalVector<RowOffer> &p_offers
) {
    netw::store::World *world = p_send.store();
    if (world == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        stage_offer(*world, p_offers[at], p_offers[at].tick);
    }
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        const RowOffer &offer = p_offers[at];
        for (uint32_t who = 0; who < offer.recipients.size(); ++who) {
            seat_stream(p_send, offer.recipients[who], offer);
        }
    }
}

inline void seat_streams(
    SessionSend &p_send,
    const godot::LocalVector<RowOffer> &p_offers
) {
    seat_sent(p_send, p_offers);
}

inline void seat_streams(
    ReplicationSend &p_send,
    const godot::LocalVector<RowOffer> &p_offers
) {
    seat_sent(p_send, p_offers);
}

template <typename Send>
inline void accept_sent(
    Send &p_send,
    const godot::LocalVector<RowOffer> &p_offers,
    int p_peer
) {
    netw::store::World *world = p_send.store();
    if (world == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        const netw::wire::StreamLane lane = p_offers[at].lane();
        const uint64_t token = world->token_of(p_peer, lane);
        const uint64_t exposed = row_exposed(*world, lane, p_peer);
        if (token != 0 && exposed != 0) {
            p_send.stream_receipt(p_peer, token, exposed);
        }
    }
}

inline void accept_streams(
    SessionSend &p_send,
    const godot::LocalVector<RowOffer> &p_offers,
    int p_peer
) {
    accept_sent(p_send, p_offers, p_peer);
}

inline void accept_streams(
    ReplicationSend &p_send,
    const godot::LocalVector<RowOffer> &p_offers,
    int p_peer
) {
    accept_sent(p_send, p_offers, p_peer);
}

template <typename Send> inline void seat_pending(Send &p_send, int p_peer) {
    netw::store::World *world = p_send.store();
    if (world == nullptr) {
        return;
    }
    const godot::LocalVector<uint64_t> waiting
        = world->opening_requests(p_peer);
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        p_send.stream_ready(
            p_peer,
            waiting[at],
            netw::wire::next_stream_token()
        );
    }
}

} // namespace netw_test

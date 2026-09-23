#pragma once

#include "godot/local_vector.hpp"
#include "netw/replication_send.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw_test {

inline netw::wire::StreamFamily family_of(const netw::repl::RowOffer &p_offer) {
    if (p_offer.windowed) {
        return netw::wire::StreamFamily::WINDOW;
    }
    if (p_offer.reliable) {
        return netw::wire::StreamFamily::RETAINED;
    }
    return netw::wire::StreamFamily::VOLATILE;
}

inline void seat_stream(
    netw::wire::StreamWriterBook &p_writer,
    int p_peer,
    const netw::repl::RowOffer &p_offer
) {
    netw::wire::StreamLane lane;
    lane.route = p_offer.route;
    lane.ordinal = p_offer.comp;
    lane.family = family_of(p_offer);
    if (p_writer.token_of(p_peer, lane) != 0) {
        return;
    }
    const uint64_t request = p_writer.open(
        p_peer,
        lane,
        uint64_t(p_offer.life),
        uint32_t(p_offer.declared().shape_hash),
        p_offer.tenure
    );
    p_writer.ready(p_peer, request, netw::wire::next_stream_token());
}

inline void seat_streams(
    netw::wire::StreamWriterBook &p_writer,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers
) {
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        const netw::repl::RowOffer &offer = p_offers[at];
        for (uint32_t who = 0; who < offer.recipients.size(); ++who) {
            seat_stream(p_writer, offer.recipients[who], offer);
        }
    }
}

inline void accept_streams(
    netw::wire::StreamWriterBook &p_writer,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers,
    int p_peer
) {
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        const netw::repl::RowOffer &offer = p_offers[at];
        netw::wire::StreamLane lane;
        lane.route = offer.route;
        lane.ordinal = offer.comp;
        lane.family = family_of(offer);
        const uint64_t token = p_writer.token_of(p_peer, lane);
        netw::wire::SnapshotSender *stream = p_writer.sender(p_peer, lane);
        if (token != 0 && stream != nullptr) {
            p_writer.receipt(p_peer, token, stream->exposed_high_water());
        }
    }
}

inline void accept_streams(
    netw::repl::SessionSend &p_send,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers,
    int p_peer
) {
    accept_streams(p_send.writer_book(), p_offers, p_peer);
}

inline void accept_streams(
    netw::ReplicationSend &p_send,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers,
    int p_peer
) {
    accept_streams(p_send.writer_book(), p_offers, p_peer);
}

inline void seat_pending(
    netw::wire::StreamWriterBook &p_writer,
    int p_peer
) {
    const godot::LocalVector<netw::wire::StreamLane> waiting
        = p_writer.unready(p_peer);
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        const uint64_t request = p_writer.request_of(p_peer, waiting[at]);
        p_writer.ready(p_peer, request, netw::wire::next_stream_token());
    }
}

inline void seat_pending(netw::ReplicationSend &p_send, int p_peer) {
    seat_pending(p_send.writer_book(), p_peer);
}

inline void seat_streams(
    netw::repl::SessionSend &p_send,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers
) {
    seat_streams(p_send.writer_book(), p_offers);
}

inline void seat_streams(
    netw::ReplicationSend &p_send,
    const godot::LocalVector<netw::repl::RowOffer> &p_offers
) {
    seat_streams(p_send.writer_book(), p_offers);
}

} // namespace netw_test

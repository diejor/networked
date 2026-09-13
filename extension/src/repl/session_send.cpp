#include "netw/repl/session_send.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

namespace {

RowFrameHeader header_of(const RowOffer &p_offer, uint64_t p_mask) {
    RowFrameHeader header;
    header.life = p_offer.life % ROW_LIFE_WRAP;
    header.reconcile_ack = p_offer.ack;
    header.tick = p_offer.tick;
    header.mask = p_mask;
    return header;
}

} // namespace

PackedByteArray SessionSend::price(
    const RowSend &p_send,
    int64_t p_base_tick
) const {
    if (!stage.is_valid()) {
        return p_send.bytes;
    }
    Array args;
    args.push_back(int64_t(p_send.peer));
    args.push_back(p_base_tick);
    args.push_back(p_send.route);
    args.push_back(int64_t(p_send.comp));
    args.push_back(int64_t(p_send.mask));
    args.push_back(p_send.bytes);
    return stage.callv(args);
}

LocalVector<RowSend> SessionSend::collect(
    const LocalVector<RowOffer> &p_offers,
    int64_t p_base_tick,
    LocalVector<wire::FitCandidate> &r_candidates,
    SessionResult &r_out
) {
    LocalVector<RowSend> staged;
    for (uint32_t at = 0; at < p_offers.size(); ++at) {
        const RowOffer &offer = p_offers[at];
        if (offer.windowed) {
            WindowRing *ring = lanes.open_window(
                offer.route,
                offer.comp,
                offer.declared(),
                offer.window
            );
            wire::CodeRow row;
            if (ring == nullptr || !ring->valid()) {
                NETW_WARN_ONCE(
                    sys::WIRE,
                    "windowed offer on route %d comp %d opened no ring",
                    int(offer.route),
                    int(offer.comp)
                );
                r_out.ungathered += 1;
                note_offer_verdict(offer, RowVerdict::UNGATHERED, p_base_tick);
                continue;
            }
            if (!ring->gather(offer.values, row)) {
                NETW_WARN_ONCE(
                    sys::WIRE,
                    "windowed offer on route %d comp %d did not gather",
                    int(offer.route),
                    int(offer.comp)
                );
                r_out.ungathered += 1;
                note_offer_verdict(offer, RowVerdict::UNGATHERED, p_base_tick);
                continue;
            }
            ring->record(offer.tick, row);
            const godot::LocalVector<WindowSample> samples = ring->pending();
            if (samples.is_empty()) {
                r_out.caught_up += 1;
                note_offer_verdict(offer, RowVerdict::CAUGHT_UP, p_base_tick);
                continue;
            }
            NETW_WARN_COND_ONCE(
                offer.recipients.size() > 1,
                sys::WIRE,
                "a window ring holds one floor for every recipient, so route "
                "%d comp %d offered to %d peers settles them against each "
                "other",
                int(offer.route),
                int(offer.comp),
                int(offer.recipients.size())
            );
            const uint64_t whole = ring->plan().full_mask();
            const PackedByteArray window_bytes = write_window_frame(
                header_of(offer, whole),
                p_base_tick,
                ring->plan(),
                samples
            );
            for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
                RowSend send;
                send.route = offer.route;
                send.comp = offer.comp;
                send.peer = offer.recipients[which];
                send.offer = at;
                send.mask = whole;
                send.windowed = true;
                send.sample_count = samples.size();
                send.row = row;
                send.bytes = window_bytes;
                send.bytes = price(send, p_base_tick);
                if (send.bytes.is_empty()) {
                    r_out.staged_out += 1;
                    note_verdict(
                        offer.route,
                        offer.comp,
                        send.peer,
                        RowVerdict::REFUSED,
                        p_base_tick
                    );
                    continue;
                }
                send.bits = int64_t(send.bytes.size()) * 8;
                staged.push_back(send);

                wire::FitCandidate candidate;
                candidate.channel_id = offer.channel;
                candidate.peer = send.peer;
                candidate.bytes = send.bytes.size();
                candidate.payload_bits = send.bits;
                candidate.priority = offer.priority;
                candidate.accumulated_priority = owed_by(send, offer.priority);
                candidate.send_id = int64_t(staged.size()) - 1;
                r_candidates.push_back(candidate);
            }
            continue;
        }
        if (offer.reliable) {
            RetainedLane *reliable = lanes.open_retained(
                offer.route,
                offer.comp,
                offer.declared()
            );
            wire::CodeRow row;
            if (reliable == nullptr || !reliable->valid()
                || !reliable->gather(offer.values, row)) {
                r_out.ungathered += 1;
                note_offer_verdict(offer, RowVerdict::UNGATHERED, p_base_tick);
                continue;
            }
            for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
                const int peer = offer.recipients[which];
                const RetainedLane::Delivery ordered
                    = reliable->send(peer, row);
                const uint64_t mask = ordered.mask;
                if (mask == 0) {
                    r_out.caught_up += 1;
                    note_verdict(
                        offer.route,
                        offer.comp,
                        peer,
                        RowVerdict::CAUGHT_UP,
                        p_base_tick
                    );
                    continue;
                }
                RowSend send;
                send.route = offer.route;
                send.comp = offer.comp;
                send.peer = peer;
                send.offer = at;
                send.mask = mask;
                send.masked = true;
                send.reliable = true;
                send.row = row;
                RowBaseline stepping;
                stepping.naming = BaselineNaming::BY_ORDER;
                if (ordered.steps_from_baseline) {
                    stepping.row = &ordered.ordered_baseline;
                }
                send.bytes = write_row_frame(
                    header_of(offer, mask),
                    p_base_tick,
                    reliable->plan(),
                    row,
                    stepping
                );
                send.bytes = price(send, p_base_tick);
                if (send.bytes.is_empty()) {
                    r_out.staged_out += 1;
                    note_verdict(
                        offer.route,
                        offer.comp,
                        peer,
                        RowVerdict::REFUSED,
                        p_base_tick
                    );
                    continue;
                }
                send.bits = int64_t(send.bytes.size()) * 8;
                note_verdict(
                    offer.route,
                    offer.comp,
                    peer,
                    RowVerdict::SENT,
                    p_base_tick
                );
                staged.push_back(send);
            }
            continue;
        }
        RowLane *lane = lanes.open(offer.route, offer.comp, offer.declared());
        if (lane == nullptr || !lane->valid()) {
            NETW_WARN_ONCE(
                sys::WIRE,
                "offer on route %d comp %d opened no lane",
                int(offer.route),
                int(offer.comp)
            );
            r_out.ungathered += 1;
            note_offer_verdict(offer, RowVerdict::UNGATHERED, p_base_tick);
            continue;
        }
        wire::CodeRow row;
        if (!lane->gather(offer.values, row)) {
            NETW_WARN_ONCE(
                sys::WIRE,
                "offer on route %d comp %d did not gather",
                int(offer.route),
                int(offer.comp)
            );
            r_out.ungathered += 1;
            note_offer_verdict(offer, RowVerdict::UNGATHERED, p_base_tick);
            continue;
        }
        for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
            const int peer = offer.recipients[which];
            const uint64_t mask = offer.masked ? lane->mask_for(peer, row)
                                               : lane->plan().full_mask();
            if (mask == 0) {
                r_out.caught_up += 1;
                note_verdict(
                    offer.route,
                    offer.comp,
                    peer,
                    RowVerdict::CAUGHT_UP,
                    p_base_tick
                );
                continue;
            }
            RowSend send;
            send.route = offer.route;
            send.comp = offer.comp;
            send.peer = peer;
            send.offer = at;
            send.mask = mask;
            send.masked = offer.masked;
            send.row = row;
            send.bytes = write_row_frame(
                header_of(offer, mask),
                p_base_tick,
                lane->plan(),
                row
            );
            send.bytes = price(send, p_base_tick);
            if (send.bytes.is_empty()) {
                r_out.staged_out += 1;
                note_verdict(
                    offer.route,
                    offer.comp,
                    peer,
                    RowVerdict::REFUSED,
                    p_base_tick
                );
                continue;
            }
            send.bits = int64_t(send.bytes.size()) * 8;
            staged.push_back(send);

            wire::FitCandidate candidate;
            candidate.channel_id = offer.channel;
            candidate.peer = peer;
            candidate.bytes = send.bytes.size();
            candidate.payload_bits = send.bits;
            candidate.priority = offer.priority;
            candidate.accumulated_priority = owed_by(send, offer.priority);
            candidate.send_id = int64_t(staged.size()) - 1;
            r_candidates.push_back(candidate);
        }
    }
    return staged;
}

uint64_t SessionSend::address_of(int64_t p_route, uint8_t p_comp) {
    return (uint64_t(p_route) << 8) | uint64_t(p_comp);
}

uint64_t SessionSend::address_of(const RowSend &p_send) {
    return address_of(p_send.route, p_send.comp);
}

void SessionSend::note_verdict(
    int64_t p_route,
    uint8_t p_comp,
    int p_peer,
    RowVerdict p_verdict,
    int64_t p_tick
) {
    RowExplain &held = verdicts[p_peer][address_of(p_route, p_comp)];
    held.verdict = p_verdict;
    held.tick = p_tick;
}

void SessionSend::note_offer_verdict(
    const RowOffer &p_offer,
    RowVerdict p_verdict,
    int64_t p_tick
) {
    for (uint32_t at = 0; at < p_offer.recipients.size(); ++at) {
        note_verdict(
            p_offer.route,
            p_offer.comp,
            p_offer.recipients[at],
            p_verdict,
            p_tick
        );
    }
}

RowExplain SessionSend::explain(
    int64_t p_route,
    uint8_t p_comp,
    int p_peer
) const {
    RowExplain out;
    const godot::HashMap<uint64_t, RowExplain> *book = verdicts.getptr(p_peer);
    if (book != nullptr) {
        const RowExplain *held = book->getptr(address_of(p_route, p_comp));
        if (held != nullptr) {
            out = *held;
        }
    }
    const RowLane *lane = const_cast<LaneSet &>(lanes).find(p_route, p_comp);
    if (lane != nullptr) {
        out.sticky = lane->sticky(p_peer);
        out.in_flight = lane->in_flight(p_peer);
        out.has_baseline = lane->knows(p_peer);
    }
    return out;
}

float SessionSend::owed_by(const RowSend &p_send, float p_priority) const {
    const godot::HashMap<uint64_t, float> *book = owed.getptr(p_send.peer);
    if (book == nullptr) {
        return p_priority;
    }
    const float *held = book->getptr(address_of(p_send));
    return held == nullptr ? p_priority : *held;
}

SessionResult SessionSend::run(
    const wire::WireRegistry &p_registry,
    const LocalVector<RowOffer> &p_offers,
    int64_t p_max_bits,
    int64_t p_base_tick
) {
    NETW_ZONE_NC("Session send", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    SessionResult out;

    LocalVector<wire::FitCandidate> candidates;
    LocalVector<RowSend> staged
        = collect(p_offers, p_base_tick, candidates, out);

    LocalVector<int> peers;
    LocalVector<int64_t> reliable_bits;
    for (uint32_t at = 0; at < staged.size(); ++at) {
        const RowSend &send = staged[at];
        uint32_t slot = 0;
        while (slot < peers.size() && peers[slot] != send.peer) {
            ++slot;
        }
        if (slot == peers.size()) {
            peers.push_back(send.peer);
            reliable_bits.push_back(0);
        }
        if (send.reliable) {
            reliable_bits[slot] += send.bits;
            out.sends.push_back(send);
        }
    }

    for (uint32_t slot = 0; slot < peers.size(); ++slot) {
        const int peer = peers[slot];
        LocalVector<wire::FitCandidate> mine;
        for (uint32_t at = 0; at < candidates.size(); ++at) {
            if (candidates[at].peer == peer) {
                mine.push_back(candidates[at]);
            }
        }
        if (mine.is_empty()) {
            continue;
        }
        const int64_t governed = link.budget_bits(peer, p_max_bits);
        const int64_t floor
            = governed < VOLATILE_FLOOR_BITS ? governed : VOLATILE_FLOOR_BITS;
        const int64_t left = governed - reliable_bits[slot];
        const int64_t volatile_bits = left > floor ? left : floor;
        const PassResult fitted
            = pass.run(p_registry, peer, mine, volatile_bits);
        out.sent_bits += fitted.sent_bits;
        out.deferred += fitted.deferred.size();
        for (uint32_t at = 0; at < fitted.sent.size(); ++at) {
            const int64_t which = fitted.sent[at].send_id;
            if (which < 0 || uint32_t(which) >= staged.size()) {
                continue;
            }
            const RowSend &send = staged[uint32_t(which)];
            godot::HashMap<uint64_t, float> *book = owed.getptr(peer);
            if (book != nullptr) {
                book->erase(address_of(send));
            }
            note_verdict(
                send.route,
                send.comp,
                peer,
                RowVerdict::SENT,
                p_base_tick
            );
            out.sends.push_back(send);
        }
        for (uint32_t at = 0; at < fitted.deferred.size(); ++at) {
            const int64_t which = fitted.deferred[at].send_id;
            if (which < 0 || uint32_t(which) >= staged.size()) {
                continue;
            }
            const RowSend &send = staged[uint32_t(which)];
            owed[peer][address_of(send)]
                = fitted.deferred[at].accumulated_priority;
            note_verdict(
                send.route,
                send.comp,
                peer,
                RowVerdict::DEFERRED,
                p_base_tick
            );
        }
    }
    return out;
}

void SessionSend::defer(const RowSend &p_send) {
    if (p_send.reliable) {
        return;
    }
    Pending held;
    held.route = p_send.route;
    held.comp = p_send.comp;
    held.bits = p_send.bits;
    held.masked = p_send.masked;
    held.row = p_send.row;
    pending[p_send.peer].push_back(held);
}

bool SessionSend::commit(int p_peer, uint16_t p_seq) {
    godot::HashMap<int, LocalVector<Pending>>::Iterator held
        = pending.find(p_peer);
    if (held == pending.end()) {
        return true;
    }
    int64_t bits = 0;
    for (uint32_t at = 0; at < held->value.size(); ++at) {
        const Pending &row = held->value[at];
        bits += row.bits;
        RowLane *lane = lanes.find(row.route, row.comp);
        if (lane != nullptr && row.masked) {
            lane->stage(p_peer, p_seq, row.row);
        }
    }
    const uint16_t frames = uint16_t(held->value.size());
    pending.erase(p_peer);
    return pass.record_datagram(p_peer, p_seq, frames, bits);
}

uint32_t SessionSend::pending_count(int p_peer) const {
    godot::HashMap<int, LocalVector<Pending>>::ConstIterator held
        = pending.find(p_peer);
    return held == pending.end() ? 0 : held->value.size();
}

void SessionSend::forget_peer(int p_peer) {
    lanes.forget_peer(p_peer);
    pending.erase(p_peer);
    owed.erase(p_peer);
    verdicts.erase(p_peer);
    link.forget(p_peer);
    pass.forget(p_peer);
}

void SessionSend::close_route(int64_t p_route) {
    lanes.close_route(p_route);
    for (godot::KeyValue<int, godot::HashMap<uint64_t, float>> &book : owed) {
        LocalVector<uint64_t> gone;
        for (const godot::KeyValue<uint64_t, float> &row : book.value) {
            if (int64_t(row.key >> 8) == p_route) {
                gone.push_back(row.key);
            }
        }
        for (uint32_t at = 0; at < gone.size(); ++at) {
            book.value.erase(gone[at]);
        }
    }
    for (godot::KeyValue<int, godot::HashMap<uint64_t, RowExplain>> &book :
         verdicts) {
        LocalVector<uint64_t> gone;
        for (const godot::KeyValue<uint64_t, RowExplain> &row : book.value) {
            if (int64_t(row.key >> 8) == p_route) {
                gone.push_back(row.key);
            }
        }
        for (uint32_t at = 0; at < gone.size(); ++at) {
            book.value.erase(gone[at]);
        }
    }
}

AckReport SessionSend::acknowledge(
    int p_peer,
    uint16_t p_acked_seq,
    uint32_t p_history,
    double p_rtt_ms,
    double p_jitter_ms,
    int64_t p_tick
) {
    LocalVector<wire::AckEntry> delivered;
    LocalVector<wire::AckEntry> lost;
    pass.acknowledge(p_peer, p_acked_seq, p_history, delivered, lost);
    lanes.acknowledge_peer(p_peer, p_acked_seq, p_history);
    link.note_ack(
        p_peer,
        delivered.size(),
        lost.size(),
        p_rtt_ms,
        p_jitter_ms,
        p_tick
    );

    AckReport report;
    report.delivered = delivered.size();
    report.lost = lost.size();
    for (uint32_t at = 0; at < delivered.size(); ++at) {
        report.delivered_bits += delivered[at].bits;
    }
    return report;
}

void SessionSend::retain(const LocalVector<int> &p_recipients) {
    lanes.retain(p_recipients);
    LocalVector<int> forgotten;
    for (const godot::KeyValue<int, LocalVector<Pending>> &held : pending) {
        bool kept = false;
        for (uint32_t at = 0; at < p_recipients.size(); ++at) {
            if (p_recipients[at] == held.key) {
                kept = true;
                break;
            }
        }
        if (!kept) {
            forgotten.push_back(held.key);
        }
    }
    for (const godot::KeyValue<int, godot::HashMap<uint64_t, float>> &book :
         owed) {
        bool kept = false;
        for (uint32_t at = 0; at < p_recipients.size(); ++at) {
            if (p_recipients[at] == book.key) {
                kept = true;
                break;
            }
        }
        if (!kept) {
            forgotten.push_back(book.key);
        }
    }
    for (uint32_t at = 0; at < forgotten.size(); ++at) {
        pending.erase(forgotten[at]);
        owed.erase(forgotten[at]);
        verdicts.erase(forgotten[at]);
        link.forget(forgotten[at]);
        pass.forget(forgotten[at]);
    }
}

} // namespace netw::repl

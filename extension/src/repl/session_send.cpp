#include "netw/repl/session_send.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

LocalVector<RowSend> SessionSend::collect(
    const LocalVector<RowOffer> &p_offers,
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
                offer.schema,
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
                continue;
            }
            ring->record(offer.tick, row);
            const godot::LocalVector<WindowSample> samples = ring->pending();
            if (samples.is_empty()) {
                r_out.caught_up += 1;
                continue;
            }
            // Every recipient is sent the same window. The lane holds no
            // per-peer state, because a sample the receiver already stepped is
            // dropped by the floor rather than by knowing who has it.
            for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
                RowSend send;
                send.route = offer.route;
                send.comp = offer.comp;
                send.peer = offer.recipients[which];
                send.mask = ring->plan().full_mask();
                send.windowed = true;
                send.row = row;
                send.samples = samples;
                staged.push_back(send);

                wire::FitCandidate candidate;
                candidate.channel_id = offer.channel;
                candidate.payload_bits
                    = int64_t(samples.size()) * ring->plan().row_bits();
                candidate.priority = offer.priority;
                candidate.accumulated_priority = offer.priority;
                candidate.send_id = int64_t(staged.size()) - 1;
                r_candidates.push_back(candidate);
            }
            continue;
        }
        if (offer.reliable) {
            RetainedLane *reliable
                = lanes.open_retained(offer.route, offer.comp, offer.schema);
            wire::CodeRow row;
            if (reliable == nullptr || !reliable->valid()
                || !reliable->gather(offer.values, row)) {
                r_out.ungathered += 1;
                continue;
            }
            for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
                const int peer = offer.recipients[which];
                const uint64_t mask = reliable->send(peer, row);
                if (mask == 0) {
                    r_out.caught_up += 1;
                    continue;
                }
                RowSend send;
                send.route = offer.route;
                send.comp = offer.comp;
                send.peer = peer;
                send.mask = mask;
                send.masked = true;
                send.reliable = true;
                send.row = row;
                staged.push_back(send);
            }
            continue;
        }
        RowLane *lane = lanes.open(offer.route, offer.comp, offer.schema);
        if (lane == nullptr || !lane->valid()) {
            NETW_WARN_ONCE(
                sys::WIRE,
                "offer on route %d comp %d opened no lane",
                int(offer.route),
                int(offer.comp)
            );
            r_out.ungathered += 1;
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
            continue;
        }
        for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
            const int peer = offer.recipients[which];
            const uint64_t mask = offer.masked ? lane->mask_for(peer, row)
                                               : lane->plan().full_mask();
            if (mask == 0) {
                r_out.caught_up += 1;
                continue;
            }
            RowSend send;
            send.route = offer.route;
            send.comp = offer.comp;
            send.peer = peer;
            send.mask = mask;
            send.masked = offer.masked;
            send.row = row;
            staged.push_back(send);

            wire::FitCandidate candidate;
            candidate.channel_id = offer.channel;
            candidate.payload_bits = lane->plan().row_bits();
            candidate.priority = offer.priority;
            candidate.accumulated_priority = offer.priority;
            candidate.send_id = int64_t(staged.size()) - 1;
            r_candidates.push_back(candidate);
        }
    }
    return staged;
}

SessionResult SessionSend::run(
    const wire::WireRegistry &p_registry,
    const LocalVector<RowOffer> &p_offers,
    int64_t p_max_bits,
    uint16_t p_seq,
    int64_t p_send_id
) {
    NETW_ZONE_NC("Session send", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    SessionResult out;

    LocalVector<wire::FitCandidate> candidates;
    LocalVector<RowSend> staged = collect(p_offers, candidates, out);

    const PassResult fitted
        = pass.run(p_registry, candidates, p_max_bits, p_seq, p_send_id);
    out.untrackable = fitted.untrackable;
    out.sent_bits = fitted.sent_bits;

    // A reliable row was never a candidate, so it rides whatever the fitter
    // decided. Its lane advanced when the mask was taken, and there is no
    // later frame that would carry the columns a deferral dropped.
    for (uint32_t at = 0; at < staged.size(); ++at) {
        if (staged[at].reliable) {
            out.sends.push_back(staged[at]);
        }
    }

    // Only what actually rides is staged. Staging a row the fitter deferred
    // would tell the book a peer holds what it was never sent, and the next
    // pass would diff against it.
    for (uint32_t at = 0; at < fitted.sent.size(); ++at) {
        const int64_t which = fitted.sent[at].send_id;
        if (which < 0 || uint32_t(which) >= staged.size()) {
            continue;
        }
        const RowSend &send = staged[uint32_t(which)];
        RowLane *lane = lanes.find(send.route, send.comp);
        if (lane != nullptr && send.masked) {
            lane->stage(send.peer, p_seq, send.row);
        }
        out.sends.push_back(send);
    }
    return out;
}

SessionResult SessionSend::run_deferred(const LocalVector<RowOffer> &p_offers) {
    NETW_ZONE_NC("Session send deferred", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    SessionResult out;

    LocalVector<wire::FitCandidate> candidates;
    const LocalVector<RowSend> staged = collect(p_offers, candidates, out);
    for (uint32_t at = 0; at < staged.size(); ++at) {
        out.sends.push_back(staged[at]);
    }
    return out;
}

void SessionSend::defer(const RowSend &p_send) {
    if (!p_send.masked || p_send.reliable) {
        return;
    }
    Pending held;
    held.route = p_send.route;
    held.comp = p_send.comp;
    held.row = p_send.row;
    pending[p_send.peer].push_back(held);
}

void SessionSend::commit(int p_peer, uint16_t p_seq) {
    godot::HashMap<int, LocalVector<Pending>>::Iterator held
        = pending.find(p_peer);
    if (held == pending.end()) {
        return;
    }
    for (uint32_t at = 0; at < held->value.size(); ++at) {
        const Pending &row = held->value[at];
        RowLane *lane = lanes.find(row.route, row.comp);
        if (lane != nullptr) {
            lane->stage(p_peer, p_seq, row.row);
        }
    }
    pending.erase(p_peer);
}

uint32_t SessionSend::pending_count(int p_peer) const {
    godot::HashMap<int, LocalVector<Pending>>::ConstIterator held
        = pending.find(p_peer);
    return held == pending.end() ? 0 : held->value.size();
}

void SessionSend::forget_peer(int p_peer) {
    lanes.forget_peer(p_peer);
    pending.erase(p_peer);
}

void SessionSend::acknowledge(int p_peer, uint16_t p_acked_seq) {
    LocalVector<wire::AckEntry> delivered;
    LocalVector<wire::AckEntry> lost;
    pass.acknowledge(p_acked_seq, delivered, lost);
    lanes.acknowledge_peer(p_peer, p_acked_seq);
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
    for (uint32_t at = 0; at < forgotten.size(); ++at) {
        pending.erase(forgotten[at]);
    }
}

} // namespace netw::repl

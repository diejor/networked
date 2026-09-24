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

wire::SnapshotSender *SessionSend::stream_ready(
    int p_peer,
    const RowOffer &p_offer,
    wire::StreamFamily p_family,
    uint64_t &r_token,
    wire::StreamLane &r_lane
) {
    wire::StreamLane lane;
    lane.route = p_offer.route;
    lane.ordinal = p_offer.comp;
    lane.family = p_family;
    r_lane = lane;
    if (!writers.holds_tenure(p_peer, lane, p_offer.tenure)) {
        uint64_t closed = 0;
        if (writers.close(p_peer, lane, closed)) {
            queue_close(p_peer, closed);
        }
    }
    r_token = writers.token_of(p_peer, lane);
    if (r_token != 0) {
        return writers.sender(p_peer, lane);
    }
    const uint64_t epoch = uint64_t(p_offer.life);
    const uint32_t schema = uint32_t(p_offer.declared().shape_hash);
    const uint64_t request
        = writers.open(p_peer, lane, epoch, schema, p_offer.tenure);
    if (request == 0) {
        return nullptr;
    }
    wire::ControlRecord asking;
    asking.tag = wire::ControlTag::OPEN;
    asking.request = request;
    asking.route = lane.route;
    asking.ordinal = lane.ordinal;
    asking.family = p_family;
    asking.epoch = epoch;
    asking.tenure = p_offer.tenure.tenure;
    asking.anchor = p_offer.tenure.anchor;
    asking.anchor_author = p_offer.tenure.anchor_author;
    asking.schema = schema;
    control.queue(p_peer, asking);
    return nullptr;
}

void SessionSend::queue_close(int p_peer, uint64_t p_token) {
    control.drop_token(p_peer, p_token);
    wire::ControlRecord closing;
    closing.tag = wire::ControlTag::CLOSE;
    closing.token = p_token;
    control.queue(p_peer, closing);
}

void SessionSend::close_tenures(int64_t p_route, uint64_t p_tenure) {
    queue_closes(writers.close_tenures_other_than(p_route, p_tenure));
}

void SessionSend::close_anchors(
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_author
) {
    queue_closes(writers.close_anchors_other_than(p_route, p_anchor, p_author));
}

void SessionSend::queue_closes(const LocalVector<wire::ClosedLane> &p_closed) {
    for (uint32_t at = 0; at < p_closed.size(); ++at) {
        queue_close(p_closed[at].peer, p_closed[at].token);
    }
}

LocalVector<wire::ParkedOpen> SessionSend::expire_parks(int64_t p_now_ms) {
    LocalVector<wire::ParkedOpen> out;
    const LocalVector<int> parked = readers.parked_peers();
    for (uint32_t at = 0; at < parked.size(); ++at) {
        const int64_t interval
            = wire::repair_interval_ms(int64_t(link.rtt_ms(parked[at])));
        const LocalVector<wire::ParkedOpen> expired
            = readers.expire_parks(parked[at], p_now_ms, interval);
        for (uint32_t which = 0; which < expired.size(); ++which) {
            out.push_back(expired[which]);
        }
    }
    return out;
}

bool SessionSend::row_is_owed(
    wire::SnapshotSender &p_stream,
    int p_peer,
    const wire::StreamLane &p_lane,
    const RowOffer &p_offer,
    int64_t p_base_tick,
    int64_t p_now_ms,
    bool &r_repairing,
    bool &r_beat
) {
    r_repairing = false;
    r_beat = false;
    if (p_stream.quiet()) {
        r_beat = p_stream.beat_due(p_base_tick, p_offer.heartbeat);
        return r_beat;
    }
    if (!p_stream.awaiting_receipt()) {
        return true;
    }
    r_repairing = true;
    return writers.repair_due(
        p_peer,
        p_lane,
        p_now_ms,
        int64_t(link.rtt_ms(p_peer))
    );
}

uint64_t SessionSend::revision_for(
    wire::SnapshotSender &p_stream,
    int p_peer,
    const wire::StreamLane &p_lane,
    int64_t p_base_tick,
    int64_t p_now_ms,
    bool p_repairing
) {
    if (p_repairing) {
        return p_stream.pinned_repair();
    }
    writers.note_attempt(p_peer, p_lane, p_now_ms);
    return p_stream.mint(p_base_tick);
}

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
    int64_t p_now_ms,
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
            for (uint32_t which = 0; which < offer.recipients.size(); ++which) {
                uint64_t token = 0;
                wire::StreamLane lane;
                wire::SnapshotSender *stream = stream_ready(
                    offer.recipients[which],
                    offer,
                    wire::StreamFamily::WINDOW,
                    token,
                    lane
                );
                if (stream == nullptr) {
                    r_out.deferred += 1;
                    note_verdict(
                        offer.route,
                        offer.comp,
                        offer.recipients[which],
                        RowVerdict::DEFERRED,
                        p_base_tick
                    );
                    continue;
                }
                SnapshotHeader header;
                header.token = token;
                header.revision = stream->reserve();
                header.reconcile_ack = offer.ack;
                RowSend send;
                send.route = offer.route;
                send.comp = offer.comp;
                send.peer = offer.recipients[which];
                send.offer = at;
                send.mask = whole;
                send.token = token;
                send.revision = header.revision;
                send.windowed = true;
                send.sample_count = samples.size();
                send.row = row;
                send.bytes = write_snapshot_window(
                    header,
                    p_base_tick,
                    ring->plan(),
                    samples
                );
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
                uint64_t token = 0;
                wire::StreamLane lane;
                wire::SnapshotSender *stream = stream_ready(
                    peer,
                    offer,
                    wire::StreamFamily::RETAINED,
                    token,
                    lane
                );
                if (stream == nullptr) {
                    r_out.deferred += 1;
                    note_verdict(
                        offer.route,
                        offer.comp,
                        peer,
                        RowVerdict::DEFERRED,
                        p_base_tick
                    );
                    continue;
                }
                stream->desire(row);
                bool repairing = false;
                bool beat = false;
                if (!row_is_owed(
                        *stream,
                        peer,
                        lane,
                        offer,
                        p_base_tick,
                        p_now_ms,
                        repairing,
                        beat
                    )) {
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
                SnapshotHeader header;
                header.token = token;
                header.revision = revision_for(
                    *stream,
                    peer,
                    lane,
                    p_base_tick,
                    p_now_ms,
                    repairing
                );
                header.distance = repairing || beat
                    ? 0
                    : stream->distance_for(header.revision);
                header.tick = offer.tick;
                header.reconcile_ack = offer.ack;
                const wire::CodeRow *baseline
                    = header.absolute() ? nullptr : stream->confirmed();
                header.mask = header.absolute()
                    ? reliable->plan().full_mask()
                    : wire::CodeRow::changed_mask(
                          reliable->plan(),
                          *baseline,
                          row
                      );
                RowSend send;
                send.route = offer.route;
                send.comp = offer.comp;
                send.peer = peer;
                send.offer = at;
                send.mask = header.mask;
                send.token = token;
                send.revision = header.revision;
                send.masked = !header.absolute();
                send.reliable = true;
                send.row = row;
                send.bytes = write_snapshot_row(
                    header,
                    p_base_tick,
                    reliable->plan(),
                    row,
                    baseline
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
            uint64_t token = 0;
            wire::StreamLane stream_lane;
            wire::SnapshotSender *stream = stream_ready(
                peer,
                offer,
                wire::StreamFamily::VOLATILE,
                token,
                stream_lane
            );
            if (stream == nullptr) {
                r_out.deferred += 1;
                note_verdict(
                    offer.route,
                    offer.comp,
                    peer,
                    RowVerdict::DEFERRED,
                    p_base_tick
                );
                continue;
            }
            stream->desire(row);
            bool repairing = false;
            bool beat = false;
            if (!row_is_owed(
                    *stream,
                    peer,
                    stream_lane,
                    offer,
                    p_base_tick,
                    p_now_ms,
                    repairing,
                    beat
                )) {
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
            SnapshotHeader header;
            header.token = token;
            header.revision = revision_for(
                *stream,
                peer,
                stream_lane,
                p_base_tick,
                p_now_ms,
                repairing
            );
            header.distance = repairing || beat
                ? 0
                : stream->distance_for(header.revision);
            header.tick = offer.tick;
            header.reconcile_ack = offer.ack;
            const wire::CodeRow *baseline
                = header.absolute() ? nullptr : stream->confirmed();
            header.mask = header.absolute()
                ? lane->plan().full_mask()
                : wire::CodeRow::changed_mask(lane->plan(), *baseline, row);
            RowSend send;
            send.route = offer.route;
            send.comp = offer.comp;
            send.peer = peer;
            send.offer = at;
            send.mask = header.mask;
            send.token = token;
            send.revision = header.revision;
            send.masked = !header.absolute();
            send.row = row;
            send.bytes = write_snapshot_row(
                header,
                p_base_tick,
                lane->plan(),
                row,
                baseline
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
    const wire::StreamFamily families[3] = {
        wire::StreamFamily::VOLATILE,
        wire::StreamFamily::RETAINED,
        wire::StreamFamily::WINDOW,
    };
    for (uint32_t family = 0; family < 3; ++family) {
        wire::StreamLane named;
        named.route = p_route;
        named.ordinal = p_comp;
        named.family = families[family];
        const wire::SnapshotSender *stream
            = const_cast<wire::StreamWriterBook &>(writers)
                  .sender(p_peer, named);
        if (stream == nullptr) {
            continue;
        }
        out.confirmed = stream->confirmed_at();
        out.exposed = stream->exposed_high_water();
        out.has_baseline = stream->confirmed() != nullptr;
        break;
    }
    return out;
}

void SessionSend::expose_send(const RowSend &p_send) {
    wire::SnapshotSender *stream
        = writers.sender_of_token(p_send.peer, p_send.token);
    if (stream != nullptr) {
        stream->expose(p_send.revision, p_send.row);
    }
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
    int64_t p_base_tick,
    int64_t p_now_ms
) {
    NETW_ZONE_NC("Session send", colors::WIRE);
    NETW_ZONE_VALUE(p_offers.size());
    SessionResult out;

    LocalVector<wire::FitCandidate> candidates;
    LocalVector<RowSend> staged
        = collect(p_offers, p_base_tick, p_now_ms, candidates, out);

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
            expose_send(send);
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

bool SessionSend::describe(const RowSend &p_send, CarrierRow &r_row) {
    if (p_send.reliable) {
        return false;
    }
    r_row.route = p_send.route;
    r_row.comp = p_send.comp;
    r_row.bits = p_send.bits;
    r_row.token = p_send.token;
    r_row.revision = p_send.revision;
    r_row.masked = p_send.masked;
    r_row.row.copy_from(p_send.row);
    expose_send(p_send);
    return true;
}

bool SessionSend::commit(
    int p_peer,
    uint16_t p_seq,
    const LocalVector<CarrierRow> &p_rows,
    int64_t p_frames,
    int64_t p_bits
) {
    return pass.record_datagram(
        p_peer,
        p_seq,
        uint16_t(p_frames),
        p_bits
    );
}

void SessionSend::cancel(
    int p_peer,
    const LocalVector<CarrierRow> &p_rows,
    int64_t p_tick
) {
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        wire::SnapshotSender *stream
            = writers.sender_of_token(p_peer, p_rows[at].token);
        if (stream != nullptr) {
            stream->withdraw(p_rows[at].revision);
        }
        note_verdict(
            p_rows[at].route,
            p_rows[at].comp,
            p_peer,
            RowVerdict::REFUSED,
            p_tick
        );
    }
}

void SessionSend::forget_peer(int p_peer) {
    owed.erase(p_peer);
    verdicts.erase(p_peer);
    link.forget(p_peer);
    pass.forget(p_peer);
    readers.forget_peer(p_peer);
    writers.forget_peer(p_peer);
    control.forget_peer(p_peer);
}

void SessionSend::close_route(int64_t p_route) {
    lanes.close_route(p_route);
    readers.close_route(p_route);
    writers.close_route(p_route);
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

LocalVector<int> SessionSend::known_peers() const {
    LocalVector<int> known;
    for (const godot::KeyValue<int, godot::HashMap<uint64_t, float>> &book :
         owed) {
        known.push_back(book.key);
    }
    for (const godot::KeyValue<int, godot::HashMap<uint64_t, RowExplain>> &seen :
         verdicts) {
        bool counted = false;
        for (uint32_t at = 0; at < known.size(); ++at) {
            counted = counted || known[at] == seen.key;
        }
        if (!counted) {
            known.push_back(seen.key);
        }
    }
    return known;
}

void SessionSend::retain_row(
    int64_t p_route,
    uint8_t p_comp,
    const LocalVector<int> &p_recipients
) {
    const LocalVector<int> known = known_peers();
    for (uint32_t which = 0; which < known.size(); ++which) {
        bool kept = false;
        for (uint32_t at = 0; at < p_recipients.size(); ++at) {
            kept = kept || p_recipients[at] == known[which];
        }
        if (kept) {
            continue;
        }
        const wire::StreamFamily families[3] = {
            wire::StreamFamily::VOLATILE,
            wire::StreamFamily::RETAINED,
            wire::StreamFamily::WINDOW,
        };
        for (uint32_t family = 0; family < 3; ++family) {
            wire::StreamLane lane;
            lane.route = p_route;
            lane.ordinal = p_comp;
            lane.family = families[family];
            uint64_t token = 0;
            writers.close(known[which], lane, token);
            control.drop_token(known[which], token);
        }
    }
}

void SessionSend::retain(const LocalVector<int> &p_recipients) {
    const LocalVector<int> known = known_peers();
    LocalVector<int> forgotten;
    for (uint32_t which = 0; which < known.size(); ++which) {
        bool kept = false;
        for (uint32_t at = 0; at < p_recipients.size(); ++at) {
            kept = kept || p_recipients[at] == known[which];
        }
        if (!kept) {
            forgotten.push_back(known[which]);
        }
    }
    for (uint32_t at = 0; at < forgotten.size(); ++at) {
        owed.erase(forgotten[at]);
        verdicts.erase(forgotten[at]);
        link.forget(forgotten[at]);
        pass.forget(forgotten[at]);
        readers.forget_peer(forgotten[at]);
        writers.forget_peer(forgotten[at]);
        control.forget_peer(forgotten[at]);
    }
}

} // namespace netw::repl

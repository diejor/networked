#include "netw/repl/link_governor.hpp"

namespace netw::repl {

namespace {

uint32_t popcount(uint32_t p_bits) {
    uint32_t count = 0;
    while (p_bits != 0) {
        p_bits &= p_bits - 1;
        ++count;
    }
    return count;
}

} // namespace

void LinkGovernor::push(Peer &r_peer, bool p_lost) {
    r_peer.outcomes = (r_peer.outcomes << 1) | (p_lost ? 1u : 0u);
    if (r_peer.filled < WINDOW) {
        r_peer.filled += 1;
    }
}

bool LinkGovernor::slowed(const Peer &p_peer) {
    return p_peer.rtt_floor_ms >= 0.0
        && p_peer.rtt_ms > p_peer.rtt_floor_ms + RTT_EXCESS_MS;
}

bool LinkGovernor::lossy(const Peer &p_peer) {
    if (p_peer.filled < MIN_SAMPLES) {
        return false;
    }
    const uint32_t mask = p_peer.filled >= WINDOW
        ? 0xFFFFFFFFu
        : ((uint32_t(1) << p_peer.filled) - 1);
    const uint32_t lost = popcount(p_peer.outcomes & mask);
    return float(lost) / float(p_peer.filled) > LOSS_ENTER;
}

int64_t LinkGovernor::round_ms(const Peer &p_peer) {
    const int64_t measured = int64_t(p_peer.rtt_ms);
    return measured > ROUND_FLOOR_MS ? measured : ROUND_FLOOR_MS;
}

void LinkGovernor::decrease(Peer &r_peer, int64_t p_now_ms, bool p_lossy) {
    const float below = r_peer.share - STEP;
    const float halved = r_peer.share * 0.5f;
    r_peer.share = halved > LEAST ? halved : LEAST;
    if (p_lossy) {
        const bool edge_again = r_peer.delivered_total >= WINDOW
            && r_peer.edge_at_ms >= 0
            && p_now_ms - r_peer.edge_at_ms < CEILING_HOLD_MS
            && below + STEP > r_peer.edge_share;
        if (edge_again) {
            r_peer.ceiling = below > LEAST ? below : LEAST;
        }
        r_peer.edge_at_ms = p_now_ms;
        r_peer.edge_share = r_peer.share;
    }
    r_peer.decreased = true;
    r_peer.decreased_at_ms = p_now_ms;
    r_peer.stepped_at_ms = p_now_ms;
    r_peer.outcomes = 0;
    r_peer.filled = 0;
}

void LinkGovernor::recover(Peer &r_peer, int64_t p_now_ms) {
    if (r_peer.ceiling < 1.0f
        && p_now_ms - r_peer.edge_at_ms >= CEILING_HOLD_MS) {
        r_peer.ceiling = 1.0f;
    }
    if (r_peer.share >= r_peer.ceiling
        || p_now_ms - r_peer.stepped_at_ms < round_ms(r_peer)) {
        return;
    }
    if (r_peer.lost_at_ms > r_peer.stepped_at_ms) {
        r_peer.stepped_at_ms = p_now_ms;
        return;
    }
    const float raised = r_peer.share + STEP;
    r_peer.share = raised < r_peer.ceiling ? raised : r_peer.ceiling;
    r_peer.stepped_at_ms = p_now_ms;
}

void LinkGovernor::note_ack(
    int p_peer,
    uint32_t p_delivered,
    uint32_t p_lost,
    double p_rtt_ms,
    double p_jitter_ms,
    int64_t p_now_ms
) {
    Peer &book = peers[p_peer];
    for (uint32_t at = 0; at < p_lost; ++at) {
        push(book, true);
    }
    for (uint32_t at = 0; at < p_delivered; ++at) {
        push(book, false);
    }
    if (book.delivered_total < WINDOW) {
        book.delivered_total += p_delivered;
    }
    if (p_lost > 0) {
        book.lost_at_ms = p_now_ms;
    }
    book.rtt_ms = p_rtt_ms;
    book.jitter_ms = p_jitter_ms;
    if (p_rtt_ms > 0.0
        && (book.rtt_floor_ms < 0.0 || p_rtt_ms < book.rtt_floor_ms)) {
        book.rtt_floor_ms = p_rtt_ms;
    }
    const bool lost_too_much = lossy(book);
    if (lost_too_much || slowed(book)) {
        if (!book.decreased
            || p_now_ms - book.decreased_at_ms >= round_ms(book)) {
            decrease(book, p_now_ms, lost_too_much);
        }
        return;
    }
    recover(book, p_now_ms);
}

void LinkGovernor::note_backlog(
    int p_peer,
    int64_t p_buffered,
    int64_t p_now_ms
) {
    Peer &book = peers[p_peer];
    const bool growing = p_buffered > book.backlog;
    book.backlog = p_buffered;
    book.backlog_growth = growing ? book.backlog_growth + 1 : 0;
    if (book.backlog_growth < BACKLOG_GROWTH) {
        return;
    }
    book.backlog_growth = 0;
    push(book, true);
    book.lost_at_ms = p_now_ms;
    if (lossy(book)
        && (!book.decreased
            || p_now_ms - book.decreased_at_ms >= round_ms(book))) {
        decrease(book, p_now_ms, true);
    }
}

LinkGovernor::Mode LinkGovernor::mode(int p_peer) const {
    return share(p_peer) < 1.0f ? BAD : GOOD;
}

float LinkGovernor::share(int p_peer) const {
    const Peer *book = peers.getptr(p_peer);
    return book == nullptr ? 1.0f : book->share;
}

float LinkGovernor::loss(int p_peer) const {
    const Peer *book = peers.getptr(p_peer);
    if (book == nullptr || book->filled == 0) {
        return 0.0f;
    }
    const uint32_t mask = book->filled >= WINDOW
        ? 0xFFFFFFFFu
        : ((uint32_t(1) << book->filled) - 1);
    return float(popcount(book->outcomes & mask)) / float(book->filled);
}

double LinkGovernor::rtt_ms(int p_peer) const {
    const Peer *book = peers.getptr(p_peer);
    return book == nullptr ? 0.0 : book->rtt_ms;
}

double LinkGovernor::jitter_ms(int p_peer) const {
    const Peer *book = peers.getptr(p_peer);
    return book == nullptr ? 0.0 : book->jitter_ms;
}

int64_t LinkGovernor::budget_bits(int p_peer, int64_t p_full_bits) const {
    const float held = share(p_peer);
    if (held >= 1.0f) {
        return p_full_bits;
    }
    const int64_t floor = p_full_bits < FLOOR_BITS ? p_full_bits : FLOOR_BITS;
    const int64_t scaled = int64_t(double(p_full_bits) * double(held));
    return scaled > floor ? scaled : floor;
}

void LinkGovernor::forget(int p_peer) {
    peers.erase(p_peer);
}

void LinkGovernor::clear() {
    peers.clear();
}

} // namespace netw::repl

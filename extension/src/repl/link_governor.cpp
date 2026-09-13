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

bool LinkGovernor::strained(const Peer &p_peer) {
    if (p_peer.rtt_floor_ms >= 0.0
        && p_peer.rtt_ms > p_peer.rtt_floor_ms + RTT_EXCESS_MS) {
        return true;
    }
    if (p_peer.filled < MIN_SAMPLES) {
        return false;
    }
    const uint32_t mask = p_peer.filled >= WINDOW
        ? 0xFFFFFFFFu
        : ((uint32_t(1) << p_peer.filled) - 1);
    const uint32_t lost = popcount(p_peer.outcomes & mask);
    return float(lost) / float(p_peer.filled) > LOSS_ENTER;
}

void LinkGovernor::note_ack(
    int p_peer,
    uint32_t p_delivered,
    uint32_t p_lost,
    double p_rtt_ms,
    double p_jitter_ms,
    int64_t p_tick
) {
    Peer &book = peers[p_peer];
    for (uint32_t at = 0; at < p_lost; ++at) {
        push(book, true);
    }
    for (uint32_t at = 0; at < p_delivered; ++at) {
        push(book, false);
    }
    book.rtt_ms = p_rtt_ms;
    book.jitter_ms = p_jitter_ms;
    if (p_rtt_ms > 0.0
        && (book.rtt_floor_ms < 0.0 || p_rtt_ms < book.rtt_floor_ms)) {
        book.rtt_floor_ms = p_rtt_ms;
    }

    const bool under_strain = strained(book);
    if (book.mode == GOOD) {
        if (!under_strain) {
            return;
        }
        book.clean_needed
            = book.left_bad_at >= 0 && p_tick - book.left_bad_at < FLAP_TICKS
            ? (book.clean_needed * 2 < CLEAN_CAP ? book.clean_needed * 2
                                                 : CLEAN_CAP)
            : CLEAN_TICKS;
        book.mode = BAD;
        book.clean_since = -1;
        return;
    }
    if (under_strain) {
        book.clean_since = -1;
        return;
    }
    if (book.clean_since < 0) {
        book.clean_since = p_tick;
        return;
    }
    if (p_tick - book.clean_since >= book.clean_needed) {
        book.mode = GOOD;
        book.left_bad_at = p_tick;
        book.clean_since = -1;
    }
}

LinkGovernor::Mode LinkGovernor::mode(int p_peer) const {
    const Peer *book = peers.getptr(p_peer);
    return book == nullptr ? GOOD : book->mode;
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
    if (mode(p_peer) == GOOD) {
        return p_full_bits;
    }
    const int64_t floor = p_full_bits < FLOOR_BITS ? p_full_bits : FLOOR_BITS;
    const int64_t halved = p_full_bits / 2;
    return halved > floor ? halved : floor;
}

void LinkGovernor::forget(int p_peer) {
    peers.erase(p_peer);
}

void LinkGovernor::clear() {
    peers.clear();
}

} // namespace netw::repl

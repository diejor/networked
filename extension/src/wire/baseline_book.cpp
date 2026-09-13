#include "netw/wire/baseline_book.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::wire {

namespace {

bool seq_is_fresher(uint16_t a, uint16_t b) {
    return a != b && uint16_t(a - b) < 32768;
}

bool seq_at_or_before(uint16_t seq, uint16_t acked) {
    return seq == acked || seq_is_fresher(acked, seq);
}

bool seq_was_delivered(uint16_t seq, uint16_t acked, uint32_t history) {
    if (seq == acked) {
        return true;
    }
    const uint16_t back = uint16_t(acked - seq);
    if (back < 1 || back > BaselineBook::HISTORY_DEPTH) {
        return false;
    }
    return (history & (uint32_t(1) << (back - 1))) != 0;
}

bool contains(const LocalVector<int> &values, int value) {
    for (uint32_t index = 0; index < values.size(); ++index) {
        if (values[index] == value) {
            return true;
        }
    }
    return false;
}

} // namespace

uint64_t BaselineBook::mask_to_send(
    int peer,
    const WirePlan &plan,
    const CodeRow &row
) {
    NETW_WARN_COND_ONCE(
        !plan.valid(),
        sys::WIRE,
        "A baseline mask was asked for against an invalid plan, so peer %d "
        "is sent nothing.",
        peer
    );
    if (!plan.valid() || row.is_empty()) {
        return 0;
    }
    Peer &book = peers[peer];
    book.sticky |= book.has_confirmed
        ? CodeRow::changed_mask(plan, book.confirmed, row)
        : plan.full_mask();
    return book.sticky;
}

void BaselineBook::stage(int peer, uint16_t seq, const CodeRow &row) {
    Peer &book = peers[peer];
    if (book.in_flight.size() >= MAX_IN_FLIGHT) {
        for (uint32_t index = 1; index < book.in_flight.size(); ++index) {
            book.in_flight[index - 1].seq = book.in_flight[index].seq;
            book.in_flight[index - 1].row.copy_from(book.in_flight[index].row);
        }
    } else {
        book.in_flight.resize(book.in_flight.size() + 1);
    }
    Staged &entry = book.in_flight[book.in_flight.size() - 1];
    entry.seq = seq;
    entry.row.copy_from(row);
}

void BaselineBook::acknowledge(int peer, uint16_t acked_seq, uint32_t history) {
    NETW_ZONE_NC("BaselineBook acknowledge", colors::WIRE);
    Peer *book = peers.getptr(peer);
    if (book == nullptr || book->in_flight.is_empty()) {
        return;
    }
    bool found = false;
    uint16_t best = 0;
    for (uint32_t index = 0; index < book->in_flight.size(); ++index) {
        const uint16_t seq = book->in_flight[index].seq;
        if (!seq_was_delivered(seq, acked_seq, history)) {
            continue;
        }
        if (!found || seq_is_fresher(seq, best)) {
            best = seq;
            found = true;
        }
    }
    if (found) {
        for (uint32_t index = 0; index < book->in_flight.size(); ++index) {
            if (book->in_flight[index].seq == best) {
                book->confirmed.copy_from(book->in_flight[index].row);
                break;
            }
        }
        book->confirmed_seq = best;
        book->has_confirmed = true;
        book->sticky = 0;
    }
    uint32_t kept = 0;
    for (uint32_t index = 0; index < book->in_flight.size(); ++index) {
        if (seq_at_or_before(book->in_flight[index].seq, acked_seq)) {
            continue;
        }
        if (kept != index) {
            book->in_flight[kept].seq = book->in_flight[index].seq;
            book->in_flight[kept].row.copy_from(book->in_flight[index].row);
        }
        ++kept;
    }
    book->in_flight.resize(kept);
}

void BaselineBook::retain(const LocalVector<int> &recipients) {
    LocalVector<int> dropped;
    for (const KeyValue<int, Peer> &entry : peers) {
        if (!contains(recipients, entry.key)) {
            dropped.push_back(entry.key);
        }
    }
    for (uint32_t index = 0; index < dropped.size(); ++index) {
        peers.erase(dropped[index]);
    }
}

void BaselineBook::forget(int peer) {
    peers.erase(peer);
}

void BaselineBook::clear() {
    peers.clear();
}

BaselineBook::Baseline BaselineBook::baseline(int peer) const {
    Baseline out;
    const Peer *book = peers.getptr(peer);
    if (book == nullptr || !book->has_confirmed) {
        return out;
    }
    out.row = &book->confirmed;
    out.seq = book->confirmed_seq;
    return out;
}

bool BaselineBook::has_baseline(int peer) const {
    const Peer *book = peers.getptr(peer);
    return book != nullptr && book->has_confirmed;
}

uint32_t BaselineBook::in_flight_count(int peer) const {
    const Peer *book = peers.getptr(peer);
    return book == nullptr ? 0 : book->in_flight.size();
}

uint64_t BaselineBook::sticky_mask(int peer) const {
    const Peer *book = peers.getptr(peer);
    return book == nullptr ? 0 : book->sticky;
}

} // namespace netw::wire

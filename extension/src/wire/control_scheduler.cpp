#include "netw/wire/control_scheduler.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

using namespace godot;

namespace {

constexpr uint32_t ACCEPT_HEAD_BYTES = 2;

struct ByToken {
    bool operator()(
        const ControlAcceptEntry &p_a,
        const ControlAcceptEntry &p_b
    ) const {
        return p_a.token < p_b.token;
    }
};

bool same_lifecycle_subject(
    const ControlRecord &p_held,
    const ControlRecord &p_fresh
) {
    if (p_held.tag != p_fresh.tag) {
        return false;
    }
    if (p_fresh.tag == ControlTag::OPEN) {
        return p_held.route == p_fresh.route
            && p_held.ordinal == p_fresh.ordinal
            && p_held.family == p_fresh.family;
    }
    if (p_fresh.tag == ControlTag::CLOSE) {
        return p_held.token == p_fresh.token;
    }
    if (p_fresh.tag == ControlTag::RESET && p_fresh.request == 0) {
        return p_held.request == 0 && p_held.token == p_fresh.token;
    }
    return p_held.request == p_fresh.request;
}

} // namespace

uint32_t varuint_byte_span(uint64_t p_value) {
    uint32_t span = 1;
    while (p_value >= 0x80) {
        p_value >>= 7;
        span += 1;
    }
    return span;
}

uint32_t svarint_byte_span(int64_t p_value) {
    return varuint_byte_span(
        (uint64_t(p_value) << 1) ^ uint64_t(p_value >> 63)
    );
}

void ControlScheduler::accept(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    Peer &held = peers[p_peer];
    for (uint32_t at = 0; at < held.receipts.size(); ++at) {
        if (held.receipts[at].token != p_token) {
            continue;
        }
        if (p_revision > held.receipts[at].revision) {
            held.receipts[at].revision = p_revision;
        }
        return;
    }
    ControlAcceptEntry entry;
    entry.token = p_token;
    entry.revision = p_revision;
    held.receipts.push_back(entry);
}

void ControlScheduler::queue(int p_peer, const ControlRecord &p_record) {
    Peer &held = peers[p_peer];
    for (uint32_t at = 0; at < held.lifecycle.size(); ++at) {
        if (same_lifecycle_subject(held.lifecycle[at], p_record)) {
            held.lifecycle[at] = p_record;
            return;
        }
    }
    held.lifecycle.push_back(p_record);
}

void ControlScheduler::drop_token(int p_peer, uint64_t p_token) {
    Peer *held = peers.getptr(p_peer);
    if (held == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < held->receipts.size(); ++at) {
        if (held->receipts[at].token == p_token) {
            held->receipts.remove_at(at);
            break;
        }
    }
    for (uint32_t at = held->lifecycle.size(); at > 0; --at) {
        if (held->lifecycle[at - 1].token == p_token
            && held->lifecycle[at - 1].tag != ControlTag::OPEN) {
            held->lifecycle.remove_at(at - 1);
        }
    }
}

bool ControlScheduler::due(int p_peer, int64_t p_now_ms) const {
    const Peer *held = peers.getptr(p_peer);
    if (held == nullptr
        || (held->receipts.is_empty() && held->lifecycle.is_empty())) {
        return false;
    }
    if (!held->primed || !held->lifecycle.is_empty()) {
        return true;
    }
    return p_now_ms - held->flushed_at_ms >= CONTROL_FLUSH_PERIOD_MS;
}

LocalVector<int> ControlScheduler::ready_peers(int64_t p_now_ms) const {
    LocalVector<int> out;
    for (const KeyValue<int, Peer> &each : peers) {
        if (due(each.key, p_now_ms)) {
            out.push_back(each.key);
        }
    }
    return out;
}

LocalVector<ControlRecord> ControlScheduler::flush(
    int p_peer,
    int64_t p_now_ms,
    int64_t p_budget_bytes,
    PackedByteArray *r_frame
) {
    NETW_ZONE_NC("Control flush", colors::WIRE);
    LocalVector<ControlRecord> out;
    if (!due(p_peer, p_now_ms)) {
        return out;
    }
    Peer &held = peers[p_peer];
    const bool receipts_due = !held.primed
        || p_now_ms - held.flushed_at_ms >= CONTROL_FLUSH_PERIOD_MS;
    held.primed = true;
    if (receipts_due) {
        held.flushed_at_ms = p_now_ms;
    }

    ControlFrameWriter frame(held.sent);
    int64_t left = p_budget_bytes;
    uint32_t sent_lifecycle = 0;
    while (sent_lifecycle < held.lifecycle.size()) {
        const ControlRecord &next = held.lifecycle[sent_lifecycle];
        const int64_t cost = frame.cost(next);
        if (cost > left) {
            break;
        }
        if (cost > 0 && frame.append(next)) {
            left -= cost;
            out.push_back(next);
        }
        sent_lifecycle += 1;
    }
    for (uint32_t at = 0; at < sent_lifecycle; ++at) {
        held.lifecycle.remove_at(0);
    }

    if (receipts_due) {
        held.receipts.sort_custom<ByToken>();
    }
    uint32_t taken = 0;
    while (receipts_due && taken < held.receipts.size()) {
        ControlRecord record;
        record.tag = ControlTag::ACCEPT;
        uint32_t span = ACCEPT_HEAD_BYTES;
        uint32_t reaching = taken;
        uint64_t previous = 0;
        while (reaching < held.receipts.size()
               && record.receipts.size() < ACCEPT_MAX_ENTRIES) {
            const ControlAcceptEntry &entry = held.receipts[reaching];
            const uint32_t cost
                = svarint_byte_span(int64_t(entry.token - previous))
                + varuint_byte_span(entry.revision);
            if (span + cost > ACCEPT_MAX_PAYLOAD_BYTES) {
                break;
            }
            record.receipts.push_back(entry);
            previous = entry.token;
            span += cost;
            reaching += 1;
        }
        if (record.receipts.is_empty() || int64_t(span) > left
            || !frame.append(record)) {
            break;
        }
        left -= int64_t(span);
        out.push_back(record);
        taken = reaching;
    }
    for (uint32_t at = 0; at < taken; ++at) {
        held.receipts.remove_at(0);
    }
    if (r_frame != nullptr) {
        *r_frame = frame.bytes();
    }
    return out;
}

bool ControlScheduler::read(
    int p_peer,
    const PackedByteArray &p_frame,
    LocalVector<ControlRecord> &r_records
) {
    return read_control_frame(p_frame, peers[p_peer].heard, r_records);
}

void ControlScheduler::forget_sent_basis(int p_peer) {
    Peer *held = peers.getptr(p_peer);
    if (held != nullptr) {
        held->sent.clear();
    }
}

void ControlScheduler::forget_peer(int p_peer) {
    peers.erase(p_peer);
}

void ControlScheduler::clear() {
    peers.clear();
}

uint32_t ControlScheduler::pending_receipts(int p_peer) const {
    const Peer *held = peers.getptr(p_peer);
    return held == nullptr ? 0 : held->receipts.size();
}

uint32_t ControlScheduler::pending_lifecycle(int p_peer) const {
    const Peer *held = peers.getptr(p_peer);
    return held == nullptr ? 0 : held->lifecycle.size();
}

} // namespace netw::wire

#include "netw/wire/control_scheduler.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::wire {

using namespace godot;

namespace {

constexpr uint32_t ACCEPT_HEAD_BYTES = 2;

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

void ControlScheduler::accept(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    Peer &held = peers[p_peer];
    for (uint32_t at = 0; at < held.tokens.size(); ++at) {
        if (held.tokens[at] != p_token) {
            continue;
        }
        if (p_revision > held.revisions[at]) {
            held.revisions[at] = p_revision;
        }
        return;
    }
    held.tokens.push_back(p_token);
    held.revisions.push_back(p_revision);
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
    for (uint32_t at = 0; at < held->tokens.size(); ++at) {
        if (held->tokens[at] != p_token) {
            continue;
        }
        held->tokens.remove_at(at);
        held->revisions.remove_at(at);
        break;
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
        || (held->tokens.is_empty() && held->lifecycle.is_empty())) {
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
    int64_t p_budget_bytes
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

    int64_t left = p_budget_bytes;
    uint32_t sent_lifecycle = 0;
    while (sent_lifecycle < held.lifecycle.size()) {
        const ControlRecord &next = held.lifecycle[sent_lifecycle];
        const int64_t cost = int64_t(write_control_record(next).size());
        if (cost == 0) {
            sent_lifecycle += 1;
            continue;
        }
        if (cost > left) {
            break;
        }
        left -= cost;
        out.push_back(next);
        sent_lifecycle += 1;
    }
    for (uint32_t at = 0; at < sent_lifecycle; ++at) {
        held.lifecycle.remove_at(0);
    }

    uint32_t taken = 0;
    while (receipts_due && taken < held.tokens.size()) {
        ControlRecord record;
        record.tag = ControlTag::ACCEPT;
        uint32_t span = ACCEPT_HEAD_BYTES;
        uint32_t reaching = taken;
        while (reaching < held.tokens.size()
               && record.receipts.size() < ACCEPT_MAX_ENTRIES) {
            const uint32_t cost = varuint_byte_span(held.tokens[reaching])
                + varuint_byte_span(held.revisions[reaching]);
            if (span + cost > ACCEPT_MAX_PAYLOAD_BYTES) {
                break;
            }
            ControlAcceptEntry entry;
            entry.token = held.tokens[reaching];
            entry.revision = held.revisions[reaching];
            record.receipts.push_back(entry);
            span += cost;
            reaching += 1;
        }
        if (record.receipts.is_empty() || int64_t(span) > left) {
            break;
        }
        left -= int64_t(span);
        out.push_back(record);
        taken = reaching;
    }
    for (uint32_t at = 0; at < taken; ++at) {
        held.tokens.remove_at(0);
        held.revisions.remove_at(0);
    }
    return out;
}

void ControlScheduler::forget_peer(int p_peer) {
    peers.erase(p_peer);
}

void ControlScheduler::clear() {
    peers.clear();
}

uint32_t ControlScheduler::pending_receipts(int p_peer) const {
    const Peer *held = peers.getptr(p_peer);
    return held == nullptr ? 0 : held->tokens.size();
}

uint32_t ControlScheduler::pending_lifecycle(int p_peer) const {
    const Peer *held = peers.getptr(p_peer);
    return held == nullptr ? 0 : held->lifecycle.size();
}

} // namespace netw::wire

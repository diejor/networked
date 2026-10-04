#include "netw/wire/profiler.hpp"

#if defined(DEBUG_ENABLED)

#include <algorithm>

namespace netw::wire {

using namespace godot;

namespace {

uint32_t mix(uint64_t p_value) {
    p_value ^= p_value >> 33;
    p_value *= UINT64_C(0xff51afd7ed558ccd);
    p_value ^= p_value >> 33;
    return uint32_t(p_value ^ (p_value >> 32));
}

} // namespace

bool ProfileKey::operator==(const ProfileKey &p_other) const {
    return route == p_other.route && owner == p_other.owner
        && version == p_other.version && channel == p_other.channel
        && member == p_other.member;
}

uint32_t ProfileKeyHasher::hash(const ProfileKey &p_key) {
    return mix(uint64_t(p_key.route)) ^ mix(p_key.owner)
        ^ mix(uint64_t(p_key.version) << 16 | p_key.channel)
        ^ p_key.member.hash();
}

bool ReplyKey::operator==(const ReplyKey &p_other) const {
    return peer == p_other.peer && transaction == p_other.transaction
        && sending == p_other.sending;
}

uint32_t ReplyKeyHasher::hash(const ReplyKey &p_key) {
    return mix(uint64_t(p_key.peer)) ^ mix(uint64_t(p_key.transaction))
        ^ uint32_t(p_key.sending);
}

String NetworkProfiler::channel_words(
    const ChannelDecl *p_channel,
    int64_t p_id
) {
    if (p_channel == nullptr) {
        return String("channel ") + String::num_int64(p_id);
    }
    return String(p_channel->name).to_lower().replace("_", " ");
}

const NetworkProfiler::Row *NetworkProfiler::row(uint32_t p_subject) const {
    return p_subject < rows.size() ? &rows[p_subject] : nullptr;
}

uint32_t NetworkProfiler::append(const String &p_label) {
    Row made;
    made.identity.instantiate();
    made.label = p_label;
    const uint32_t result = rows.size();
    rows.push_back(made);
    return result;
}

void NetworkProfiler::reset_interval() {
    for (uint32_t at : dirty) {
        Row &held = rows[at];
        held.incoming_count = 0;
        held.incoming_bytes = 0;
        held.outgoing_count = 0;
        held.outgoing_bytes = 0;
        held.dirty = false;
    }
    dirty.clear();
}

void NetworkProfiler::activate(bool p_rpc, bool p_sync) {
    if (rpc_enabled == p_rpc && sync_enabled == p_sync) {
        return;
    }
    reset_interval();
    for (ReplySlot &slot : replies) {
        slot.state = 0;
    }
    reply_count = 0;
    generation += 1;
    rpc_enabled = p_rpc;
    sync_enabled = p_sync;
}

void NetworkProfiler::prepare(const WireRegistry &p_channels) {
    if (initialized || !active()) {
        return;
    }
    rows.reserve(SUBJECT_LIMIT + 258);
    dirty.reserve(SUBJECT_LIMIT + 258);
    replies.resize(SUBJECT_LIMIT * 2);
    for (uint32_t channel = 0; channel < 256; ++channel) {
        const ChannelDecl *decl = p_channels.find_channel(uint8_t(channel));
        fallback[channel]
            = append(String("(other) ") + channel_words(decl, channel));
    }
    overhead = append("(headers)");
    ack = append("(acks)");
    initialized = true;
}

uint32_t NetworkProfiler::aggregate(uint8_t p_channel) const {
    return initialized && rpc_enabled ? fallback[p_channel] : NONE;
}

bool NetworkProfiler::watch_node(uint64_t p_node) {
    if (p_node == 0 || watched_nodes.has(p_node)
        || watched_nodes.size() >= SUBJECT_LIMIT) {
        return false;
    }
    watched_nodes.insert(p_node);
    return true;
}

void NetworkProfiler::invalidate_node(uint64_t p_node) {
    if (watched_nodes.has(p_node)) {
        path_changes += 1;
    }
}

uint32_t NetworkProfiler::find(const ProfileKey &p_key) const {
    const uint32_t *subject = subjects.getptr(p_key);
    return subject != nullptr ? *subject : NONE;
}

uint32_t NetworkProfiler::admit(
    const ProfileKey &p_key,
    const String &p_label
) {
    if (!rpc_enabled) {
        return NONE;
    }
    const uint32_t found = find(p_key);
    if (found != NONE) {
        return found;
    }
    if (detailed >= SUBJECT_LIMIT) {
        return aggregate(uint8_t(p_key.channel));
    }
    const uint32_t result = append(p_label);
    subjects.insert(p_key, result);
    detailed += 1;
    return result;
}

uint32_t NetworkProfiler::synchronizer(
    int64_t p_route,
    ObjectID p_sync,
    ObjectID p_root,
    ObjectID p_config
) {
    if (!sync_enabled || !gd::object_registered(p_sync)
        || !gd::object_registered(p_root)) {
        return NONE;
    }
    ProfileKey key;
    key.route = p_route;
    key.owner = uint64_t(p_sync);
    key.channel = 256;
    const uint32_t found = find(key);
    if (found != NONE) {
        rows[found].config = p_config;
        return found;
    }
    if (detailed >= SUBJECT_LIMIT) {
        return aggregate(uint8_t(builtin_channel("SYNC")));
    }
    Row made;
    made.sync = p_sync;
    made.root = p_root;
    made.config = p_config;
    const uint32_t result = rows.size();
    rows.push_back(made);
    subjects.insert(key, result);
    detailed += 1;
    return result;
}

void NetworkProfiler::observe(
    uint32_t p_subject,
    uint32_t p_epoch,
    int64_t p_peer,
    bool p_incoming,
    int64_t p_bytes
) {
    if (!active() || p_epoch != generation || p_subject >= rows.size()
        || p_peer < 0 || p_bytes < 0) {
        return;
    }
    Row &held = rows[p_subject];
    if (held.sync.is_valid() ? !sync_enabled : !rpc_enabled) {
        return;
    }
    if (!held.dirty) {
        dirty.push_back(p_subject);
        held.dirty = true;
    }
    if (p_incoming) {
        held.incoming_count += 1;
        held.incoming_bytes += p_bytes;
    } else {
        held.outgoing_count += 1;
        held.outgoing_bytes += p_bytes;
    }
}

NetworkProfiler::Arrival NetworkProfiler::begin_arrival(
    bool p_counted,
    int64_t p_peer,
    int64_t p_bytes,
    uint32_t p_subject
) {
    const Arrival previous = arrival;
    arrival = Arrival();
    arrival.subject = p_subject;
    arrival.epoch = generation;
    arrival.peer = p_peer;
    arrival.bytes = p_bytes;
    arrival.open = p_counted && active();
    return previous;
}

void NetworkProfiler::name_arrival(uint32_t p_subject) {
    if (arrival.open) {
        arrival.subject = p_subject;
    }
}

void NetworkProfiler::finish_arrival(const Arrival &p_previous) {
    if (arrival.open) {
        observe(
            arrival.subject,
            arrival.epoch,
            arrival.peer,
            true,
            arrival.bytes
        );
    }
    arrival = p_previous;
}

uint32_t NetworkProfiler::reply_slot(const ReplyKey &p_key) const {
    if (replies.is_empty()) {
        return NONE;
    }
    const uint32_t mask = replies.size() - 1;
    uint32_t at = ReplyKeyHasher::hash(p_key) & mask;
    for (uint32_t count = 0; count < replies.size(); ++count) {
        const ReplySlot &slot = replies[at];
        if (slot.state == 0) {
            return NONE;
        }
        if (slot.state == 1 && slot.key == p_key) {
            return at;
        }
        at = (at + 1) & mask;
    }
    return NONE;
}

void NetworkProfiler::erase_reply(uint32_t p_at) {
    const uint32_t mask = replies.size() - 1;
    uint32_t gap = p_at;
    uint32_t at = (gap + 1) & mask;
    while (replies[at].state == 1) {
        const uint32_t home = ReplyKeyHasher::hash(replies[at].key) & mask;
        if (((at - home) & mask) >= ((gap - home) & mask)) {
            replies[gap] = replies[at];
            gap = at;
        }
        at = (at + 1) & mask;
    }
    replies[gap].state = 0;
    reply_count -= 1;
}

void NetworkProfiler::remember_reply(
    int64_t p_peer,
    int64_t p_transaction,
    bool p_sending,
    uint32_t p_call
) {
    if (!rpc_enabled || p_transaction <= 0 || p_call >= rows.size()
        || reply_count >= SUBJECT_LIMIT || replies.is_empty()) {
        return;
    }
    const ReplyKey key{p_peer, p_transaction, p_sending};
    const uint32_t existing = reply_slot(key);
    if (existing != NONE) {
        replies[existing].subject = p_call;
        return;
    }
    const uint32_t mask = replies.size() - 1;
    uint32_t at = ReplyKeyHasher::hash(key) & mask;
    for (uint32_t count = 0; count < replies.size(); ++count) {
        ReplySlot &slot = replies[at];
        if (slot.state != 1) {
            slot.key = key;
            slot.subject = p_call;
            slot.state = 1;
            reply_count += 1;
            return;
        }
        at = (at + 1) & mask;
    }
}

uint32_t NetworkProfiler::take_reply(
    int64_t p_peer,
    int64_t p_transaction,
    bool p_sending
) {
    const ReplyKey key{p_peer, p_transaction, p_sending};
    const uint32_t at = reply_slot(key);
    if (!rpc_enabled || at == NONE) {
        return aggregate(uint8_t(builtin_channel("REPLY")));
    }
    const uint32_t parent = replies[at].subject;
    erase_reply(at);
    if (parent < 258) {
        return aggregate(uint8_t(builtin_channel("REPLY")));
    }
    const uint32_t *existing = reply_rows.getptr(parent);
    if (existing != nullptr) {
        return *existing;
    }
    if (detailed >= SUBJECT_LIMIT) {
        return aggregate(uint8_t(builtin_channel("REPLY")));
    }
    const uint32_t result = append(rows[parent].label + String(" reply"));
    reply_rows.insert(parent, result);
    detailed += 1;
    return result;
}

void NetworkProfiler::forget_transaction(int64_t p_transaction) {
    for (uint32_t at = 0; at < replies.size();) {
        const ReplySlot &slot = replies[at];
        if (slot.state == 1 && !slot.key.sending
            && slot.key.transaction == p_transaction) {
            erase_reply(at);
        } else {
            at += 1;
        }
    }
}

void NetworkProfiler::forget_peer(int64_t p_peer) {
    for (uint32_t at = 0; at < replies.size();) {
        const ReplySlot &slot = replies[at];
        if (slot.state == 1 && slot.key.peer == p_peer) {
            erase_reply(at);
        } else {
            at += 1;
        }
    }
}

void NetworkProfiler::collect(Array &r_rpc, Array &r_sync) {
    r_rpc.clear();
    r_sync.clear();
    r_rpc.push_back(0);
    r_sync.push_back(0);
    const LocalVector<Row> &held_rows = rows;
    std::sort(
        dirty.ptr(),
        dirty.ptr() + dirty.size(),
        [&held_rows](uint32_t p_left, uint32_t p_right) {
            return held_rows[p_left].label < held_rows[p_right].label;
        }
    );
    for (uint32_t at : dirty) {
        const Row &held = rows[at];
        if (held.sync.is_valid()) {
            r_sync.push_back(int64_t(uint64_t(held.sync)));
            r_sync.push_back(int64_t(uint64_t(held.config)));
            r_sync.push_back(int64_t(uint64_t(held.root)));
        } else {
            r_rpc.push_back(int64_t(held.identity->get_instance_id()));
            r_rpc.push_back(held.label);
        }
        Array &target = held.sync.is_valid() ? r_sync : r_rpc;
        target.push_back(held.incoming_count);
        target.push_back(held.incoming_bytes);
        target.push_back(held.outgoing_count);
        target.push_back(held.outgoing_bytes);
    }
    r_rpc[0] = r_rpc.size() - 1;
    r_sync[0] = r_sync.size() - 1;
    reset_interval();
}

} // namespace netw::wire

#endif

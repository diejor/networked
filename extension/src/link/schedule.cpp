#include "netw/link/schedule.hpp"

#include <algorithm>

#include "netw/api/loopback.hpp"

using namespace godot;

namespace netw::link {

namespace {

String stream_key(int p_sender_id, const char *p_stream) {
    return String::num_int64(p_sender_id) + String(":") + String(p_stream);
}

String channel_key(int p_sender_id, int p_channel) {
    return String::num_int64(p_sender_id) + String(":")
        + String::num_int64(p_channel);
}

String sender_prefix(int p_sender_id) {
    return String::num_int64(p_sender_id) + String(":");
}

Ref<RandomNumberGenerator> rng_for(
    Draws &p_state,
    int p_sender_id,
    const char *p_stream,
    int64_t p_seed,
    bool p_reliable = false
) {
    const String key = stream_key(p_sender_id, p_stream)
        + (p_reliable ? String(":reliable") : String());
    const Ref<RandomNumberGenerator> *found = p_state.rng_by_stream.getptr(key);
    if (found != nullptr) {
        return *found;
    }
    Ref<RandomNumberGenerator> rng;
    rng.instantiate();
    rng->set_seed(
        uint64_t(p_seed ^ int64_t(uint32_t(String(p_stream).hash())))
    );
    p_state.rng_by_stream.insert(key, rng);
    return rng;
}

bool roll(
    Draws &p_state,
    int p_sender_id,
    const char *p_stream,
    int64_t p_seed,
    double p_probability,
    bool p_reliable = false
) {
    if (p_probability <= 0.0) {
        return false;
    }
    if (p_probability >= 1.0) {
        return true;
    }
    return rng_for(p_state, p_sender_id, p_stream, p_seed, p_reliable)->randf()
        < p_probability;
}

double draw_jitter(
    Draws &p_state,
    int p_sender_id,
    const NetwLinkConditions &p_conditions
) {
    if (p_conditions.get_jitter_ms() <= 0.0) {
        return 0.0;
    }
    const Ref<RandomNumberGenerator> rng
        = rng_for(p_state, p_sender_id, "jitter", p_conditions.get_seed());
    return rng->randf_range(0.0, p_conditions.get_jitter_ms());
}

double stall_until(
    Draws &p_state,
    const NetwLinkConditions &p_conditions,
    Send p_send,
    double p_now_ms,
    double p_frame_ms
) {
    if (p_state.stall_until_ms <= p_now_ms
        && roll(
            p_state,
            p_send.sender,
            "throttle",
            p_conditions.get_seed(),
            p_conditions.get_stall_chance(),
            p_send.reliable
        )) {
        p_state.stall_until_ms
            = p_now_ms + std::max(p_frame_ms, p_conditions.get_stall_ms());
    }
    return std::max(p_now_ms, p_state.stall_until_ms);
}

} // namespace

Arrival schedule(
    Draws &p_state,
    const NetwLinkConditions &p_conditions,
    Send p_send,
    double p_now_ms,
    double p_frame_ms
) {
    if (p_send.reliable) {
        const int sender_id = p_send.sender;
        double due_ms = p_now_ms + p_conditions.get_latency_ms();
        if (roll(
                p_state,
                sender_id,
                "loss",
                p_conditions.get_seed(),
                p_conditions.get_loss_chance()
            )) {
            due_ms += std::max(30.0, 2.0 * p_conditions.get_latency_ms());
        }
        due_ms = std::max(
            due_ms,
            stall_until(p_state, p_conditions, p_send, p_now_ms, p_frame_ms)
        );
        const String key = channel_key(sender_id, p_send.channel);
        const double *last_due = p_state.reliable_due_by_channel.getptr(key);
        if (last_due != nullptr) {
            due_ms = std::max(due_ms, *last_due);
        }
        p_state.reliable_due_by_channel.insert(key, due_ms);
        return {false, due_ms};
    }
    const int sender_id = p_send.sender;
    const int64_t seed = p_conditions.get_seed();
    if (roll(
            p_state,
            sender_id,
            "loss",
            seed,
            p_conditions.get_loss_chance()
        )) {
        return {true};
    }

    double due_ms = p_now_ms + p_conditions.get_latency_ms();
    due_ms += draw_jitter(p_state, sender_id, p_conditions);
    if (roll(
            p_state,
            sender_id,
            "reorder",
            seed,
            p_conditions.get_reorder_chance()
        )) {
        due_ms += p_frame_ms;
    }
    due_ms = std::max(
        due_ms,
        stall_until(p_state, p_conditions, p_send, p_now_ms, p_frame_ms)
    );

    Arrival arrival;
    arrival.due_ms = due_ms;
    if (roll(
            p_state,
            sender_id,
            "duplicate",
            seed,
            p_conditions.get_duplicate_chance()
        )) {
        arrival.duplicate_due_ms = due_ms + p_frame_ms;
    }
    return arrival;
}

bool accept_ordered(
    HashMap<String, uint64_t> &p_delivered,
    int p_sender,
    int p_channel,
    uint64_t p_sequence
) {
    const String key = channel_key(p_sender, p_channel);
    const uint64_t *last = p_delivered.getptr(key);
    if (last != nullptr && p_sequence <= *last) {
        return false;
    }
    p_delivered.insert(key, p_sequence);
    return true;
}

void forget_ordered_sender(
    HashMap<String, uint64_t> &p_delivered,
    int p_sender
) {
    const String prefix = sender_prefix(p_sender);
    Vector<String> keys;
    for (const KeyValue<String, uint64_t> &entry : p_delivered) {
        if (entry.key.begins_with(prefix)) {
            keys.push_back(entry.key);
        }
    }
    for (const String &key : keys) {
        p_delivered.erase(key);
    }
}

void forget_sender(Draws &p_state, int p_sender_id) {
    const String prefix = sender_prefix(p_sender_id);

    Vector<String> stream_keys;
    for (const KeyValue<String, Ref<RandomNumberGenerator>> &stream :
         p_state.rng_by_stream) {
        if (stream.key.begins_with(prefix)) {
            stream_keys.push_back(stream.key);
        }
    }
    for (const String &key : stream_keys) {
        p_state.rng_by_stream.erase(key);
    }

    Vector<String> channel_keys;
    for (const KeyValue<String, double> &channel :
         p_state.reliable_due_by_channel) {
        if (channel.key.begins_with(prefix)) {
            channel_keys.push_back(channel.key);
        }
    }
    for (const String &key : channel_keys) {
        p_state.reliable_due_by_channel.erase(key);
    }
}

void prune_reliable_due(Draws &p_state, double p_now_ms) {
    Vector<String> expired;
    for (const KeyValue<String, double> &channel :
         p_state.reliable_due_by_channel) {
        if (channel.value <= p_now_ms) {
            expired.push_back(channel.key);
        }
    }
    for (const String &key : expired) {
        p_state.reliable_due_by_channel.erase(key);
    }
}

} // namespace netw::link

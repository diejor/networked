#pragma once

#if defined(DEBUG_ENABLED)

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "netw/wire/registry.hpp"

namespace netw::wire {

struct ProfileKey {
    int64_t route = 0;
    uint64_t owner = 0;
    uint32_t version = 0;
    uint16_t channel = 0;
    godot::StringName member;

    bool operator==(const ProfileKey &p_other) const;
};

struct ProfileKeyHasher {
    static uint32_t hash(const ProfileKey &p_key);
};

struct ReplyKey {
    int64_t peer = 0;
    int64_t transaction = 0;
    bool sending = false;

    bool operator==(const ReplyKey &p_other) const;
};

struct ReplyKeyHasher {
    static uint32_t hash(const ReplyKey &p_key);
};

class NetworkProfiler {
public:
    static constexpr uint32_t NONE = UINT32_MAX;
    static constexpr uint32_t SKIP = UINT32_MAX - 1;
    static constexpr uint32_t SUBJECT_LIMIT = 8192;
    static constexpr uint16_t SET_CHANNEL = 257;

    static godot::String channel_words(
        const ChannelDecl *p_channel,
        int64_t p_id
    );

    struct Row {
        godot::Ref<godot::RefCounted> identity;
        godot::String label;
        godot::ObjectID sync;
        godot::ObjectID root;
        godot::ObjectID config;
        int64_t incoming_count = 0;
        int64_t incoming_bytes = 0;
        int64_t outgoing_count = 0;
        int64_t outgoing_bytes = 0;
        bool dirty = false;
    };

    struct Arrival {
        uint32_t subject = NONE;
        uint32_t epoch = 0;
        int64_t peer = 0;
        int64_t bytes = 0;
        bool open = false;
    };

private:
    godot::LocalVector<Row> rows;
    godot::LocalVector<uint32_t> dirty;
    godot::HashMap<ProfileKey, uint32_t, ProfileKeyHasher> subjects;
    godot::HashSet<uint64_t> watched_nodes;
    struct ReplySlot {
        ReplyKey key;
        uint32_t subject = NONE;
        uint8_t state = 0;
    };

    godot::LocalVector<ReplySlot> replies;
    uint32_t reply_count = 0;
    uint32_t path_changes = 0;
    godot::HashMap<uint32_t, uint32_t> reply_rows;
    uint32_t fallback[256] = {};
    uint32_t overhead = NONE;
    uint32_t ack = NONE;
    uint32_t detailed = 0;
    uint32_t generation = 1;
    bool initialized = false;
    bool rpc_enabled = false;
    bool sync_enabled = false;
    Arrival arrival;

    uint32_t append(const godot::String &p_label);
    void reset_interval();
    uint32_t reply_slot(const ReplyKey &p_key) const;
    void erase_reply(uint32_t p_at);

public:
    bool active() const {
        return rpc_enabled || sync_enabled;
    }
    bool records_rpc() const {
        return rpc_enabled;
    }
    bool records_sync() const {
        return sync_enabled;
    }
    uint32_t epoch() const {
        return generation;
    }
    uint32_t subject_count() const {
        return detailed;
    }
    uint32_t row_count() const {
        return rows.size();
    }
    const Row *row(uint32_t p_subject) const;
    void activate(bool p_rpc, bool p_sync);
    void prepare(const WireRegistry &p_channels);
    uint32_t aggregate(uint8_t p_channel) const;
    uint32_t overhead_subject() const {
        return overhead;
    }
    uint32_t ack_subject() const {
        return ack;
    }
    bool watch_node(uint64_t p_node);
    void invalidate_node(uint64_t p_node);
    uint32_t path_revision() const {
        return path_changes;
    }
    uint32_t find(const ProfileKey &p_key) const;
    uint32_t admit(
        const ProfileKey &p_key,
        const godot::String &p_label
    );
    uint32_t synchronizer(
        int64_t p_route,
        godot::ObjectID p_sync,
        godot::ObjectID p_root,
        godot::ObjectID p_config
    );
    void observe(
        uint32_t p_subject,
        uint32_t p_epoch,
        int64_t p_peer,
        bool p_incoming,
        int64_t p_bytes
    );
    Arrival begin_arrival(
        bool p_counted,
        int64_t p_peer,
        int64_t p_bytes,
        uint32_t p_subject
    );
    void name_arrival(uint32_t p_subject);
    void finish_arrival(const Arrival &p_previous);
    void remember_reply(
        int64_t p_peer,
        int64_t p_transaction,
        bool p_sending,
        uint32_t p_call
    );
    uint32_t take_reply(
        int64_t p_peer,
        int64_t p_transaction,
        bool p_sending
    );
    void forget_transaction(int64_t p_transaction);
    void forget_peer(int64_t p_peer);
    void collect(godot::Array &r_rpc, godot::Array &r_sync);
};

class ProfileSubjectScope {
    uint32_t &subject;
    uint32_t previous;

public:
    ProfileSubjectScope(uint32_t &p_subject, uint32_t p_value)
        : subject(p_subject), previous(p_subject) {
        subject = p_value;
    }
    ~ProfileSubjectScope() {
        subject = previous;
    }
};

class ProfileArrivalScope {
    NetworkProfiler *profiler;
    NetworkProfiler::Arrival previous;

public:
    ProfileArrivalScope(
        NetworkProfiler *p_profiler,
        bool p_counted,
        int64_t p_peer,
        int64_t p_bytes,
        uint32_t p_subject
    ) : profiler(p_profiler) {
        if (profiler != nullptr) {
            previous = profiler->begin_arrival(
                p_counted, p_peer, p_bytes, p_subject
            );
        }
    }
    ProfileArrivalScope(
        NetworkProfiler &p_profiler,
        bool p_counted,
        int64_t p_peer,
        int64_t p_bytes,
        uint32_t p_subject
    ) : ProfileArrivalScope(
            &p_profiler, p_counted, p_peer, p_bytes, p_subject
        ) {
    }
    ~ProfileArrivalScope() {
        if (profiler != nullptr) {
            profiler->finish_arrival(previous);
        }
    }
};

} // namespace netw::wire

#define NETW_NETWORK_PROFILE(...) __VA_ARGS__
#else
#define NETW_NETWORK_PROFILE(...) ((void)0)
#endif

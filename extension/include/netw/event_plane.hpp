#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwEvent : public godot::RefCounted {
    GDCLASS(NetwEvent, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    int64_t event = 0;
    int64_t phase = 0;
    int64_t tick = 0;
    int64_t route = 0;
    godot::StringName entity_id;
    int64_t peer = 0;
    int64_t verdict = 0;
    godot::Dictionary detail;
    godot::Dictionary model;

    int64_t get_event() const {
        return event;
    }
    int64_t get_phase() const {
        return phase;
    }
    int64_t get_tick() const {
        return tick;
    }
    int64_t get_route() const {
        return route;
    }
    godot::StringName get_entity_id() const {
        return entity_id;
    }
    int64_t get_peer() const {
        return peer;
    }
    int64_t get_verdict() const {
        return verdict;
    }
    godot::Dictionary get_detail() const {
        return detail;
    }
    godot::Dictionary get_model() const {
        return model;
    }
    godot::String get_event_name() const;
};

class EventPlane {
public:
    enum Event : int64_t {
        SPAWNING = 0,
        SPAWNED = 1,
        DESPAWNING = 2,
        DESPAWNED = 3,
        REPARENTED = 4,
        STAGE_TRANSITION = 5,
        CONTROL_CHANGED = 6,
        CONTROL_REQUESTED = 7,

        INTEREST_ENTER = 16,
        INTEREST_EXIT = 17,
        OBSERVER_ENTERED = 18,
        OBSERVER_LEFT = 19,
        INTEREST_COMMIT = 20,

        EPISODE_OPEN = 32,
        EPISODE_CLOSE = 33,
        EPISODE_FALLBACK = 34,
        PREDICT_DRIVE = 35,
        PREDICT_CONSUME = 36,
        PREDICT_EVALUATE = 37,
        PREDICT_RECOVER = 38,
        DIVERGENCE = 39,
        RECOVERY = 40,

        GATE_SYNC = 48,
        GATE_SPAWN = 49,
        GATE_TABLE = 50,
        GATE_PREDICT = 51,

        SYNC_ENCODE = 64,
        SYNC_DECODE = 65,
        GATHER = 66,
        APPLY = 67,
        SPAWN_DECLARE = 68,
        SPAWN_RECONCILE = 69,
        SPAWN_CONSTRUCT = 70,
        DISPLAY_RECORD = 71,
        DISPLAY_PUMP = 72,
        DISPLAY_WRITE = 73,
        TABLE_COMMIT = 74,

        VERDICT = 96,
        SEAM_MISUSE = 97,

        SESSION_STATE = 112,
        PEER_JOINED = 113,
        PEER_LEFT = 114,
        PEER_AUTH_FAILED = 115,
        SCENE_LIVE = 116,

        DATAGRAM_SENT = 128,
        DATAGRAM_RECEIVED = 129,
        DATAGRAM_MALFORMED = 130,
        ACK_ADVANCED = 131,
    };

    enum Phase : int64_t {
        BEFORE = 0,
        AFTER = 1,
    };

    static constexpr int64_t EVENT_LIMIT = 144;
    static constexpr int RING_CAPACITY = 64;

    struct Emission {
        int64_t event = 0;
        int64_t phase = AFTER;
        int64_t tick = 0;
        int64_t route = 0;
        godot::StringName entity_id;
        int64_t peer = 0;
        int64_t verdict = 0;
        godot::Dictionary detail;
        godot::Dictionary model;

        Emission() = default;
        Emission(int64_t p_event, int64_t p_phase, int64_t p_tick)
            : event(p_event), phase(p_phase), tick(p_tick) {
        }
    };

    static bool is_event(int64_t event);
    static const char *group_of(int64_t event);
    static const char *name_of(int64_t event);
    static bool is_terminal(int64_t event);
    static int taxonomy_size();
    static int64_t value_at(int index);

    int64_t watch(
        const godot::PackedInt64Array &events,
        const godot::Dictionary &target,
        const godot::Dictionary &predicate,
        const godot::Callable &sink,
        const godot::Dictionary &opts
    );
    bool unwatch(int64_t id);
    godot::Array watches() const;

    bool wants(int64_t event, int64_t route) const;
    void emit(const Emission &fact);
    void stage(const Emission &fact);
    void drain_staged();

    godot::Array ring(int64_t route);
    void ring_clear(int64_t route);

    void set_armed(bool enabled);
    bool is_armed() const;

    void clear();

private:
    struct Watch {
        int64_t id = 0;
        godot::LocalVector<int64_t> events;
        int64_t phase = AFTER;
        int64_t route = 0;
        godot::StringName entity_id;
        int64_t peer = 0;
        godot::Dictionary predicate;
        godot::Callable sink;
        bool enabled = true;
        bool once = false;
        bool dedupe = false;
        godot::String note;
        int64_t hit_count = 0;
        godot::HashMap<int64_t, godot::HashSet<int64_t>> claimed;
    };

    struct Ring {
        godot::LocalVector<Emission> rows;
        int head = 0;
    };

    godot::LocalVector<Watch> rows;
    godot::HashMap<int64_t, Ring> rings;
    godot::HashMap<int64_t, godot::Dictionary> snapshots;
    godot::HashMap<int64_t, godot::StringName> identities;
    int64_t next_id = 1;

    // Read from a lane thread while the main thread writes them.
    std::atomic_bool armed = { false };
    std::atomic<int32_t> hot[EVENT_LIMIT] = {};

    mutable std::mutex staged_lock;
    godot::LocalVector<Emission> staged;

    void rebuild_hot();
    bool matches(const Watch &row, const Emission &fact) const;
    void deliver(const Emission &fact);
    void record(int64_t route, const Emission &fact);
};

} // namespace netw

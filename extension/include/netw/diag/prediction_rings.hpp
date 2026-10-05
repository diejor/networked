#pragma once

#if defined(DEBUG_ENABLED)

#include <cstdint>
#include <memory>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/diag/sketch.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;
class NetwPredictionEngine;

namespace predict {
struct Wiring;
} // namespace predict

namespace diag {

struct RingSource {
    int64_t key = 0;
    NetwEntity *entity = nullptr;
    int64_t slot = -1;
    godot::Node *owner = nullptr;
};

struct StateLine {
    enum State {
        NONE = -1,
        OPEN = 0,
        CLOSED = 1,
        FALLBACK = 2,
        QUARANTINE = 3,
        DEMOTED = 4,
    };

    int state = NONE;
    int op = 0;
    int outcome = -1;
    bool flat = false;
    bool replica = false;
    int64_t gap_hundredths = -1;
    int64_t ack = -1;
    int64_t age_hundredths = -1;
    int64_t drift_hundredths = -1;

    bool wanted(int p_role) const;
    bool operator==(const StateLine &p_other) const;
    godot::String text() const;
};

class PredictionRings {
public:
    static constexpr uint32_t ENTITY_CAP = 64;
    static constexpr double FRESH_SECONDS = 1.0;
    static constexpr float STALE_ALPHA = 0.25f;

    static bool wanted();

    PredictionRings();

    godot::LocalVector<RingSource> sources;

    void attach_sketch();
    void poll(NetwMultiplayer &p_session);
    void sample(NetwMultiplayer &p_session);
    const godot::LocalVector<RingRow> &rows() const {
        return sampled;
    }

private:
    struct Candidate {
        uint32_t source = 0;
        uint64_t world = 0;
        int role = -1;
        double distance = 0.0;
        godot::Vector3 simulated;
    };

    struct Clock {
        int64_t tick = 0;
        double tick_seconds = 0.0;
    };

    struct Seen {
        bool resolved = false;
        const predict::Wiring *wiring = nullptr;
        int field = -1;
        godot::StringName key;
        bool global_key = false;
        bool parent_key = false;
        bool visual_resolved = false;
        uint64_t visual = 0;
        bool store_resolved = false;
        bool has_store = false;
        uint64_t store_owner = 0;
        uint32_t store_lane = 0;
        uint32_t store_column = 0;
        StateLine line;
        godot::String text;
        uint64_t generation = 0;
    };

    struct WorldCamera {
        uint64_t world = 0;
        godot::Vector3 point;
    };

    godot::HashMap<int64_t, Seen> seen;
    godot::LocalVector<Candidate> candidates;
    godot::LocalVector<RingRow> sampled;
    godot::LocalVector<WorldCamera> cameras;
    godot::LocalVector<int64_t> departed;
    uint64_t generation = 0;
    std::unique_ptr<Sketch> sketch;

    void select();
    void read(
        NetwMultiplayer &p_session,
        NetwPredictionEngine &p_pool,
        const Candidate &p_candidate,
        const Clock &p_now
    );
    godot::Vector3 camera_point(uint64_t p_world, godot::Node *p_owner);
    void forget_departed();
};

} // namespace diag
} // namespace netw

#endif

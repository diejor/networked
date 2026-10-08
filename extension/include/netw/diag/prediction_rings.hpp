#pragma once

#if defined(DEBUG_ENABLED)

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/variant.hpp"
#include "netw/diag/sketch.hpp"
#include "netw/diag/space.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;
class NetwPredictionEngine;

namespace predict {
struct Wiring;
} // namespace predict

namespace diag {

enum RingMark : uint8_t {
    MARK_SHOWN = 1u << 0,
    MARK_SIMULATED = 1u << 1,
    MARK_SERVER = 1u << 2,
    MARK_OURS = 1u << 3,
};

struct RingRow {
    int64_t key = 0;
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    int role = -1;
    uint8_t marks = 0;
    godot::Vector3 shown;
    godot::Vector3 simulated;
    godot::Vector3 server;
    godot::Vector3 ours;
    double gap = 0.0;
    double epsilon = 0.0;
    double confirmed_age = 0.0;
    float confirmed_alpha = 1.0f;
    godot::String text;
    double camera_distance = 0.0;

    bool has(RingMark p_mark) const {
        return (marks & p_mark) != 0;
    }
    bool erred() const {
        return has(MARK_SERVER) && has(MARK_OURS) && gap > epsilon;
    }
};

void draw_ring_rows(
    const godot::LocalVector<RingRow> &p_rows,
    Drawing &r_drawing
);

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
    static constexpr uint32_t ENTITY_CAP = Sketch::ENTITY_CAP;
    static constexpr double FRESH_SECONDS = 1.0;
    static constexpr float STALE_ALPHA = 0.25f;
    static constexpr float RADIUS_SHOWN = 0.72f;
    static constexpr float RADIUS_SIMULATED = 0.86f;
    static constexpr float RADIUS_SERVER = 1.0f;
    static constexpr float RADIUS_OURS = 1.14f;
    static constexpr float PIXELS_PER_METRE = 32.0f;

    static bool wanted();

    PredictionRings();

    godot::LocalVector<RingSource> sources;

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

    godot::HashMap<int64_t, Seen> seen;
    godot::LocalVector<Candidate> candidates;
    godot::LocalVector<RingRow> sampled;
    CameraBook cameras;
    godot::LocalVector<int64_t> departed;
    uint64_t generation = 0;

    void read(
        NetwMultiplayer &p_session,
        NetwPredictionEngine &p_pool,
        const Candidate &p_candidate,
        const Clock &p_now
    );
    void forget_departed();
};

} // namespace diag
} // namespace netw

#endif

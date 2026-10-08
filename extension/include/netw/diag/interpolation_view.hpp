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

class NetwMultiplayer;

namespace display {
class Channel;
class Runtime;
} // namespace display

namespace diag {

struct InterpSource {
    int64_t route = 0;
    display::Runtime *runtime = nullptr;
    godot::Node *owner = nullptr;
};

enum TimelinePhase : int {
    PHASE_NONE = -1,
    PHASE_REST = 0,
    PHASE_BUFFERED = 1,
    PHASE_HOLD = 2,
    PHASE_STARVING = 3,
    PHASE_FORECAST = 4,
    PHASE_GLIDE = 5,
};

struct HeldSample {
    int64_t tick = -1;
    godot::Vector3 point;
    bool buffered = false;
    bool follows_miss = false;
    bool turned = false;
    godot::Quaternion heading;
};

struct TimelineLine {
    int64_t pump = -1;
    int phase = PHASE_NONE;
    bool snapped = false;
    bool flat = false;
    int64_t buffer_tenths = 0;
    int64_t lag_tenths = 0;
    bool has_age = false;
    int64_t age_tenths = 0;
    bool has_received = false;
    int64_t received_ahead = 0;
    int64_t forecast_tenths = 0;
    int64_t forecast_cap = 0;
    int64_t glide_hundredths = 0;
    int64_t misses = 0;
    int64_t horizon = 0;

    bool operator==(const TimelineLine &p_other) const;
    godot::String text() const;
};

struct TimelineRow {
    int64_t route = 0;
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    float scale = 1.0f;
    int64_t pump = -1;
    int64_t role = 0;
    int phase = PHASE_NONE;
    bool snapped = false;
    bool has_head = false;
    double playhead = 0.0;
    double buffer = 0.0;
    double lag = 0.0;
    bool has_age = false;
    double arrival_age = 0.0;
    bool finished = false;
    bool has_received = false;
    int64_t received_ahead = 0;
    int64_t misses = 0;
    double forecast_age = 0.0;
    int64_t forecast_cap = 0;
    int64_t horizon = 0;
    double glide = 0.0;
    godot::Vector3 chase_target;
    godot::Vector3 anchor;
    uint32_t first_sample = 0;
    uint32_t sample_count = 0;
    godot::String text;
    double camera_distance = 0.0;
};

enum TimelineTint : int {
    TINT_PAST,
    TINT_BUFFERED,
    TINT_HOLD,
    TINT_STARVING,
    TINT_MISS,
    TINT_BRACKET,
    TINT_GLIDE,
};

struct TimelineStyle {
    static constexpr float MARK_METRES = 0.03f;
    static constexpr float MARK_PIXELS = 1.5f;
    static constexpr float SIZE_RATIO = 1.0f / 32.0f;
    static constexpr float MAX_SCALE = 6.0f;
    static constexpr float HEAD_SCALE = 2.5f;
    static constexpr float HEAD_ALPHA = 0.6f;
    static constexpr float HEADING_METRES = 0.15f;
    static constexpr float HEADING_PIXELS = 6.0f;
    static constexpr float GHOST_ALPHA = 0.5f;
    static constexpr double GHOST_TURN_RADIANS = 0.035;

    static godot::Color tint(TimelineTint p_tint, float p_alpha = 1.0f);
    static TimelineTint head_tint(int p_phase);
    static float mark_alpha(double p_distance, double p_reach);
    static float scale_for(double p_size, bool p_flat);
};

void draw_timelines(
    const godot::LocalVector<TimelineRow> &p_rows,
    const godot::LocalVector<HeldSample> &p_marks,
    Drawing &r_drawing
);

class InterpolationView {
public:
    static constexpr uint32_t ENTITY_CAP = Sketch::ENTITY_CAP;
    static constexpr uint32_t SAMPLE_CAP = Sketch::SAMPLE_CAP;
    static constexpr uint32_t GHOST_PARTS = Sketch::GHOST_PARTS;
    static constexpr uint32_t GHOST_WALK_CAP = 256;
    static constexpr double SNAP_SECONDS = 1.0;
    static constexpr double GLIDE_FLOOR_METRES = 0.01;
    static constexpr double GLIDE_FLOOR_PIXELS = 1.0;

    static bool wanted();

    InterpolationView();

    godot::LocalVector<InterpSource> sources;
    bool rings_drawn = false;
#if defined(NETW_TESTS)
    uint64_t part_walks = 0;
#endif

    void sample(NetwMultiplayer &p_session);
    const godot::LocalVector<TimelineRow> &rows() const {
        return sampled;
    }
    const godot::LocalVector<HeldSample> &marks() const {
        return held;
    }
    const godot::LocalVector<Ghost> &ghosts() const {
        return ghosted;
    }

private:
    enum HeadingFrom {
        HEADING_UNRESOLVED,
        HEADING_NONE,
        HEADING_POSE,
        HEADING_COLUMN,
    };

    struct Now {
        int64_t tick = 0;
        int64_t display_tick = 0;
        double tick_factor = 0.0;
        int64_t snap_ticks = 1;
    };

    struct Seen {
        const display::Runtime *runtime = nullptr;
        uint32_t channel_count = 0;
        int pose = -1;
        bool global_key = false;
        bool parent_key = false;
        int heading_from = HEADING_UNRESOLVED;
        uint64_t heading_owner = 0;
        uint32_t heading_lane = 0;
        uint32_t heading_column = 0;
        int64_t snap_until = -1;
        TimelineLine line;
        godot::String text;
        uint64_t generation = 0;
        bool parts_found = false;
        uint32_t part_count = 0;
        double part_size = 0.0;
        uint64_t parts[GHOST_PARTS] = {};
        godot::Node *part_nodes[GHOST_PARTS] = {};
    };

    struct Candidate {
        Seen *entry = nullptr;
        godot::Node *frame = nullptr;
        uint32_t source = 0;
        uint64_t world = 0;
        double distance = 0.0;
        godot::Vector3 anchor;
        double glide = 0.0;
        bool forecast = false;
    };

    struct WorldTexts {
        uint64_t world = 0;
        uint32_t count = 0;
    };

    godot::HashMap<int64_t, Seen> seen;
    godot::LocalVector<Candidate> candidates;
    godot::LocalVector<WorldTexts> texted;
    godot::LocalVector<TimelineRow> sampled;
    godot::LocalVector<HeldSample> held;
    godot::LocalVector<Ghost> ghosted;
    godot::LocalVector<godot::Node *> walk;
    godot::LocalVector<int64_t> held_ticks;
    godot::LocalVector<godot::Variant> held_values;
    godot::LocalVector<int64_t> departed;
    CameraBook cameras;
    uint64_t generation = 0;

    bool resolve_pose(Seen &r_entry, display::Runtime &p_runtime);
    void resolve_heading(
        Seen &r_entry,
        const display::Channel &p_pose,
        NetwMultiplayer &p_session
    );
    void read(
        NetwMultiplayer &p_session,
        const Candidate &p_candidate,
        const Now &p_now
    );
    void read_remote(
        NetwMultiplayer &p_session,
        Seen &r_entry,
        display::Runtime &p_runtime,
        godot::Node *p_frame,
        TimelineRow &r_row,
        const Now &p_now
    );
    void read_forecast(
        NetwMultiplayer &p_session,
        Seen &r_entry,
        display::Runtime &p_runtime,
        godot::Node *p_frame,
        TimelineRow &r_row,
        const Now &p_now
    );
    void hold_marks(
        NetwMultiplayer &p_session,
        const Seen &p_entry,
        godot::Node *p_frame,
        uint32_t p_count,
        double p_buffered_after,
        TimelineRow &r_row
    );
    void find_parts(Seen &r_entry, godot::Node *p_frame, bool p_flat);
    void cast_ghost(
        Seen &r_entry,
        godot::Node *p_frame,
        godot::Node *p_drawn,
        const TimelineRow &p_row
    );
    void cast_spatial_ghost(
        const Seen &r_entry,
        godot::Node *p_frame,
        const TimelineRow &p_row,
        const HeldSample &p_newest
    );
    void cast_flat_ghost(
        const Seen &r_entry,
        godot::Node *p_frame,
        const TimelineRow &p_row,
        const HeldSample &p_newest
    );
    bool takes_text(uint64_t p_world);
    const godot::String &text_of(Seen &r_entry, const TimelineLine &p_line);
    void forget_departed();
};

} // namespace diag
} // namespace netw

#endif

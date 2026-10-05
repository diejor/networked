#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/tick_ring.hpp"

namespace netw::store {
class World;
class Table;
} // namespace netw::store

namespace netw::display {

double angle_difference(double p_from, double p_to);

#if defined(NETW_TESTS)
struct SampleWork {
    int64_t samples = 0;
    int64_t allocations = 0;
};

SampleWork &sample_work();
#endif

class History {
private:
    struct Rows {
        const store::Table *table = nullptr;
        uint32_t row = 0;
        int64_t count = 0;
    };

    TickRing ring;
    const store::World *world = nullptr;
    uint64_t owner = 0;
    uint32_t lane = 0;
    uint32_t column = 0;
    uint32_t floor_append = 0;
    uint32_t seen_append = 0;
    bool floor_pending = false;

    int mode = 1;
    double snap_distance = 0.0;
    bool sleeping = false;
    int tick_domain = -1;
    int value_type = 0;
    godot::Variant last_recorded;
    bool has_recorded = false;
    bool source_rested = false;

    bool projected = false;
    double project_age = 0.0;
    bool snap_taken = false;

    Rows rows() const;
    int64_t tick_in(const Rows &p_rows, int64_t p_age) const;
    godot::Variant value_in(const Rows &p_rows, int64_t p_age) const;
    godot::Vector2i bracket_in(const Rows &p_rows, int64_t p_tick) const;
    godot::Variant at_in(const Rows &p_rows, int64_t p_tick) const;

    godot::Variant finite_velocity(
        const Rows &p_rows,
        int64_t newest,
        double ticktime
    ) const;
    bool velocity_negligible(const godot::Variant &velocity) const;
    godot::Variant lerp_bracketed(
        const godot::Variant &previous,
        const godot::Variant &next,
        int64_t previous_tick,
        int64_t next_tick,
        int64_t dt,
        double factor,
        int64_t expected_interval_ticks
    ) const;
    godot::Variant interpolate(
        const godot::Variant &from,
        const godot::Variant &to,
        double weight
    ) const;
    bool exceeds(
        const godot::Variant &from,
        const godot::Variant &to,
        double distance
    ) const;

public:
    bool is_close(const godot::Variant &from, const godot::Variant &to) const;

    enum Pass {
        PASS_SKIP_SLEEPING,
        PASS_SKIP_EMPTY,
        PASS_SAMPLE,
        PASS_SAMPLE_PROJECT,
    };

    History();

    int64_t held_bytes() const {
        return ring.held_bytes();
    }

    int pass_verdict(
        const godot::Ref<NetwInterpolate> &p_spec,
        bool p_forecast
    ) const;

    void set_mode(int value) {
        mode = value;
    }
    int get_mode() const {
        return mode;
    }

    void set_snap_distance(double value) {
        snap_distance = value;
    }
    double get_snap_distance() const {
        return snap_distance;
    }

    void set_sleeping(bool value) {
        sleeping = value;
    }
    bool is_sleeping() const {
        return sleeping;
    }

    void set_source_rested(bool value) {
        source_rested = value;
    }
    bool holds_rested_source() const {
        return source_rested && has_recorded;
    }

    bool has_projected() const {
        return projected;
    }
    double get_project_age() const {
        return project_age;
    }
    bool has_snapped() const {
        return snap_taken;
    }

    void follow(
        const store::World *p_world,
        uint64_t p_owner,
        uint32_t p_lane,
        uint32_t p_column
    );
    void unfollow();
    bool follows_table() const {
        return world != nullptr;
    }
    bool holds_ring() const {
        return ring.holds_storage();
    }
    bool follows(uint64_t p_owner, uint32_t p_lane, uint32_t p_column) const {
        return world != nullptr && owner == p_owner && lane == p_lane
            && column == p_column;
    }
    bool follow_address(
        uint64_t &r_owner,
        uint32_t &r_lane,
        uint32_t &r_column
    ) const {
        r_owner = owner;
        r_lane = lane;
        r_column = column;
        return world != nullptr;
    }
    bool table_pulls() const;
    bool table_rests() const;
    void notice();

    void record(int64_t tick, const godot::Variant &value, bool authoring_tick);
    void record_every_tick(int64_t tick, const godot::Variant &value);
    void clear();
    bool is_empty() const;
    int64_t size() const;
    int64_t newest_tick() const;
    bool has_tick_after(int64_t tick) const;
    godot::Vector2i bracketing_ticks(int64_t tick) const;
    godot::Variant get_at(int64_t tick) const;

    godot::Variant sample(
        int64_t dt,
        double factor,
        const godot::Variant &last_written,
        int64_t expected_interval_ticks,
        bool forecast,
        int64_t max_forecast_ticks,
        double ticktime,
        const godot::Variant &explicit_velocity,
        bool has_explicit_velocity
    );
    godot::Variant smooth_toward(
        const godot::Variant &last_written,
        const godot::Variant &result,
        double weight
    );
};

} // namespace netw::display

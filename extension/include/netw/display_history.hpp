#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/interpolate.hpp"
#include "netw/ring_buffer.hpp"

namespace netw {

double angle_difference(double p_from, double p_to);

class NetwDisplayHistory : public godot::RefCounted {
    GDCLASS(NetwDisplayHistory, godot::RefCounted)

    godot::Ref<NetwRingBuffer> buffer;
    int mode = 1;
    double snap_distance = 0.0;
    bool sleeping = false;
    int tick_domain = -1;
    int value_type = 0;
    godot::Variant last_recorded;
    bool has_recorded = false;

    bool projected = false;
    double project_age = 0.0;
    bool snap_taken = false;

    godot::Variant finite_velocity(int64_t newest, double ticktime) const;
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
    bool is_close(const godot::Variant &from, const godot::Variant &to) const;

protected:
    static void _bind_methods();

public:
    enum Pass {
        PASS_SKIP_SLEEPING,
        PASS_SKIP_EMPTY,
        PASS_SAMPLE,
        PASS_SAMPLE_PROJECT,
    };

    NetwDisplayHistory();

    int pass_verdict(
        const godot::Ref<NetwInterpolate> &p_spec,
        bool p_forecast
    ) const;

    void set_mode(int value) { mode = value; }
    int get_mode() const { return mode; }

    void set_snap_distance(double value) { snap_distance = value; }
    double get_snap_distance() const { return snap_distance; }

    void set_sleeping(bool value) { sleeping = value; }
    bool is_sleeping() const { return sleeping; }

    bool has_projected() const { return projected; }
    double get_project_age() const { return project_age; }
    bool has_snapped() const { return snap_taken; }

    godot::Ref<NetwRingBuffer> get_buffer() const { return buffer; }

    void record(
        int64_t tick,
        const godot::Variant &value,
        bool authoring_tick
    );
    void clear();
    bool is_empty() const;
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

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwDisplayHistory::Pass);

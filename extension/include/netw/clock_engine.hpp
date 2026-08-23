#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/object_port.hpp"

namespace netw {

class ClockEngine {
public:
    enum SyncMode {
        SYNC_SNAP = 0,
        SYNC_STRETCH = 1,
    };

    static int64_t pumps_for(double seconds, double rate);

private:
    static constexpr uint64_t NEVER_STAMPED = 0;

    struct Stats {
        double rtt = 0.0;
        double avg = 0.0;
        double jitter = 0.0;
        bool is_stable = true;
        godot::LocalVector<double> samples;

        void record(double sample, double stability_threshold, int window);
        void clear();
    };

    int32_t tickrate = 30;
    bool configured = false;
    bool use_physics_interpolation = true;
    double tick_factor_override = -1.0;
    int32_t max_ticks_per_frame = 8;
    double stall_threshold = 1.0;
    SyncMode sync_mode = SYNC_STRETCH;
    int32_t panic_snap_threshold = 20;
    double stretch_nudge_factor = 0.05;
    double ping_interval = 0.1;
    double lead_ticks = 1.0;
    int32_t display_offset = 2;
    double jitter_multiplier = 2.0;
    int32_t jitter_window = 16;
    double jitter_stability_threshold = 0.05;
    bool manual_tick = false;
    bool node_pumped = false;
    bool enable_drift_logging = false;

    int32_t tick = 0;
    bool is_synchronized = false;
    bool simulating = true;
    int32_t simulation_behind_count = 0;

    double accumulator = 0.0;
    double target_tick_estimate = 0.0;
    double ping_timer = 0.0;
    bool display_offset_insufficient_latched = false;
    uint64_t step_stamp_usec = 0;

    int32_t simulation_gates = 0;
    int32_t simulation_credit = 0;

    int64_t physics_frames = 0;
    int64_t polls = 0;
    uint64_t cadence_started_usec = 0;

    Stats stats;

    double seconds_into_frame() const;
    void announce(const godot::StringName &p_signal);
    void announce(const godot::StringName &p_signal, const godot::Variant &p_a);
    void announce(
        const godot::StringName &p_signal,
        const godot::Variant &p_a,
        const godot::Variant &p_b
    );
    void emit_tick();
    void calibrate(double target);
    void nudge_toward_estimate();
    void notify_display_offset();
    void resolve_simulation_gate(int ticks_this_frame);

public:
    ObjectPort sink;

    void set_tickrate(int value);
    int get_tickrate() const;
    void set_max_ticks_per_frame(int value);
    int get_max_ticks_per_frame() const;
    void set_stall_threshold(double value);
    double get_stall_threshold() const;
    void set_sync_mode(SyncMode value);
    SyncMode get_sync_mode() const;
    void set_panic_snap_threshold(int value);
    int get_panic_snap_threshold() const;
    void set_stretch_nudge_factor(double value);
    double get_stretch_nudge_factor() const;
    void set_ping_interval(double value);
    double get_ping_interval() const;
    void set_lead_ticks(double value);
    double get_lead_ticks() const;
    void set_display_offset(int value);
    int get_display_offset() const;
    void set_jitter_multiplier(double value);
    double get_jitter_multiplier() const;
    void set_jitter_window(int value);
    int get_jitter_window() const;
    void set_jitter_stability_threshold(double value);
    double get_jitter_stability_threshold() const;
    void set_configured(bool value);
    bool get_configured() const;
    void set_use_physics_interpolation(bool value);
    bool get_use_physics_interpolation() const;
    void set_tick_factor_override(double value);
    double get_tick_factor_override() const;
    void set_manual_tick(bool value);
    bool get_manual_tick() const;
    void set_node_pumped(bool value);
    bool get_node_pumped() const;
    void set_enable_drift_logging(bool value);
    bool get_enable_drift_logging() const;

    void set_tick(int value);
    int get_tick() const;
    void set_synchronized(bool value);
    bool get_synchronized() const;

    double ticktime() const;
    int display_tick() const;
    double physics_factor() const;
    int physics_steps_per_tick() const;
    double tick_phase() const;
    double tick_accumulator() const;
    double tick_factor() const;
    double rtt() const;
    double rtt_avg() const;
    double rtt_jitter() const;
    double one_way_latency() const;
    int recommended_display_offset() const;
    bool is_stable() const;

    void physics_step(double delta);
    void force_step(int count);

    void begin_tick_loop();
    void end_tick_loop();

    void count_poll();
    void poll_step();
    godot::Dictionary cadence() const;

    double seconds_since_step() const;
    void mark_step(double seconds_ago = 0.0);

    bool is_simulating() const;
    int get_simulation_behind_count() const;

    void arm_gate();
    void release_gate();
    bool is_gated() const;

    godot::Dictionary handle_pong(
        double sample,
        int server_tick_at_pong,
        double server_tick_phase,
        bool apply_lead
    );

    bool consume_ping_due(double delta);

    void clear();
};

} // namespace netw

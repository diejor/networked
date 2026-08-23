#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/clock_engine.hpp"

namespace netw {

class NetwClockHandle : public godot::RefCounted {
    GDCLASS(NetwClockHandle, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    enum SyncMode {
        SYNC_SNAP = ClockEngine::SYNC_SNAP,
        SYNC_STRETCH = ClockEngine::SYNC_STRETCH,
    };

    ClockEngine engine;

    static int64_t pumps_for(double p_seconds, double p_rate) {
        return ClockEngine::pumps_for(p_seconds, p_rate);
    }

    void set_tickrate(int p_value) { engine.set_tickrate(p_value); }
    int get_tickrate() const { return engine.get_tickrate(); }

    void set_max_ticks_per_frame(int p_value) {
        engine.set_max_ticks_per_frame(p_value);
    }
    int get_max_ticks_per_frame() const {
        return engine.get_max_ticks_per_frame();
    }

    void set_stall_threshold(double p_value) {
        engine.set_stall_threshold(p_value);
    }
    double get_stall_threshold() const { return engine.get_stall_threshold(); }

    void set_use_physics_interpolation(bool p_value) {
        engine.set_use_physics_interpolation(p_value);
    }
    bool get_use_physics_interpolation() const {
        return engine.get_use_physics_interpolation();
    }

    void set_sync_mode(SyncMode p_value) {
        engine.set_sync_mode(ClockEngine::SyncMode(p_value));
    }
    SyncMode get_sync_mode() const { return SyncMode(engine.get_sync_mode()); }

    void set_panic_snap_threshold(int p_value) {
        engine.set_panic_snap_threshold(p_value);
    }
    int get_panic_snap_threshold() const {
        return engine.get_panic_snap_threshold();
    }

    void set_stretch_nudge_factor(double p_value) {
        engine.set_stretch_nudge_factor(p_value);
    }
    double get_stretch_nudge_factor() const {
        return engine.get_stretch_nudge_factor();
    }

    void set_ping_interval(double p_value) {
        engine.set_ping_interval(p_value);
    }
    double get_ping_interval() const { return engine.get_ping_interval(); }

    void set_lead_ticks(double p_value) { engine.set_lead_ticks(p_value); }
    double get_lead_ticks() const { return engine.get_lead_ticks(); }

    void set_display_offset(int p_value) { engine.set_display_offset(p_value); }
    int get_display_offset() const { return engine.get_display_offset(); }

    void set_jitter_multiplier(double p_value) {
        engine.set_jitter_multiplier(p_value);
    }
    double get_jitter_multiplier() const {
        return engine.get_jitter_multiplier();
    }

    void set_jitter_window(int p_value) { engine.set_jitter_window(p_value); }
    int get_jitter_window() const { return engine.get_jitter_window(); }

    void set_jitter_stability_threshold(double p_value) {
        engine.set_jitter_stability_threshold(p_value);
    }
    double get_jitter_stability_threshold() const {
        return engine.get_jitter_stability_threshold();
    }

    void set_enable_drift_logging(bool p_value) {
        engine.set_enable_drift_logging(p_value);
    }
    bool get_enable_drift_logging() const {
        return engine.get_enable_drift_logging();
    }

    void set_tick(int p_value) { engine.set_tick(p_value); }
    int get_tick() const { return engine.get_tick(); }

    void set_is_synchronized(bool p_value) { engine.set_synchronized(p_value); }
    bool get_is_synchronized() const { return engine.get_synchronized(); }

    void set_is_configured(bool p_value) { engine.set_configured(p_value); }
    bool get_is_configured() const { return engine.get_configured(); }

    void set_manual_tick(bool p_value) { engine.set_manual_tick(p_value); }
    bool get_manual_tick() const { return engine.get_manual_tick(); }

    void set_node_pumped(bool p_value) { engine.set_node_pumped(p_value); }
    bool get_node_pumped() const { return engine.get_node_pumped(); }

    void set_tick_factor_override(double p_value) {
        engine.set_tick_factor_override(p_value);
    }
    double get_tick_factor_override() const {
        return engine.get_tick_factor_override();
    }

    double get_ticktime() const { return engine.ticktime(); }
    double get_tick_factor() const { return engine.tick_factor(); }
    double get_tick_phase() const { return engine.tick_phase(); }
    double get_tick_accumulator() const { return engine.tick_accumulator(); }
    int get_display_tick() const { return engine.display_tick(); }
    double get_physics_factor() const { return engine.physics_factor(); }
    int get_physics_steps_per_tick() const {
        return engine.physics_steps_per_tick();
    }
    bool get_is_simulating() const { return engine.is_simulating(); }
    bool get_is_gated() const { return engine.is_gated(); }
    int get_simulation_behind_count() const {
        return engine.get_simulation_behind_count();
    }
    double get_rtt() const { return engine.rtt(); }
    double get_rtt_avg() const { return engine.rtt_avg(); }
    double get_rtt_jitter() const { return engine.rtt_jitter(); }
    double get_one_way_latency() const { return engine.one_way_latency(); }
    int get_recommended_display_offset() const {
        return engine.recommended_display_offset();
    }
    bool get_is_stable() const { return engine.is_stable(); }

    void physics_step(double p_delta) { engine.physics_step(p_delta); }
    void force_step(int p_count) { engine.force_step(p_count); }
    void begin_tick_loop() { engine.begin_tick_loop(); }
    void end_tick_loop() { engine.end_tick_loop(); }
    void poll_step() { engine.poll_step(); }
    void count_poll() { engine.count_poll(); }
    void arm_gate() { engine.arm_gate(); }
    void release_gate() { engine.release_gate(); }
    void mark_step(double p_seconds_ago) { engine.mark_step(p_seconds_ago); }
    double seconds_since_step() const { return engine.seconds_since_step(); }
    bool consume_ping_due(double p_delta) {
        return engine.consume_ping_due(p_delta);
    }
    void clear() { engine.clear(); }
    godot::Dictionary cadence() const { return engine.cadence(); }

    godot::Dictionary handle_pong(
        double p_sample,
        int p_server_tick_at_pong,
        double p_server_tick_phase,
        bool p_apply_lead
    ) {
        return engine.handle_pong(
            p_sample,
            p_server_tick_at_pong,
            p_server_tick_phase,
            p_apply_lead
        );
    }
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwClockHandle::SyncMode);

#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

// The tick engine: it turns frame deltas into a fixed-rate tick schedule, holds
// the calibration that keeps that schedule aligned with a server's, and decides
// which frames the simulated world may advance on.
//
// It holds no Object and sends nothing. A tick is a fixed amount of simulated
// time, so everything here is arithmetic over integers and durations, and the
// two things that are not — the ping/pong messages that carry a sample in, and
// the node that authors the configuration — live in the interface above this
// class. That split is why calibration is fed rather than fetched:
// handle_pong takes the sample someone else received.
class NetwClockCore : public godot::RefCounted {
    GDCLASS(NetwClockCore, godot::RefCounted)

public:
    enum SyncMode {
        // Hard-jump the local tick to the calibrated target on every sample.
        SYNC_SNAP = 0,
        // Nudge the accumulator toward the target, falling back to a snap once
        // the divergence exceeds panic_snap_threshold.
        SYNC_STRETCH = 1,
    };

private:
    // The RTT window and everything derived from it. Absorbed rather than
    // registered: nothing outside this class ever held one.
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

    int32_t tick = 0;
    bool is_synchronized = false;
    bool simulating = true;
    int32_t simulation_behind_count = 0;

    double accumulator = 0.0;
    double target_tick_estimate = 0.0;
    double ping_timer = 0.0;
    bool display_offset_insufficient_latched = false;

    // Gates armed from above, and the world's unspent step budget. A tick pays
    // physics_steps_per_tick in, a simulated frame spends one.
    int32_t simulation_gates = 0;
    int32_t simulation_credit = 0;

    Stats stats;

    void emit_tick();
    void calibrate(double target);
    void nudge_toward_estimate();
    void notify_display_offset();
    // Spends one frame of the step budget each tick pays into, which is what
    // makes the tick-to-step correspondence exact rather than rounded.
    void resolve_simulation_gate(int ticks_this_frame);

protected:
    static void _bind_methods();

public:
    // Configuration. Every one is authored above this class and pushed down.
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

    // The clock position. Settable because a peer that has not calibrated yet
    // is placed rather than advanced.
    void set_tick(int value);
    int get_tick() const;
    void set_synchronized(bool value);
    bool get_synchronized() const;

    // Derived readings, none of them stored.
    double ticktime() const;
    int display_tick() const;
    double physics_factor() const;
    // Physics steps one tick is worth, as a whole number. The physics server
    // runs exactly one step per frame, so a tick can be worth one step or two
    // but never one and a fifth.
    int physics_steps_per_tick() const;
    // Where the clock sits inside its current tick, in [0, 1]. This is the
    // phase a server echoes so a client can calibrate against a continuous
    // position rather than a whole tick.
    double tick_phase() const;
    // The unspent remainder in seconds, UNCLAMPED. A visual playhead reads this
    // rather than tick_phase: clamping it stalls the playhead for the part of a
    // frame that ran past a tick boundary, which shows up as a jitter the
    // clamp itself caused.
    double tick_accumulator() const;
    double rtt() const;
    double rtt_avg() const;
    double rtt_jitter() const;
    double one_way_latency() const;
    int recommended_display_offset() const;
    bool is_stable() const;

    // The tick loop. physics_step is the pumped path and force_step is the
    // stepped one, and they emit identically so a stepped rig and a running
    // session cannot mean different things by a tick.
    void physics_step(double delta);
    void force_step(int count);

    // Whether this frame's simulated world may advance, and how many frames
    // wanted a tick the ceiling refused. Sustained growth in the second means
    // this peer is too slow to predict, which no netcode setting repairs.
    bool is_simulating() const;
    int get_simulation_behind_count() const;

    // A gate is armed for each body the physics server integrates under this
    // clock, and held until released. An ungated clock simulates every frame.
    void arm_gate();
    void release_gate();
    bool is_gated() const;

    // Ingests one RTT sample and the server clock position it was measured
    // against, and answers the metrics the interface above publishes.
    //
    // apply_lead is false for a server echoing its own probe: it has no flight
    // time to its own authority and so never leads itself.
    godot::Dictionary handle_pong(
        double sample,
        int server_tick_at_pong,
        double server_tick_phase,
        bool apply_lead
    );

    // Accumulates the ping cadence, answering true and rearming when one is
    // due, so the pump above owns the send.
    bool consume_ping_due(double delta);

    // Session teardown: the schedule, the calibration and the sample window.
    void clear();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwClockCore::SyncMode);

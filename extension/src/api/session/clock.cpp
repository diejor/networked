#include "godot/class_db.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw {

Error NetwMultiplayer::clock_configure(const Ref<NetwClockConfig> &p_config) {
    if (p_config.is_null()) {
        return ERR_INVALID_PARAMETER;
    }
    clock_apply_config(p_config);
    clock_engine().set_configured(true);
    const Callable sweeper
        = callable_mp(this, &NetwMultiplayer::clock_sweep_effects);
    if (!is_connected(StringName("clock_on_tick"), sweeper)) {
        connect(StringName("clock_on_tick"), sweeper);
    }
    wire_lagcomp_service();
    return OK;
}

Ref<NetwClockConfig> NetwMultiplayer::clock_get_config() const {
    const ClockEngine &clock = clock_engine();
    Ref<NetwClockConfig> effective;
    effective.instantiate();
    effective->set_tickrate(clock.get_tickrate());
    effective->set_max_ticks_per_frame(clock.get_max_ticks_per_frame());
    effective->set_stall_threshold(clock.get_stall_threshold());
    effective->set_use_physics_interpolation(
        clock.get_use_physics_interpolation()
    );
    effective->set_sync_mode(int64_t(clock.get_sync_mode()));
    effective->set_panic_snap_threshold(clock.get_panic_snap_threshold());
    effective->set_stretch_nudge_factor(clock.get_stretch_nudge_factor());
    effective->set_ping_interval(clock.get_ping_interval());
    effective->set_display_offset(clock.get_display_offset());
    effective->set_jitter_multiplier(clock.get_jitter_multiplier());
    effective->set_jitter_window(clock.get_jitter_window());
    effective->set_jitter_stability_threshold(
        clock.get_jitter_stability_threshold()
    );
    effective->set_enable_drift_logging(clock.get_enable_drift_logging());
    return effective;
}

Variant NetwMultiplayer::clock_get_param(ClockParam p_param) const {
    const ClockEngine &engine = clock_engine();
    switch (p_param) {
        case CLOCK_PARAM_TICKRATE:
            return engine.get_tickrate();
        case CLOCK_PARAM_SYNC_MODE:
            return int(engine.get_sync_mode());
        case CLOCK_PARAM_PING_INTERVAL:
            return engine.get_ping_interval();
        case CLOCK_PARAM_MAX_TICKS_PER_FRAME:
            return engine.get_max_ticks_per_frame();
        case CLOCK_PARAM_STALL_THRESHOLD:
            return engine.get_stall_threshold();
        case CLOCK_PARAM_PANIC_SNAP_THRESHOLD:
            return engine.get_panic_snap_threshold();
        case CLOCK_PARAM_STRETCH_NUDGE_FACTOR:
            return engine.get_stretch_nudge_factor();
        case CLOCK_PARAM_DISPLAY_OFFSET:
            return engine.get_display_offset();
        case CLOCK_PARAM_LEAD_TICKS:
            return engine.get_lead_ticks();
        case CLOCK_PARAM_JITTER_MULTIPLIER:
            return engine.get_jitter_multiplier();
        case CLOCK_PARAM_JITTER_WINDOW:
            return engine.get_jitter_window();
        case CLOCK_PARAM_JITTER_STABILITY_THRESHOLD:
            return engine.get_jitter_stability_threshold();
        case CLOCK_PARAM_USE_PHYSICS_INTERPOLATION:
            return engine.get_use_physics_interpolation();
        case CLOCK_PARAM_ENABLE_DRIFT_LOGGING:
            return engine.get_enable_drift_logging();
        case CLOCK_PARAM_MANUAL_TICK:
            return engine.get_manual_tick();
        case CLOCK_PARAM_TICK_FACTOR_OVERRIDE:
            return engine.get_tick_factor_override();
    }
    return Variant();
}

Error NetwMultiplayer::clock_set_param(
    ClockParam p_param,
    const Variant &p_value
) {
    ClockEngine &engine = clock_engine();
    switch (p_param) {
        case CLOCK_PARAM_TICKRATE:
            return ERR_UNAUTHORIZED;
        case CLOCK_PARAM_SYNC_MODE:
            engine.set_sync_mode(ClockEngine::SyncMode(int(p_value)));
            return OK;
        case CLOCK_PARAM_PING_INTERVAL:
            engine.set_ping_interval(double(p_value));
            return OK;
        case CLOCK_PARAM_MAX_TICKS_PER_FRAME:
            engine.set_max_ticks_per_frame(int(p_value));
            return OK;
        case CLOCK_PARAM_STALL_THRESHOLD:
            engine.set_stall_threshold(double(p_value));
            return OK;
        case CLOCK_PARAM_PANIC_SNAP_THRESHOLD:
            engine.set_panic_snap_threshold(int(p_value));
            return OK;
        case CLOCK_PARAM_STRETCH_NUDGE_FACTOR:
            engine.set_stretch_nudge_factor(double(p_value));
            return OK;
        case CLOCK_PARAM_DISPLAY_OFFSET:
            engine.set_display_offset(int(p_value));
            return OK;
        case CLOCK_PARAM_LEAD_TICKS:
            engine.set_lead_ticks(double(p_value));
            return OK;
        case CLOCK_PARAM_JITTER_MULTIPLIER:
            engine.set_jitter_multiplier(double(p_value));
            return OK;
        case CLOCK_PARAM_JITTER_WINDOW:
            engine.set_jitter_window(int(p_value));
            return OK;
        case CLOCK_PARAM_JITTER_STABILITY_THRESHOLD:
            engine.set_jitter_stability_threshold(double(p_value));
            return OK;
        case CLOCK_PARAM_USE_PHYSICS_INTERPOLATION:
            engine.set_use_physics_interpolation(bool(p_value));
            return OK;
        case CLOCK_PARAM_ENABLE_DRIFT_LOGGING:
            engine.set_enable_drift_logging(bool(p_value));
            return OK;
        case CLOCK_PARAM_MANUAL_TICK:
            engine.set_manual_tick(bool(p_value));
            return OK;
        case CLOCK_PARAM_TICK_FACTOR_OVERRIDE:
            engine.set_tick_factor_override(double(p_value));
            return OK;
            return OK;
    }
    return ERR_INVALID_PARAMETER;
}

double NetwMultiplayer::clock_get_monitor(ClockMonitor p_monitor) const {
    const ClockEngine &engine = clock_engine();
    switch (p_monitor) {
        case CLOCK_MONITOR_RTT:
            return engine.rtt();
        case CLOCK_MONITOR_RTT_AVG:
            return engine.rtt_avg();
        case CLOCK_MONITOR_RTT_JITTER:
            return engine.rtt_jitter();
        case CLOCK_MONITOR_ONE_WAY_LATENCY:
            return engine.one_way_latency();
        case CLOCK_MONITOR_TICKTIME:
            return engine.ticktime();
        case CLOCK_MONITOR_TICK_FACTOR:
            return engine.tick_factor();
        case CLOCK_MONITOR_TICK_PHASE:
            return engine.tick_phase();
        case CLOCK_MONITOR_TICK_ACCUMULATOR:
            return engine.tick_accumulator();
        case CLOCK_MONITOR_PHYSICS_FACTOR:
            return engine.physics_factor();
        case CLOCK_MONITOR_RECOMMENDED_DISPLAY_OFFSET:
            return double(engine.recommended_display_offset());
        case CLOCK_MONITOR_PHYSICS_FRAMES:
            return double(engine.cadence()[StringName("physics_frames")]);
        case CLOCK_MONITOR_POLLS:
            return double(engine.cadence()[StringName("polls")]);
        case CLOCK_MONITOR_WALL_SECONDS:
            return double(engine.cadence()[StringName("wall_seconds")]);
        case CLOCK_MONITOR_PHYSICS_HZ:
            return double(engine.cadence()[StringName("physics_hz")]);
        case CLOCK_MONITOR_POLL_HZ:
            return double(engine.cadence()[StringName("poll_hz")]);
    }
    return 0.0;
}

int64_t NetwMultiplayer::clock_get_tick() const {
    return clock_engine().get_tick();
}

int64_t NetwMultiplayer::clock_get_display_tick() const {
    return clock_engine().display_tick();
}

int64_t NetwMultiplayer::clock_get_physics_steps_per_tick() const {
    return clock_engine().physics_steps_per_tick();
}

int64_t NetwMultiplayer::clock_get_simulation_behind_count() const {
    return clock_engine().get_simulation_behind_count();
}

bool NetwMultiplayer::clock_is_configured() const {
    return clock_engine().get_configured();
}

bool NetwMultiplayer::clock_is_synchronized() const {
    return clock_engine().get_synchronized();
}

bool NetwMultiplayer::clock_is_simulating() const {
    return clock_engine().is_simulating();
}

bool NetwMultiplayer::clock_is_stable() const {
    return clock_engine().is_stable();
}

bool NetwMultiplayer::clock_is_gated() const {
    return clock_engine().is_gated();
}

void NetwMultiplayer::clock_step(int64_t p_count) {
    clock_engine().force_step(int(p_count));
}

void NetwMultiplayer::clock_physics_step(double p_delta) {
    clock_engine().physics_step(p_delta);
}

bool NetwMultiplayer::clock_consume_ping_due(double p_delta) {
    return clock_engine().consume_ping_due(p_delta);
}

void NetwMultiplayer::clock_tick_loop(bool p_open) {
    if (p_open) {
        clock_engine().begin_tick_loop();
        return;
    }
    clock_engine().end_tick_loop();
}

void NetwMultiplayer::clock_set_gate(bool p_armed) {
    if (p_armed) {
        clock_engine().arm_gate();
        return;
    }
    clock_engine().release_gate();
}

void NetwMultiplayer::clock_set_synchronized(bool p_value) {
    clock_engine().set_synchronized(p_value);
}

Dictionary NetwMultiplayer::clock_ingest_pong(
    double p_sample,
    int64_t p_server_tick_at_pong,
    double p_server_tick_phase,
    bool p_apply_lead
) {
    return clock_engine().handle_pong(
        p_sample,
        int(p_server_tick_at_pong),
        p_server_tick_phase,
        p_apply_lead
    );
}

} // namespace netw

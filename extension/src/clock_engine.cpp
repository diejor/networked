#include "netw/clock_engine.hpp"

#include <algorithm>
#include <cmath>

#include "godot/engine.hpp"
#include "godot/time.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_BEFORE_TICK = "before_tick";
const char *SIG_ON_TICK = "on_tick";
const char *SIG_AFTER_TICK = "after_tick";
const char *SIG_BEFORE_TICK_LOOP = "before_tick_loop";
const char *SIG_AFTER_TICK_LOOP = "after_tick_loop";
const char *SIG_CLOCK_SYNCHRONIZED = "clock_synchronized";
const char *SIG_STABILITY_CHANGED = "stability_changed";
const char *SIG_DISPLAY_OFFSET_INSUFFICIENT = "display_offset_insufficient";

int physics_ticks_per_second() {
    const Engine *engine = Engine::get_singleton();
    return engine ? engine->get_physics_ticks_per_second() : 60;
}

uint64_t wall_usec() {
    const Time *clock = Time::get_singleton();
    return clock ? clock->get_ticks_usec() : 0;
}

} // namespace

void ClockEngine::Stats::record(
    double sample,
    double stability_threshold,
    int window
) {
    rtt = sample;
    samples.push_back(sample);
    const uint32_t keep = uint32_t(window > 1 ? window : 1);
    while (samples.size() > keep) {
        samples.remove_at(0);
    }

    double sum = 0.0;
    for (const double value : samples) {
        sum += value;
    }
    avg = sum / double(samples.size());

    double deviation = 0.0;
    for (const double value : samples) {
        deviation += std::fabs(value - avg);
    }
    jitter = deviation / double(samples.size());
    is_stable = jitter < stability_threshold;
}

void ClockEngine::Stats::clear() {
    rtt = 0.0;
    avg = 0.0;
    jitter = 0.0;
    is_stable = true;
    samples.clear();
}

#define NETW_CLOCK_ACCESSOR(m_type, m_name, m_field) \
    void ClockEngine::set_##m_name(m_type value) { \
        m_field = value; \
    } \
    m_type ClockEngine::get_##m_name() const { \
        return m_field; \
    }

NETW_CLOCK_ACCESSOR(int, tickrate, tickrate)
NETW_CLOCK_ACCESSOR(int, max_ticks_per_frame, max_ticks_per_frame)
NETW_CLOCK_ACCESSOR(double, stall_threshold, stall_threshold)
NETW_CLOCK_ACCESSOR(int, panic_snap_threshold, panic_snap_threshold)
NETW_CLOCK_ACCESSOR(double, stretch_nudge_factor, stretch_nudge_factor)
NETW_CLOCK_ACCESSOR(double, ping_interval, ping_interval)
NETW_CLOCK_ACCESSOR(double, lead_ticks, lead_ticks)
NETW_CLOCK_ACCESSOR(int, display_offset, display_offset)
NETW_CLOCK_ACCESSOR(double, jitter_multiplier, jitter_multiplier)
NETW_CLOCK_ACCESSOR(int, jitter_window, jitter_window)
NETW_CLOCK_ACCESSOR(
    double,
    jitter_stability_threshold,
    jitter_stability_threshold
)
NETW_CLOCK_ACCESSOR(int, tick, tick)
NETW_CLOCK_ACCESSOR(bool, synchronized, is_synchronized)
NETW_CLOCK_ACCESSOR(bool, configured, configured)
NETW_CLOCK_ACCESSOR(
    bool,
    use_physics_interpolation,
    use_physics_interpolation
)
NETW_CLOCK_ACCESSOR(double, tick_factor_override, tick_factor_override)

#undef NETW_CLOCK_ACCESSOR

void ClockEngine::set_sync_mode(SyncMode value) {
    sync_mode = value;
}

ClockEngine::SyncMode ClockEngine::get_sync_mode() const {
    return sync_mode;
}

double ClockEngine::ticktime() const {
    return 1.0 / double(tickrate);
}

int ClockEngine::display_tick() const {
    const int shown = tick - display_offset;
    return shown > 0 ? shown : 0;
}

double ClockEngine::physics_factor() const {
    return double(physics_ticks_per_second()) / double(tickrate);
}

int ClockEngine::physics_steps_per_tick() const {
    const int steps = int(std::lround(physics_factor()));
    return steps > 1 ? steps : 1;
}

double ClockEngine::tick_phase() const {
    const double phase = accumulator / ticktime();
    if (phase < 0.0) {
        return 0.0;
    }
    return phase > 1.0 ? 1.0 : phase;
}

double ClockEngine::tick_accumulator() const {
    return accumulator;
}

double ClockEngine::seconds_into_frame() const {
    if (use_physics_interpolation) {
        const Engine *engine = Engine::get_singleton();
        const double fraction
            = engine ? engine->get_physics_interpolation_fraction() : 0.0;
        return fraction / double(physics_ticks_per_second());
    }
    const double span = seconds_since_step();
    return span > 0.0 ? span : 0.0;
}

double ClockEngine::tick_factor() const {
    if (tick_factor_override >= 0.0) {
        return tick_factor_override;
    }
    const Engine *engine = Engine::get_singleton();
    if (!configured || (engine && engine->is_editor_hint())) {
        return 0.0;
    }
    return (accumulator + seconds_into_frame()) / ticktime();
}

double ClockEngine::seconds_since_step() const {
    if (step_stamp_usec == NEVER_STAMPED) {
        return -1.0;
    }
    return double(wall_usec() - step_stamp_usec) / 1'000'000.0;
}

void ClockEngine::mark_step(double seconds_ago) {
    const uint64_t now = wall_usec();
    const uint64_t back
        = seconds_ago > 0.0 ? uint64_t(seconds_ago * 1'000'000.0) : 0;
    step_stamp_usec = back < now ? now - back : NEVER_STAMPED + 1;
}

double ClockEngine::rtt() const {
    return stats.rtt;
}

double ClockEngine::rtt_avg() const {
    return stats.avg;
}

double ClockEngine::rtt_jitter() const {
    return stats.jitter;
}

double ClockEngine::one_way_latency() const {
    return stats.avg * 0.5;
}

int ClockEngine::recommended_display_offset() const {
    if (!is_synchronized) {
        return display_offset;
    }
    return int(std::ceil(
        (one_way_latency() + stats.jitter * jitter_multiplier)
        * double(tickrate)
    ));
}

bool ClockEngine::is_stable() const {
    return stats.is_stable;
}

bool ClockEngine::is_simulating() const {
    return simulating;
}

int ClockEngine::get_simulation_behind_count() const {
    return simulation_behind_count;
}

void ClockEngine::announce(const StringName &p_signal) {
    Object *owner = sink.resolve(sys::CLOCK);
    if (owner != nullptr) {
        owner->emit_signal(p_signal);
    }
}

void ClockEngine::announce(const StringName &p_signal, const Variant &p_a) {
    Object *owner = sink.resolve(sys::CLOCK);
    if (owner != nullptr) {
        owner->emit_signal(p_signal, p_a);
    }
}

void ClockEngine::announce(
    const StringName &p_signal,
    const Variant &p_a,
    const Variant &p_b
) {
    Object *owner = sink.resolve(sys::CLOCK);
    if (owner != nullptr) {
        owner->emit_signal(p_signal, p_a, p_b);
    }
}

void ClockEngine::emit_tick() {
    NETW_ZONE_NC("ClockEngine tick", colors::CLOCK);
    NETW_ZONE_VALUE(tick);
    NETW_TICK_MARK();
    const double step = ticktime();
    announce(SIG_BEFORE_TICK, step, tick);
    announce(SIG_ON_TICK, step, tick);
    announce(SIG_AFTER_TICK, step, tick);
    tick += 1;
}

void ClockEngine::force_step(int count) {
    for (int index = 0; index < count; ++index) {
        emit_tick();
    }
    resolve_simulation_gate(count);
}

void ClockEngine::physics_step(double delta) {
    NETW_ZONE_NC("ClockEngine physics step", colors::CLOCK);
    physics_frames += 1;
    if (cadence_started_usec == 0) {
        cadence_started_usec = wall_usec();
    }
    mark_step();
    NETW_WARN_COND_ONCE(
        delta > stall_threshold,
        sys::CLOCK,
        "Physics step %.3f exceeded stall threshold %.3f.",
        delta,
        stall_threshold
    );
    if (delta > stall_threshold) {
        accumulator = 0.0;
        simulation_credit = 0;
    }

    begin_tick_loop();

    accumulator += delta;

    if (is_synchronized && sync_mode == SYNC_STRETCH) {
        target_tick_estimate += delta * double(tickrate);
        nudge_toward_estimate();
    }

    const int ceiling = simulation_gates > 0 ? 1 : max_ticks_per_frame;
    int ticks_this_frame = 0;
    const double step = ticktime();
    while (accumulator >= step && ticks_this_frame < ceiling) {
        accumulator -= step;
        emit_tick();
        ticks_this_frame += 1;
    }

    resolve_simulation_gate(ticks_this_frame);

    end_tick_loop();
}

void ClockEngine::set_manual_tick(bool value) {
    manual_tick = value;
    if (value) {
        is_synchronized = true;
    }
}

bool ClockEngine::get_manual_tick() const {
    return manual_tick;
}

void ClockEngine::set_enable_drift_logging(bool value) {
    enable_drift_logging = value;
}

bool ClockEngine::get_enable_drift_logging() const {
    return enable_drift_logging;
}

void ClockEngine::set_node_pumped(bool value) {
    node_pumped = value;
}

bool ClockEngine::get_node_pumped() const {
    return node_pumped;
}

void ClockEngine::poll_step() {
    const double span = seconds_since_step();
    if (node_pumped || manual_tick || !configured || span < 0.0) {
        mark_step();
        return;
    }
    physics_step(span);
}

void ClockEngine::count_poll() {
    polls += 1;
    if (cadence_started_usec == 0) {
        cadence_started_usec = wall_usec();
    }
}

Dictionary ClockEngine::cadence() const {
    const double wall = cadence_started_usec == 0
        ? 0.0
        : double(wall_usec() - cadence_started_usec) / 1'000'000.0;
    Dictionary out;
    out[StringName("physics_frames")] = physics_frames;
    out[StringName("polls")] = polls;
    out[StringName("wall_seconds")] = wall;
    out[StringName("physics_hz")]
        = wall > 0.0 ? double(physics_frames) / wall : 0.0;
    out[StringName("poll_hz")] = wall > 0.0 ? double(polls) / wall : 0.0;
    return out;
}

void ClockEngine::begin_tick_loop() {
    announce(SIG_BEFORE_TICK_LOOP);
}

void ClockEngine::end_tick_loop() {
    announce(SIG_AFTER_TICK_LOOP);
}

void ClockEngine::resolve_simulation_gate(int ticks_this_frame) {
    if (simulation_gates <= 0) {
        simulating = true;
        simulation_credit = 0;
        return;
    }
    if (accumulator >= ticktime()) {
        simulation_behind_count += 1;
    }
    simulation_credit += ticks_this_frame * physics_steps_per_tick();
    simulating = simulation_credit > 0;
    if (simulating) {
        simulation_credit -= 1;
    }
}

void ClockEngine::arm_gate() {
    simulation_gates += 1;
}

void ClockEngine::release_gate() {
    simulation_gates = simulation_gates > 1 ? simulation_gates - 1 : 0;
    if (simulation_gates == 0) {
        simulating = true;
        simulation_credit = 0;
    }
}

bool ClockEngine::is_gated() const {
    return simulation_gates > 0;
}

Dictionary ClockEngine::handle_pong(
    double sample,
    int server_tick_at_pong,
    double server_tick_phase,
    bool apply_lead
) {
    const bool was_stable = stats.is_stable;

    stats.record(sample, jitter_stability_threshold, jitter_window);

    if (stats.is_stable != was_stable) {
        announce(SIG_STABILITY_CHANGED, stats.is_stable);
    }

    const double lead
        = apply_lead ? stats.avg * 0.5 / ticktime() + lead_ticks : 0.0;
    const double target
        = double(server_tick_at_pong) + server_tick_phase + lead;
    const int pre_calibrate_diff = int(std::lround(target)) - tick;

    calibrate(target);
    notify_display_offset();

    Dictionary metrics;
    metrics["rtt_raw"] = sample;
    metrics["rtt_avg"] = stats.avg;
    metrics["rtt_jitter"] = stats.jitter;
    metrics["diff"] = pre_calibrate_diff;
    metrics["tick"] = tick;
    metrics["display_offset"] = display_offset;
    metrics["recommended_display_offset"] = recommended_display_offset();
    metrics["is_stable"] = stats.is_stable;
    metrics["is_synchronized"] = is_synchronized;
    return metrics;
}

void ClockEngine::calibrate(double target) {
    const int whole = int(std::floor(target));

    if (!is_synchronized) {
        tick = whole;
        accumulator = (target - double(whole)) * ticktime();
        target_tick_estimate = target;
        is_synchronized = true;
        announce(SIG_CLOCK_SYNCHRONIZED);
        return;
    }

    if (sync_mode == SYNC_SNAP) {
        tick = whole;
        accumulator = (target - double(whole)) * ticktime();
    } else {
        target_tick_estimate = target;
    }
}

void ClockEngine::nudge_toward_estimate() {
    const double step = ticktime();
    const double current = double(tick) + accumulator / step;
    const double divergence = target_tick_estimate - current;

    if (std::fabs(divergence) > double(panic_snap_threshold)) {
        tick = int(std::lround(target_tick_estimate));
        accumulator = (target_tick_estimate - double(tick)) * step;
        return;
    }

    accumulator += divergence * step * stretch_nudge_factor;
}

void ClockEngine::notify_display_offset() {
    const bool insufficient = recommended_display_offset() > display_offset;
    if (insufficient && !display_offset_insufficient_latched) {
        display_offset_insufficient_latched = true;
        announce(
            SIG_DISPLAY_OFFSET_INSUFFICIENT,
            recommended_display_offset()
        );
    } else if (!insufficient && display_offset_insufficient_latched) {
        display_offset_insufficient_latched = false;
    }
}

bool ClockEngine::consume_ping_due(double delta) {
    ping_timer += delta;
    if (ping_timer >= ping_interval) {
        ping_timer = 0.0;
        return true;
    }
    return false;
}

void ClockEngine::clear() {
    tick = 0;
    is_synchronized = false;
    simulating = true;
    simulation_behind_count = 0;
    accumulator = 0.0;
    target_tick_estimate = 0.0;
    ping_timer = 0.0;
    display_offset_insufficient_latched = false;
    step_stamp_usec = NEVER_STAMPED;
    simulation_gates = 0;
    simulation_credit = 0;
    node_pumped = false;
    physics_frames = 0;
    polls = 0;
    cadence_started_usec = 0;
    stats.clear();
}

int64_t ClockEngine::pumps_for(double seconds, double rate) {
    return int64_t(std::ceil(seconds * std::max(1.0, rate)));
}

} // namespace netw

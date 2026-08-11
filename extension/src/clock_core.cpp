#include "netw/clock_core.hpp"

#include <cmath>

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

// Signal names, spelled once so a rename is one edit rather than nine.
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

} // namespace

void NetwClockCore::Stats::record(
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

void NetwClockCore::Stats::clear() {
    rtt = 0.0;
    avg = 0.0;
    jitter = 0.0;
    is_stable = true;
    samples.clear();
}

#define NETW_CLOCK_ACCESSOR(m_type, m_name, m_field) \
    void NetwClockCore::set_##m_name(m_type value) { \
        m_field = value; \
    } \
    m_type NetwClockCore::get_##m_name() const { \
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

#undef NETW_CLOCK_ACCESSOR

void NetwClockCore::set_sync_mode(SyncMode value) {
    sync_mode = value;
}

NetwClockCore::SyncMode NetwClockCore::get_sync_mode() const {
    return sync_mode;
}

double NetwClockCore::ticktime() const {
    return 1.0 / double(tickrate);
}

int NetwClockCore::display_tick() const {
    const int shown = tick - display_offset;
    return shown > 0 ? shown : 0;
}

double NetwClockCore::physics_factor() const {
    return double(physics_ticks_per_second()) / double(tickrate);
}

int NetwClockCore::physics_steps_per_tick() const {
    const int steps = int(std::lround(physics_factor()));
    return steps > 1 ? steps : 1;
}

double NetwClockCore::tick_phase() const {
    const double phase = accumulator / ticktime();
    if (phase < 0.0) {
        return 0.0;
    }
    return phase > 1.0 ? 1.0 : phase;
}

double NetwClockCore::tick_accumulator() const {
    return accumulator;
}

double NetwClockCore::rtt() const {
    return stats.rtt;
}

double NetwClockCore::rtt_avg() const {
    return stats.avg;
}

double NetwClockCore::rtt_jitter() const {
    return stats.jitter;
}

double NetwClockCore::one_way_latency() const {
    return stats.avg * 0.5;
}

int NetwClockCore::recommended_display_offset() const {
    if (!is_synchronized) {
        return display_offset;
    }
    return int(std::ceil(
        (one_way_latency() + stats.jitter * jitter_multiplier)
        * double(tickrate)
    ));
}

bool NetwClockCore::is_stable() const {
    return stats.is_stable;
}

bool NetwClockCore::is_simulating() const {
    return simulating;
}

int NetwClockCore::get_simulation_behind_count() const {
    return simulation_behind_count;
}

void NetwClockCore::emit_tick() {
    NETW_ZONE_NC("NetwClockCore tick", colors::CLOCK);
    NETW_ZONE_VALUE(tick);
    NETW_TICK_MARK();
    const double step = ticktime();
    emit_signal(SIG_BEFORE_TICK, step, tick);
    emit_signal(SIG_ON_TICK, step, tick);
    emit_signal(SIG_AFTER_TICK, step, tick);
    tick += 1;
}

void NetwClockCore::force_step(int count) {
    for (int index = 0; index < count; ++index) {
        emit_tick();
    }
    // A stepped frame owes the same decision a pumped one makes, so a manual
    // frame that emitted no tick holds exactly as a pumped one would.
    resolve_simulation_gate(count);
}

void NetwClockCore::physics_step(double delta) {
    NETW_ZONE_NC("NetwClockCore physics step", colors::CLOCK);
    NETW_WARN_COND_ONCE(
        delta > stall_threshold,
        "clock",
        "Physics step %.3f exceeded stall threshold %.3f.",
        delta,
        stall_threshold
    );
    // A frame longer than the threshold is a hitch rather than simulated time,
    // so whatever was banked before it is discarded instead of paid out.
    if (delta > stall_threshold) {
        accumulator = 0.0;
        simulation_credit = 0;
    }

    emit_signal(SIG_BEFORE_TICK_LOOP);

    accumulator += delta;

    // Drift the estimate forward with the server, then close the residual gap a
    // fraction at a time so the playhead never teleports on a pong.
    if (is_synchronized && sync_mode == SYNC_STRETCH) {
        target_tick_estimate += delta * double(tickrate);
        nudge_toward_estimate();
    }

    // A gated clock emits at most one tick per frame, because a second tick in
    // one frame would have to share the single step the physics server runs and
    // the two would then mean different amounts of simulated time.
    const int ceiling = simulation_gates > 0 ? 1 : max_ticks_per_frame;
    int ticks_this_frame = 0;
    const double step = ticktime();
    while (accumulator >= step && ticks_this_frame < ceiling) {
        accumulator -= step;
        emit_tick();
        ticks_this_frame += 1;
    }

    resolve_simulation_gate(ticks_this_frame);

    emit_signal(SIG_AFTER_TICK_LOOP);
}

void NetwClockCore::resolve_simulation_gate(int ticks_this_frame) {
    if (simulation_gates <= 0) {
        simulating = true;
        simulation_credit = 0;
        return;
    }
    if (accumulator >= ticktime()) {
        // The loop wanted another tick and the ceiling refused it, so this peer
        // is not keeping up with the authority it tracks.
        simulation_behind_count += 1;
    }
    simulation_credit += ticks_this_frame * physics_steps_per_tick();
    simulating = simulation_credit > 0;
    if (simulating) {
        simulation_credit -= 1;
    }
}

void NetwClockCore::arm_gate() {
    simulation_gates += 1;
}

void NetwClockCore::release_gate() {
    simulation_gates = simulation_gates > 1 ? simulation_gates - 1 : 0;
    if (simulation_gates == 0) {
        simulating = true;
        simulation_credit = 0;
    }
}

bool NetwClockCore::is_gated() const {
    return simulation_gates > 0;
}

Dictionary NetwClockCore::handle_pong(
    double sample,
    int server_tick_at_pong,
    double server_tick_phase,
    bool apply_lead
) {
    const bool was_stable = stats.is_stable;

    stats.record(sample, jitter_stability_threshold, jitter_window);

    if (stats.is_stable != was_stable) {
        emit_signal(SIG_STABILITY_CHANGED, stats.is_stable);
    }

    const double lead
        = apply_lead ? stats.avg * 0.5 / ticktime() + lead_ticks : 0.0;
    // The target is a continuous clock position rather than a whole tick.
    // Rounding the phase away would make it jump by a full tick as the ping's
    // arrival phase slid across a server boundary, and STRETCH would then chase
    // that sawtooth for about a second at a time.
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

void NetwClockCore::calibrate(double target) {
    const int whole = int(std::floor(target));

    if (!is_synchronized) {
        // The first calibration hard-aligns so STRETCH begins already
        // converged, and it seeds the phase within the tick rather than landing
        // on the boundary below it.
        tick = whole;
        accumulator = (target - double(whole)) * ticktime();
        target_tick_estimate = target;
        is_synchronized = true;
        emit_signal(SIG_CLOCK_SYNCHRONIZED);
        return;
    }

    if (sync_mode == SYNC_SNAP) {
        tick = whole;
        accumulator = (target - double(whole)) * ticktime();
    } else {
        // Re-anchor the estimate to the fresh measurement. The tick loop nudges
        // the live clock toward it every frame.
        target_tick_estimate = target;
    }
}

void NetwClockCore::nudge_toward_estimate() {
    const double step = ticktime();
    const double current = double(tick) + accumulator / step;
    const double divergence = target_tick_estimate - current;

    // A large gap is a real desync rather than drift, so it snaps rather than
    // crawling, matching the panic path SNAP mode relies on.
    if (std::fabs(divergence) > double(panic_snap_threshold)) {
        tick = int(std::lround(target_tick_estimate));
        accumulator = (target_tick_estimate - double(tick)) * step;
        return;
    }

    accumulator += divergence * step * stretch_nudge_factor;
}

void NetwClockCore::notify_display_offset() {
    const bool insufficient = recommended_display_offset() > display_offset;
    if (insufficient && !display_offset_insufficient_latched) {
        display_offset_insufficient_latched = true;
        emit_signal(
            SIG_DISPLAY_OFFSET_INSUFFICIENT,
            recommended_display_offset()
        );
    } else if (!insufficient && display_offset_insufficient_latched) {
        display_offset_insufficient_latched = false;
    }
}

bool NetwClockCore::consume_ping_due(double delta) {
    ping_timer += delta;
    if (ping_timer >= ping_interval) {
        ping_timer = 0.0;
        return true;
    }
    return false;
}

void NetwClockCore::clear() {
    tick = 0;
    is_synchronized = false;
    simulating = true;
    simulation_behind_count = 0;
    accumulator = 0.0;
    target_tick_estimate = 0.0;
    ping_timer = 0.0;
    display_offset_insufficient_latched = false;
    simulation_gates = 0;
    simulation_credit = 0;
    stats.clear();
}

void NetwClockCore::_bind_methods() {
#define NETW_CLOCK_BIND(m_name, m_variant) \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, "value"), \
        &NetwClockCore::set_##m_name \
    ); \
    ClassDB::bind_method( \
        D_METHOD("get_" #m_name), \
        &NetwClockCore::get_##m_name \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(m_variant, #m_name), \
        "set_" #m_name, \
        "get_" #m_name \
    )

    NETW_CLOCK_BIND(tickrate, Variant::INT);
    NETW_CLOCK_BIND(max_ticks_per_frame, Variant::INT);
    NETW_CLOCK_BIND(stall_threshold, Variant::FLOAT);
    NETW_CLOCK_BIND(panic_snap_threshold, Variant::INT);
    NETW_CLOCK_BIND(stretch_nudge_factor, Variant::FLOAT);
    NETW_CLOCK_BIND(ping_interval, Variant::FLOAT);
    NETW_CLOCK_BIND(lead_ticks, Variant::FLOAT);
    NETW_CLOCK_BIND(display_offset, Variant::INT);
    NETW_CLOCK_BIND(jitter_multiplier, Variant::FLOAT);
    NETW_CLOCK_BIND(jitter_window, Variant::INT);
    NETW_CLOCK_BIND(jitter_stability_threshold, Variant::FLOAT);
    NETW_CLOCK_BIND(tick, Variant::INT);
    NETW_CLOCK_BIND(synchronized, Variant::BOOL);

#undef NETW_CLOCK_BIND

    ClassDB::bind_method(
        D_METHOD("set_sync_mode", "value"),
        &NetwClockCore::set_sync_mode
    );
    ClassDB::bind_method(
        D_METHOD("get_sync_mode"),
        &NetwClockCore::get_sync_mode
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "sync_mode",
            PROPERTY_HINT_ENUM,
            "Snap,Stretch"
        ),
        "set_sync_mode",
        "get_sync_mode"
    );

    ClassDB::bind_method(D_METHOD("ticktime"), &NetwClockCore::ticktime);
    ClassDB::bind_method(
        D_METHOD("display_tick"),
        &NetwClockCore::display_tick
    );
    ClassDB::bind_method(
        D_METHOD("physics_factor"),
        &NetwClockCore::physics_factor
    );
    ClassDB::bind_method(
        D_METHOD("physics_steps_per_tick"),
        &NetwClockCore::physics_steps_per_tick
    );
    ClassDB::bind_method(D_METHOD("tick_phase"), &NetwClockCore::tick_phase);
    ClassDB::bind_method(
        D_METHOD("tick_accumulator"),
        &NetwClockCore::tick_accumulator
    );
    ClassDB::bind_method(D_METHOD("rtt"), &NetwClockCore::rtt);
    ClassDB::bind_method(D_METHOD("rtt_avg"), &NetwClockCore::rtt_avg);
    ClassDB::bind_method(D_METHOD("rtt_jitter"), &NetwClockCore::rtt_jitter);
    ClassDB::bind_method(
        D_METHOD("one_way_latency"),
        &NetwClockCore::one_way_latency
    );
    ClassDB::bind_method(
        D_METHOD("recommended_display_offset"),
        &NetwClockCore::recommended_display_offset
    );
    ClassDB::bind_method(D_METHOD("is_stable"), &NetwClockCore::is_stable);

    ClassDB::bind_method(
        D_METHOD("physics_step", "delta"),
        &NetwClockCore::physics_step
    );
    ClassDB::bind_method(
        D_METHOD("force_step", "count"),
        &NetwClockCore::force_step
    );
    ClassDB::bind_method(
        D_METHOD("is_simulating"),
        &NetwClockCore::is_simulating
    );
    ClassDB::bind_method(
        D_METHOD("simulation_behind_count"),
        &NetwClockCore::get_simulation_behind_count
    );
    ClassDB::bind_method(D_METHOD("arm_gate"), &NetwClockCore::arm_gate);
    ClassDB::bind_method(
        D_METHOD("release_gate"),
        &NetwClockCore::release_gate
    );
    ClassDB::bind_method(D_METHOD("is_gated"), &NetwClockCore::is_gated);
    ClassDB::bind_method(
        D_METHOD(
            "handle_pong",
            "sample",
            "server_tick_at_pong",
            "server_tick_phase",
            "apply_lead"
        ),
        &NetwClockCore::handle_pong
    );
    ClassDB::bind_method(
        D_METHOD("consume_ping_due", "delta"),
        &NetwClockCore::consume_ping_due
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwClockCore::clear);

    ADD_SIGNAL(MethodInfo(
        SIG_BEFORE_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_ON_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_AFTER_TICK,
        PropertyInfo(Variant::FLOAT, "delta"),
        PropertyInfo(Variant::INT, "tick")
    ));
    ADD_SIGNAL(MethodInfo(SIG_BEFORE_TICK_LOOP));
    ADD_SIGNAL(MethodInfo(SIG_AFTER_TICK_LOOP));
    ADD_SIGNAL(MethodInfo(SIG_CLOCK_SYNCHRONIZED));
    ADD_SIGNAL(MethodInfo(
        SIG_STABILITY_CHANGED,
        PropertyInfo(Variant::BOOL, "is_stable")
    ));
    ADD_SIGNAL(MethodInfo(
        SIG_DISPLAY_OFFSET_INSUFFICIENT,
        PropertyInfo(Variant::INT, "recommended")
    ));

    BIND_ENUM_CONSTANT(SYNC_SNAP);
    BIND_ENUM_CONSTANT(SYNC_STRETCH);
}

} // namespace netw

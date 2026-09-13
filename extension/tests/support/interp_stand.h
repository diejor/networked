#pragma once

#include "netw_test.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"

#include <godot_cpp/classes/random_number_generator.hpp>

#include "netw/api/interpolate.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/pump.hpp"
#include "netw/display/pump_stats.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"
#include "netw/display/tracks.hpp"

namespace netw_test {

struct Oracle {
    enum Kind {
        LINEAR,
        CIRCLE,
        STATIONARY,
    };

    const char *label = "linear";
    Kind kind = LINEAR;
    godot::Vector2 origin;
    godot::Vector2 velocity = godot::Vector2(120.0, 0.0);
    double radius = 100.0;
    double omega = 2.0;

    godot::Vector2 value_at(double p_t) const {
        switch (kind) {
            case CIRCLE:
                return origin
                    + godot::Vector2(
                          std::cos(omega * p_t),
                          std::sin(omega * p_t)
                      )
                    * radius;
            case STATIONARY:
                return origin;
            default:
                return origin + velocity * p_t;
        }
    }

    double speed_max() const {
        switch (kind) {
            case CIRCLE:
                return radius * std::abs(omega);
            case STATIONARY:
                return 0.0;
            default:
                return double(velocity.length());
        }
    }

    double accel_max() const {
        return kind == CIRCLE ? radius * omega * omega : 0.0;
    }

    bool is_constant_velocity() const {
        return kind == LINEAR;
    }
};

inline Oracle linear_truth() {
    Oracle truth;
    truth.label = "linear";
    truth.kind = Oracle::LINEAR;
    truth.velocity = godot::Vector2(120.0, 0.0);
    return truth;
}

inline Oracle circle_truth() {
    Oracle truth;
    truth.label = "circle";
    truth.kind = Oracle::CIRCLE;
    truth.radius = 100.0;
    truth.omega = 2.0;
    return truth;
}

inline Oracle stationary_truth() {
    Oracle truth;
    truth.label = "stationary";
    truth.kind = Oracle::STATIONARY;
    truth.origin = godot::Vector2(10.0, 5.0);
    return truth;
}

struct LinkPreset {
    const char *label = "perfect";
    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double packet_loss = 0.0;
    double reorder = 0.0;
};

inline LinkPreset perfect_link() {
    return LinkPreset{"perfect", 0.0, 0.0, 0.0, 0.0};
}

inline LinkPreset wifi_link() {
    return LinkPreset{"wifi", 35.0, 8.0, 0.01, 0.005};
}

inline LinkPreset mobile_4g_link() {
    return LinkPreset{"mobile_4g", 85.0, 25.0, 0.03, 0.02};
}

inline LinkPreset poor_3g_link() {
    return LinkPreset{"poor_3g", 220.0, 90.0, 0.08, 0.05};
}

inline LinkPreset satellite_link() {
    return LinkPreset{"satellite", 650.0, 120.0, 0.04, 0.03};
}

inline LinkPreset heavy_loss_link() {
    return LinkPreset{"loss20", 40.0, 10.0, 0.20, 0.02};
}

struct Arrival {
    double arrival_sec = 0.0;
    int64_t tick = 0;
    godot::Vector2 value;
};

inline std::vector<Arrival> build_schedule(
    const Oracle &p_truth,
    const LinkPreset &p_link,
    double p_tickrate,
    int p_send_period,
    double p_duration_sec,
    int64_t p_seed
) {
    godot::Ref<godot::RandomNumberGenerator> rng;
    rng.instantiate();
    rng->set_seed(uint64_t(p_seed));

    std::vector<Arrival> out;
    const double ticktime = 1.0 / p_tickrate;
    const double latency_sec = p_link.latency_ms * 0.001;
    const double jitter_sec = p_link.jitter_ms * 0.001;
    const int period = std::max(1, p_send_period);
    for (int64_t tick = 0; double(tick) * ticktime <= p_duration_sec;
         tick += period) {
        if (double(rng->randf()) < p_link.packet_loss) {
            continue;
        }
        double jitter = 0.0;
        if (jitter_sec > 0.0) {
            jitter = double(rng->randf_range(-jitter_sec, jitter_sec));
        }
        double arrival = double(tick) * ticktime + latency_sec + jitter;
        if (p_link.reorder > 0.0 && double(rng->randf()) < p_link.reorder) {
            arrival += double(period) * ticktime;
        }
        Arrival landed;
        landed.arrival_sec = std::max(arrival, double(tick) * ticktime);
        landed.tick = tick;
        landed.value = p_truth.value_at(double(tick) * ticktime);
        out.push_back(landed);
    }
    std::stable_sort(
        out.begin(),
        out.end(),
        [](const Arrival &a, const Arrival &b) {
            return a.arrival_sec < b.arrival_sec;
        }
    );
    return out;
}

struct WriteLog {
    std::vector<godot::Vector2> samples;
    std::vector<double> frame_times;
    double stamp = 0.0;
};

class RecordingWriter final : public godot::CallableCustom {
    std::shared_ptr<WriteLog> log;
    godot::ObjectID anchor;

    static bool same(
        const godot::CallableCustom *a,
        const godot::CallableCustom *b
    ) {
        return a == b;
    }

    static bool before(
        const godot::CallableCustom *a,
        const godot::CallableCustom *b
    ) {
        return a < b;
    }

public:
    RecordingWriter(
        const std::shared_ptr<WriteLog> &p_log,
        const godot::Object *p_anchor
    )
        : log(p_log), anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    godot::String get_as_text() const override {
        return godot::String("NetwInterpRecordingWriter");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &RecordingWriter::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &RecordingWriter::before;
    }

    godot::ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const godot::Variant **p_arguments,
        int p_count,
        godot::Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        if (p_count > 0) {
            log->samples.push_back(godot::Vector2(*p_arguments[0]));
            log->frame_times.push_back(log->stamp);
        }
        r_return_value = godot::Variant();
        netw::gd::call_ok(r_call_error);
    }
};

struct InterpHarness {
    double tickrate = 60.0;
    double fps = 60.0;
    int send_period = 1;
    int timeline_mode = netw::display::TIMELINE_BUFFERED;
    int max_forecast_ticks = 6;
    int pump_mode = netw::display::PUMP_REMOTE;
    double record_cutoff_sec = INFINITY;

    std::vector<double> frames;
    std::vector<godot::Vector2> displayed;
    std::vector<double> playhead_time;
    netw::display::PumpStats run_stats;
    int display_offset = 0;
    int recommended_display_offset = 0;

    std::shared_ptr<WriteLog> written = std::make_shared<WriteLog>();

    InterpHarness() {
        anchor.instantiate();
    }

    ~InterpHarness() {
        if (body != nullptr) {
            memdelete(body);
        }
        if (runtime != nullptr) {
            godot::memdelete(runtime);
        }
    }

    double frame_dt() const {
        return 1.0 / fps;
    }

    double warmup_hint() const {
        return double(display_offset) / tickrate + 0.5;
    }

    void configure(double p_smoothing, const godot::Vector2 &p_initial) {
        build_runtime(pump_mode);
        config.set_param(netw::display::PARAM_TIMELINE_MODE, timeline_mode);
        config.set_param(
            netw::display::PARAM_MAX_FORECAST_TICKS,
            max_forecast_ticks
        );
        runtime->set_config(config);
        runtime->display_playhead().set_expected_interval_ticks(
            std::max(1, send_period)
        );
        attach(p_smoothing, p_initial);
        runtime->display_tracks().declare(
            channel->get_state_key(),
            channel->get_name()
        );
    }

    void configure_chase(
        double p_smooth_time,
        const godot::Vector2 &p_initial
    ) {
        build_runtime(netw::display::PUMP_CHASE);
        config.set_param(
            netw::display::PARAM_PREDICTED_SMOOTH_TIME,
            p_smooth_time
        );
        runtime->set_config(config);
        body = memnew(godot::Node2D);
        body->set_position(p_initial);
        attach(0.0, p_initial);
        channel->set_source_obj(body);
    }

    void set_body(const godot::Vector2 &p_value) const {
        body->set_position(p_value);
    }

    void absorb_recovery(const godot::Vector2 &p_delta, bool p_teleported) {
        godot::Dictionary deltas;
        deltas[godot::StringName("position")] = p_delta;
        netw::display::absorb_recovery(
            runtime,
            deltas,
            p_teleported,
            netw::display::Hooks()
        );
    }

    void step_chase(double p_wall) {
        pump_at(p_wall);
        frames.push_back(p_wall);
        displayed.push_back(godot::Vector2(channel->get_last_written()));
        playhead_time.push_back(p_wall);
    }

    void run_chase(const Oracle &p_truth, double p_duration_sec) {
        clear_series();
        const int frame_count = int(std::ceil(p_duration_sec * fps));
        for (int at = 0; at < frame_count; ++at) {
            const double wall = double(at) / fps;
            const int64_t tick = int64_t(std::floor(wall * tickrate));
            body->set_position(p_truth.value_at(double(tick) / tickrate));
            step_chase(wall);
        }
    }

    void run(
        const Oracle &p_truth,
        const LinkPreset &p_link,
        double p_duration_sec,
        int64_t p_seed
    ) {
        const double latency_ticks = p_link.latency_ms * 0.001 * tickrate;
        const double jitter_ticks = p_link.jitter_ms * 0.001 * tickrate;
        display_offset = int(std::ceil(latency_ticks));
        recommended_display_offset
            = display_offset + int(std::ceil(jitter_ticks));

        const std::vector<Arrival> schedule = build_schedule(
            p_truth,
            p_link,
            tickrate,
            send_period,
            p_duration_sec,
            p_seed
        );

        runtime->display_playhead().settle(
            runtime->get_config(),
            display_offset,
            recommended_display_offset
        );

        clear_series();
        run_stats.reset();

        size_t next = 0;
        const int frame_count = int(std::ceil(p_duration_sec * fps));
        for (int at = 0; at < frame_count; ++at) {
            const double wall = double(at) / fps;
            while (next < schedule.size()
                   && schedule[next].arrival_sec <= wall) {
                if (schedule[next].arrival_sec <= record_cutoff_sec) {
                    channel->display_history().record(
                        schedule[next].tick,
                        schedule[next].value,
                        false
                    );
                }
                ++next;
            }
            const netw::display::Timing timing = pump_at(wall);
            run_stats.merge(stats);
            frames.push_back(wall);
            displayed.push_back(godot::Vector2(channel->get_last_written()));
            playhead_time.push_back(
                double(timing.display_tick) + timing.tick_factor
                - runtime->display_playhead().get_display_lag()
            );
        }
    }

private:
    netw::display::Runtime *runtime = nullptr;
    netw::display::Channel *channel = nullptr;
    netw::display::PumpStats stats;
    godot::Ref<godot::RefCounted> anchor;
    godot::Node2D *body = nullptr;
    netw::display::Decl config;

    void clear_series() {
        frames.clear();
        displayed.clear();
        playhead_time.clear();
    }

    void build_runtime(int p_mode) {
        if (runtime != nullptr) {
            godot::memdelete(runtime);
        }
        runtime = memnew(netw::display::Runtime);
        config = netw::display::Decl();
        runtime->set_config(config);
        runtime->set_pump_mode(p_mode);
    }

    void attach(double p_smoothing, const godot::Vector2 &p_initial) {
        godot::Ref<netw::NetwInterpolate> spec;
        spec.instantiate();
        spec->lerp();
        spec->smooth(p_smoothing);
        spec->to(godot::StringName("position"));

        channel = runtime->add_channel();
        channel->set_name(godot::StringName("position"));
        channel->set_state_key(godot::StringName("Body:position"));
        channel->set_spec(spec);
        channel->set_source_prop(godot::StringName("position"));
        channel->set_target_prop(godot::StringName("position"));
        netw::display::History &history = channel->display_history();
        history.set_mode(spec->get_mode());
        history.set_snap_distance(spec->get_snap_distance());
        channel->set_output(
            godot::Callable(memnew(RecordingWriter(written, anchor.ptr())))
        );
        channel->set_last_written(p_initial);
    }

    netw::display::Timing pump_at(double p_wall) {
        const netw::display::Timing timing = make_timing(p_wall);
        written->stamp = p_wall;
        stats.reset();
        netw::display::pump_runtime(
            runtime,
            timing,
            stats,
            netw::display::Hooks()
        );
        return timing;
    }

    netw::display::Timing make_timing(double p_wall) const {
        netw::display::Timing timing;
        const double sim_ticks = p_wall * tickrate;
        const double display_time = sim_ticks - double(display_offset);
        timing.tick = int(std::floor(sim_ticks));
        timing.display_tick = int(std::floor(display_time));
        timing.tick_factor
            = display_time - double(int(std::floor(display_time)));
        timing.ticktime = 1.0 / tickrate;
        timing.display_offset = display_offset;
        timing.recommended_display_offset = recommended_display_offset;
        timing.frame_delta = 1.0 / fps;
        timing.frame_ticks = tickrate / fps;
        return timing;
    }
};

struct Metrics {
    int regressions = 0;
    int stalls = 0;
    int moved = 0;
    int samples = 0;
    double max_step = 0.0;
    double mean_speed = 0.0;
    bool playhead_monotonic = true;
    double max_playhead_backstep = 0.0;
    double min_lag_sec = 0.0;
    double max_lag_sec = 0.0;
    bool led_truth = false;
    double max_jerk = 0.0;
};

inline Metrics analyze(
    const InterpHarness &p_run,
    const Oracle &p_truth,
    double p_warmup_sec
) {
    Metrics out;
    godot::Vector2 dir;
    if (p_truth.kind == Oracle::LINEAR) {
        dir = p_truth.velocity.normalized();
    }
    const double speed_eps = std::max(p_truth.speed_max() * 0.02, 0.001);
    const double lag_speed = double(p_truth.velocity.length());
    const double frame_dt = p_run.frame_dt();

    bool first = true;
    bool lag_first = true;
    bool have_step_prev = false;
    double speed_sum = 0.0;
    godot::Vector2 step_prev;
    godot::Vector2 prev_disp;
    double prev_playhead = 0.0;

    for (size_t at = 0; at < p_run.frames.size(); ++at) {
        const double t = p_run.frames[at];
        const godot::Vector2 disp = p_run.displayed[at];
        const double ph = p_run.playhead_time[at];
        if (t < p_warmup_sec) {
            prev_disp = disp;
            prev_playhead = ph;
            first = false;
            continue;
        }
        if (!first) {
            const godot::Vector2 step = disp - prev_disp;
            const double step_len = double(step.length());
            out.max_step = std::max(out.max_step, step_len);
            out.samples += 1;
            speed_sum += step_len / std::max(frame_dt, 0.0001);

            if (p_truth.speed_max() > speed_eps) {
                if (step_len <= speed_eps * frame_dt) {
                    out.stalls += 1;
                } else {
                    out.moved += 1;
                }
            }
            if (dir != godot::Vector2()
                && step.dot(dir) < -speed_eps * frame_dt) {
                out.regressions += 1;
            }
            if (ph < prev_playhead - 1e-6) {
                out.playhead_monotonic = false;
                out.max_playhead_backstep
                    = std::max(out.max_playhead_backstep, prev_playhead - ph);
            }
            if (have_step_prev) {
                out.max_jerk = std::max(
                    out.max_jerk,
                    double((step - step_prev).length())
                );
            }
            step_prev = step;
            have_step_prev = true;
        }

        if (p_truth.is_constant_velocity() && lag_speed > 0.0) {
            const double s
                = double((disp - p_truth.origin).dot(p_truth.velocity))
                / double(p_truth.velocity.length_squared());
            const double lag = t - s;
            if (lag_first) {
                out.min_lag_sec = lag;
                out.max_lag_sec = lag;
                lag_first = false;
            } else {
                out.min_lag_sec = std::min(out.min_lag_sec, lag);
                out.max_lag_sec = std::max(out.max_lag_sec, lag);
            }
            if (lag < -0.01) {
                out.led_truth = true;
            }
        }

        prev_disp = disp;
        prev_playhead = ph;
        first = false;
    }

    if (out.samples > 0) {
        out.mean_speed = speed_sum / double(out.samples);
    }
    return out;
}

struct LaneDecl {
    Oracle truth;
    LinkPreset link;
    int64_t seed = 0;
};

struct ScheduleHarness {
    double tickrate = 60.0;
    double fps = 60.0;
    int send_period = 1;
    netw::display::PumpStats run_stats;

    struct Lane {
        LaneDecl decl;
        std::shared_ptr<netw::display::Runtime> runtime;
        netw::display::Channel *channel = nullptr;
        std::shared_ptr<WriteLog> written = std::make_shared<WriteLog>();
        std::vector<Arrival> schedule;
        size_t next = 0;
        int display_offset = 0;
        int recommended_display_offset = 0;
        std::vector<godot::Vector2> displayed;
    };

    std::vector<Lane> lanes;

    ScheduleHarness() {
        anchor.instantiate();
    }

    void add_lane(const LaneDecl &p_decl) {
        Lane lane;
        lane.decl = p_decl;
        lanes.push_back(lane);
    }

    void run(double p_duration_sec, const std::vector<int> &p_order) {
        run_stats.reset();
        for (Lane &lane : lanes) {
            stand_up(lane, p_duration_sec);
        }
        const int frame_count = int(std::ceil(p_duration_sec * fps));
        for (int at = 0; at < frame_count; ++at) {
            const double wall = double(at) / fps;
            for (Lane &lane : lanes) {
                while (lane.next < lane.schedule.size()
                       && lane.schedule[lane.next].arrival_sec <= wall) {
                    lane.channel->display_history().record(
                        lane.schedule[lane.next].tick,
                        lane.schedule[lane.next].value,
                        false
                    );
                    ++lane.next;
                }
            }
            for (int index : p_order) {
                Lane &lane = lanes[size_t(index)];
                lane.written->stamp = wall;
                stats.reset();
                netw::display::pump_runtime(
                    lane.runtime.get(),
                    timing_for(lane, wall),
                    stats,
                    netw::display::Hooks()
                );
                run_stats.merge(stats);
                lane.displayed.push_back(
                    godot::Vector2(lane.channel->get_last_written())
                );
            }
        }
    }

private:
    netw::display::PumpStats stats;
    godot::Ref<godot::RefCounted> anchor;

    void stand_up(Lane &r_lane, double p_duration_sec) {
        const double latency_ticks
            = r_lane.decl.link.latency_ms * 0.001 * tickrate;
        const double jitter_ticks
            = r_lane.decl.link.jitter_ms * 0.001 * tickrate;
        r_lane.display_offset = int(std::ceil(latency_ticks));
        r_lane.recommended_display_offset
            = r_lane.display_offset + int(std::ceil(jitter_ticks));

        r_lane.runtime = std::make_shared<netw::display::Runtime>();
        const netw::display::Decl decl;
        r_lane.runtime->set_config(decl);
        r_lane.runtime->set_pump_mode(netw::display::PUMP_REMOTE);
        netw::display::Playhead &playhead = r_lane.runtime->display_playhead();
        playhead.set_expected_interval_ticks(std::max(1, send_period));

        godot::Ref<netw::NetwInterpolate> spec;
        spec.instantiate();
        spec->lerp();
        spec->smooth(0.05);
        spec->to(godot::StringName("position"));

        r_lane.channel = r_lane.runtime->add_channel();
        r_lane.channel->set_name(godot::StringName("position"));
        r_lane.channel->set_state_key(godot::StringName("Body:position"));
        r_lane.channel->set_spec(spec);
        r_lane.channel->set_source_prop(godot::StringName("position"));
        r_lane.channel->set_target_prop(godot::StringName("position"));
        r_lane.channel->display_history().set_mode(spec->get_mode());
        r_lane.channel->set_output(
            godot::Callable(
                memnew(RecordingWriter(r_lane.written, anchor.ptr()))
            )
        );
        r_lane.channel->set_last_written(r_lane.decl.truth.value_at(0.0));

        r_lane.schedule = build_schedule(
            r_lane.decl.truth,
            r_lane.decl.link,
            tickrate,
            send_period,
            p_duration_sec,
            r_lane.decl.seed
        );
        r_lane.next = 0;
        r_lane.displayed.clear();
        playhead.settle(
            decl,
            r_lane.display_offset,
            r_lane.recommended_display_offset
        );
    }

    netw::display::Timing timing_for(const Lane &p_lane, double p_wall) const {
        netw::display::Timing timing;
        const double sim_ticks = p_wall * tickrate;
        const double display_time = sim_ticks - double(p_lane.display_offset);
        timing.tick = int(std::floor(sim_ticks));
        timing.display_tick = int(std::floor(display_time));
        timing.tick_factor
            = display_time - double(int(std::floor(display_time)));
        timing.ticktime = 1.0 / tickrate;
        timing.display_offset = p_lane.display_offset;
        timing.recommended_display_offset = p_lane.recommended_display_offset;
        timing.frame_delta = 1.0 / fps;
        timing.frame_ticks = tickrate / fps;
        return timing;
    }
};

inline bool same_stats(
    const netw::display::PumpStats &p_a,
    const netw::display::PumpStats &p_b
) {
    return p_a.runtimes == p_b.runtimes && p_a.starving == p_b.starving
        && p_a.sleeping == p_b.sleeping && p_a.projecting == p_b.projecting
        && p_a.snaps == p_b.snaps
        && std::abs(p_a.max_display_lag - p_b.max_display_lag) < 1e-9
        && std::abs(p_a.max_forecast_age - p_b.max_forecast_age) < 1e-9;
}

inline int first_difference(
    const std::vector<godot::Vector2> &p_a,
    const std::vector<godot::Vector2> &p_b
) {
    if (p_a.size() != p_b.size()) {
        return -2;
    }
    for (size_t at = 0; at < p_a.size(); ++at) {
        if (p_a[at] != p_b[at]) {
            return int(at);
        }
    }
    return -1;
}

} // namespace netw_test

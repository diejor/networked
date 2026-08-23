#include "netw/display_pump.hpp"

#include <algorithm>
#include <cmath>

#include "netw/display_channel.hpp"
#include "netw/api/display_decl.hpp"
#include "netw/display_history.hpp"
#include "netw/display_role_facts.hpp"
#include "netw/display_playhead.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/log.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace display {

void DisplayHooks::resolve(const Ref<NetwDisplayRuntime> &p_runtime) const {
    if (resolve_role.is_valid()) {
        resolve_role.call(p_runtime);
    }
}

double DisplayHooks::clamp_for(const Ref<NetwDisplayRuntime> &p_runtime) const {
    if (!chase_clamp.is_valid()) {
        return INFINITY;
    }
    return double(chase_clamp.call(p_runtime));
}

Array DisplayHooks::specs_of(Node *p_owner) const {
    if (!spec_reader.is_valid() || p_owner == nullptr) {
        return Array();
    }
    return spec_reader.call(p_owner);
}

void DisplayHooks::compute_sync_intervals(
    const Ref<NetwDisplayRuntime> &p_runtime
) const {
    if (sync_intervals.is_valid()) {
        sync_intervals.call(p_runtime);
    }
}

bool DisplayHooks::authors(const Ref<NetwDisplayRuntime> &p_runtime) const {
    if (!authors_streams.is_valid()) {
        return false;
    }
    return authors_streams.call(p_runtime);
}

Ref<NetwDisplayRoleFacts> DisplayHooks::role_facts(
    const Ref<NetwDisplayRuntime> &p_runtime,
    bool p_authors_streams
) const {
    if (!role_facts_reader.is_valid()) {
        return Ref<NetwDisplayRoleFacts>();
    }
    return role_facts_reader.call(p_runtime, p_authors_streams);
}

void DisplayHooks::chase_hook(
    const Ref<NetwDisplayRuntime> &p_runtime,
    bool p_bind
) const {
    if (chase_hook_binder.is_valid()) {
        chase_hook_binder.call(p_runtime, p_bind);
    }
}

double glide(const Ref<NetwDisplayRuntime> &p_runtime, double p_frame_delta) {
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    if (config.is_null()) {
        return 1.0;
    }
    return std::exp(
        -p_frame_delta / std::max(config->get_chase_glide_time(), 0.001)
    );
}

double chase_smooth_time(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing
) {
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    if (config.is_valid() && config->get_predicted_smooth_time() > 0.0) {
        return config->get_predicted_smooth_time();
    }
    if (p_timing->get_ticktime() > 0.0) {
        return std::max(p_timing->get_ticktime() * 0.85, 0.001);
    }
    return 1.0 / 60.0;
}

bool take_trace_frame(const Ref<NetwDisplayRuntime> &p_runtime) {
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    if (config.is_null() || config->get_trace_interval() <= 0) {
        return false;
    }
    const int64_t next
        = (p_runtime->get_trace_frame() + 1) % config->get_trace_interval();
    p_runtime->set_trace_frame(next);
    return next == 0;
}

void dilate_playhead(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing,
    const Ref<NetwPumpStats> &p_stats,
    bool p_trace
) {
    const Ref<NetwDisplayPlayhead> playhead = p_runtime->get_playhead();
    const int effective
        = playhead->effective_tick(p_timing->get_display_tick());
    const TypedArray<NetwDisplayChannel> states = p_runtime->get_states();

    bool starving = false;
    int64_t newest = -1;
    for (int at = 0; at < states.size(); ++at) {
        const Ref<NetwDisplayChannel> state = states[at];
        const Ref<NetwDisplayHistory> history = state->get_history();
        if (history.is_null() || history->is_empty()) {
            continue;
        }
        if (!history->has_tick_after(effective)) {
            starving = true;
            newest = history->newest_tick();
            break;
        }
    }

    if (starving) {
        p_stats->set_starving(p_stats->get_starving() + 1);
        for (int at = 0; at < states.size(); ++at) {
            const Ref<NetwDisplayChannel> state = states[at];
            const Ref<NetwDisplayHistory> history = state->get_history();
            if (history.is_valid()) {
                history->set_sleeping(false);
            }
        }
    }

    playhead->dilate(
        p_runtime->get_config(),
        p_timing->get_frame_ticks(),
        p_timing->get_display_offset(),
        p_timing->get_recommended_display_offset(),
        starving
    );

    if (p_trace) {
        NETW_TRACE(
            sys::INTERPOLATION,
            "dilation dt=%d newest=%d starving=%d lag=%.2f",
            effective,
            int(newest),
            int(starving),
            playhead->get_display_lag()
        );
    }
}

void pump_history(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing,
    const Ref<NetwPumpStats> &p_stats
) {
    const Ref<NetwDisplayPlayhead> playhead = p_runtime->get_playhead();
    const Ref<NetwDisplayDecl> config = p_runtime->get_config();
    const bool trace = take_trace_frame(p_runtime);
    const bool bracketed
        = p_runtime->get_pump_mode() != NetwDisplayDecl::PUMP_REMOTE;

    bool forecast = false;
    int64_t max_forecast_ticks = 0;
    if (!bracketed) {
        forecast = config.is_valid()
            && config->get_timeline_mode() == NetwDisplayDecl::TIMELINE_FORECAST;
        if (config.is_valid()) {
            max_forecast_ticks = config->get_max_forecast_ticks();
        }
        if (config.is_valid() && config->get_enable_smart_dilation()) {
            dilate_playhead(p_runtime, p_timing, p_stats, trace);
        } else {
            playhead->set_display_lag(0.0);
        }
    }

    const double factor = playhead->place(
        p_timing->get_display_tick(),
        p_timing->get_tick_factor(),
        bracketed
    );
    const int64_t dt = playhead->get_display_tick();
    p_stats->set_max_display_lag(
        std::max(p_stats->get_max_display_lag(), playhead->get_display_lag())
    );

    const int64_t eit = playhead->get_expected_interval_ticks();
    const double decay = glide(p_runtime, p_timing->get_frame_delta());
    const TypedArray<NetwDisplayChannel> states = p_runtime->get_states();
    const Ref<NetwDisplayTracks> tracks = p_runtime->get_tracks();

    for (int at = 0; at < states.size(); ++at) {
        const Ref<NetwDisplayChannel> state = states[at];
        if (state->get_self_feedback()
            && p_runtime->get_pump_mode() == NetwDisplayDecl::PUMP_BRACKETED) {
            continue;
        }
        const Ref<NetwDisplayHistory> history = state->get_history();
        const Ref<NetwInterpolate> spec = state->get_spec();
        const int verdict = history->pass_verdict(spec, forecast);
        if (verdict == NetwDisplayHistory::PASS_SKIP_SLEEPING) {
            p_stats->set_sleeping(p_stats->get_sleeping() + 1);
            continue;
        }
        if (verdict == NetwDisplayHistory::PASS_SKIP_EMPTY) {
            continue;
        }

        const bool project = verdict == NetwDisplayHistory::PASS_SAMPLE_PROJECT;
        Variant velocity;
        bool has_velocity = false;
        if (project && spec.is_valid()
            && spec->get_project_channel() != StringName()) {
            const int64_t sibling_at
                = tracks->by_key(spec->get_project_channel());
            if (sibling_at >= 0) {
                const Ref<NetwDisplayChannel> sibling = states[sibling_at];
                const Ref<NetwDisplayHistory> shistory = sibling->get_history();
                if (shistory.is_valid() && !shistory->is_empty()) {
                    velocity = shistory->get_at(history->newest_tick());
                    has_velocity = velocity.get_type() != Variant::NIL;
                }
            }
        }

        Variant result = history->sample(
            dt,
            factor,
            state->get_last_written(),
            eit,
            project,
            max_forecast_ticks,
            p_timing->get_ticktime(),
            velocity,
            has_velocity
        );
        result = state->render_offset().apply(
            result,
            decay,
            p_runtime->get_display_offset_limit(),
            state->get_last_written(),
            spec.is_valid() ? int64_t(spec->get_mode())
                            : int64_t(NetwInterpolate::MODE_LERP)
        );
        if (history->has_projected()) {
            p_stats->set_projecting(p_stats->get_projecting() + 1);
            p_stats->set_max_forecast_age(std::max(
                p_stats->get_max_forecast_age(),
                history->get_project_age()
            ));
        }
        if (history->is_sleeping()) {
            p_stats->set_sleeping(p_stats->get_sleeping() + 1);
        }
        const double weight = spec.is_valid()
            ? spec->smoothing_weight(p_timing->get_frame_delta())
            : 1.0;
        result
            = history->smooth_toward(state->get_last_written(), result, weight);
        if (history->has_snapped()) {
            p_stats->set_snaps(p_stats->get_snaps() + 1);
        }
        if (trace) {
            NETW_TRACE(
                sys::INTERPOLATION,
                "interp %s dt=%d lag=%.2f val=%s",
                String(state->get_name()),
                int(dt),
                playhead->get_display_lag(),
                String(result)
            );
        }
        state->write(result);
        state->set_last_written(result);
    }
}

void pump_chase(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing
) {
    const double weight = 1.0
        - std::exp(
              -p_timing->get_frame_delta()
              / chase_smooth_time(p_runtime, p_timing)
        );
    const double decay = glide(p_runtime, p_timing->get_frame_delta());
    const bool trace = take_trace_frame(p_runtime);
    const TypedArray<NetwDisplayChannel> states = p_runtime->get_states();

    for (int at = 0; at < states.size(); ++at) {
        const Ref<NetwDisplayChannel> state = states[at];
        if (state->get_self_feedback()) {
            continue;
        }
        const Variant source = state->get_source_obj();
        Object *source_obj = source;
        if (source_obj == nullptr) {
            continue;
        }
        const Ref<NetwInterpolate> spec = state->get_spec();
        Variant value = source_obj->get(state->get_source_prop());
        value = state->render_offset().apply(
            value,
            decay,
            p_runtime->get_display_offset_limit(),
            state->get_last_written(),
            spec.is_valid() ? int64_t(spec->get_mode())
                            : int64_t(NetwInterpolate::MODE_LERP)
        );
        const Variant result = state->get_history()->smooth_toward(
            state->get_last_written(),
            value,
            weight
        );
        if (trace) {
            NETW_TRACE(
                sys::INTERPOLATION,
                "chase %s target=%s val=%s",
                String(state->get_name()),
                String(value),
                String(result)
            );
        }
        state->write(result);
        state->set_last_written(result);
    }
}

void pump_runtime(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Ref<NetwDisplayTiming> &p_timing,
    const Ref<NetwPumpStats> &p_stats,
    const DisplayHooks &p_hooks
) {
    if (p_runtime->get_pump_mode() == NetwDisplayDecl::PUMP_UNRESOLVED) {
        if (p_runtime->owner() == nullptr) {
            return;
        }
        p_hooks.resolve(p_runtime);
    }
    switch (p_runtime->get_pump_mode()) {
        case NetwDisplayDecl::PUMP_DISABLED:
            return;
        case NetwDisplayDecl::PUMP_CHASE:
            p_runtime->set_pumped(p_runtime->get_pumped() + 1);
            p_stats->set_runtimes(p_stats->get_runtimes() + 1);
            pump_chase(p_runtime, p_timing);
            return;
        default:
            p_runtime->set_pumped(p_runtime->get_pumped() + 1);
            p_stats->set_runtimes(p_stats->get_runtimes() + 1);
            pump_history(p_runtime, p_timing, p_stats);
            return;
    }
}

void absorb_recovery(
    const Ref<NetwDisplayRuntime> &p_runtime,
    const Dictionary &p_deltas,
    bool p_teleported,
    const DisplayHooks &p_hooks
) {
    if (p_runtime->get_pump_mode() != NetwDisplayDecl::PUMP_CHASE) {
        return;
    }
    const TypedArray<NetwDisplayChannel> states = p_runtime->get_states();
    if (p_teleported) {
        for (int at = 0; at < states.size(); ++at) {
            const Ref<NetwDisplayChannel> state = states[at];
            state->render_offset().clear();
        }
        return;
    }
    const double limit = p_hooks.clamp_for(p_runtime);
    p_runtime->set_display_offset_limit(limit);
    for (int at = 0; at < states.size(); ++at) {
        const Ref<NetwDisplayChannel> state = states[at];
        if (state->get_self_feedback()) {
            continue;
        }
        if (!p_deltas.has(state->get_source_prop())) {
            continue;
        }
        state->render_offset().absorb(
            p_deltas[state->get_source_prop()],
            limit
        );
    }
}

} // namespace display

} // namespace netw

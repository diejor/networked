#include "netw/display/pump.hpp"

#include <algorithm>
#include <cmath>

#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/colors.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/role_facts.hpp"
#include "netw/display/spec_row.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

void Hooks::resolve(const RID &p_entity) const {
    if (resolve_role.is_valid()) {
        resolve_role.call(p_entity);
    }
}

double Hooks::clamp_for(const RID &p_entity) const {
    if (!chase_clamp.is_valid()) {
        return INFINITY;
    }
    return double(chase_clamp.call(p_entity));
}

static void append_argument_specs(
    LocalVector<SpecRow> &r_rows,
    Node *p_node,
    const Dictionary &p_configs
) {
    const Array members = p_configs.keys();
    for (int at = 0; at < members.size(); at++) {
        const Ref<NetwMemberConfig> config = p_configs[members[at]];
        if (config.is_null()) {
            continue;
        }
        const Array specs = config->get_interpolators();
        for (int spec_at = 0; spec_at < specs.size(); spec_at++) {
            const Ref<NetwInterpolate> spec = specs[spec_at];
            if (spec.is_null() || spec->get_mode() == NetwInterpolate::MODE_NONE
                || spec->get_target().is_empty()) {
                continue;
            }
            r_rows.push_back(SpecRow::of_argument(p_node, spec));
        }
    }
}

LocalVector<SpecRow> Hooks::specs_of(Node *p_owner) const {
    LocalVector<SpecRow> rows;
    if (p_owner == nullptr) {
        return rows;
    }
    if (spec_override_armed) {
        ++spec_asks;
        for (uint32_t at = 0; at < spec_override.size(); ++at) {
            rows.push_back(spec_override[at]);
        }
        return rows;
    }

    LocalVector<Node *> nodes;
    nodes.push_back(p_owner);
    const TypedArray<Node> found
        = p_owner->find_children("*", String(), true, false);
    for (int at = 0; at < found.size(); at++) {
        Node *child = Object::cast_to<Node>(found[at]);
        if (child != nullptr) {
            nodes.push_back(child);
        }
    }

    for (Node *node : nodes) {
        const Dictionary properties
            = netw::script::model::get_node_property_configs(node);
        const Array named = properties.keys();
        for (int at = 0; at < named.size(); at++) {
            const Ref<NetwMemberConfig> config = properties[named[at]];
            if (config.is_null()) {
                continue;
            }
            const Array specs = config->get_interpolators();
            if (specs.is_empty()) {
                continue;
            }
            const Ref<NetwInterpolate> spec = specs[0];
            rows.push_back(SpecRow::of_property(node, named[at], spec));
        }
        const Ref<Script> script = node->get_script();
        if (script.is_null()) {
            continue;
        }
        append_argument_specs(
            rows,
            node,
            netw::script::model::get_rpc_configs(script)
        );
        append_argument_specs(
            rows,
            node,
            netw::script::model::get_signal_configs(script)
        );
    }
    return rows;
}

void Hooks::compute_sync_intervals(const RID &p_entity) const {
    if (sync_intervals.is_valid()) {
        sync_intervals.call(p_entity);
    }
}

bool Hooks::authors(const RID &p_entity) const {
    if (!authors_streams.is_valid()) {
        return false;
    }
    return authors_streams.call(p_entity);
}

int Hooks::role_of(const RID &p_entity, bool p_authors_streams) const {
    if (!role_reader.is_valid()) {
        return netw::display::ROLE_DISABLED;
    }
    return role_reader.call(p_entity, p_authors_streams);
}

void Hooks::chase_hook(const RID &p_entity, bool p_bind) const {
    if (chase_hook_binder.is_valid()) {
        chase_hook_binder.call(p_entity, p_bind);
    }
}

double glide(Runtime *p_runtime, double p_frame_delta) {
    const Decl &config = p_runtime->get_config();
    return std::exp(-p_frame_delta / std::max(config.chase_glide_time, 0.001));
}

double chase_smooth_time(Runtime *p_runtime, const Timing &p_timing) {
    const Decl &config = p_runtime->get_config();
    if (config.predicted_smooth_time > 0.0) {
        return config.predicted_smooth_time;
    }
    if (p_timing.ticktime > 0.0) {
        return std::max(p_timing.ticktime * 0.85, 0.001);
    }
    return 1.0 / 60.0;
}

bool take_trace_frame(Runtime *p_runtime) {
    const Decl &config = p_runtime->get_config();
    if (config.trace_interval <= 0) {
        return false;
    }
    const int64_t next
        = (p_runtime->get_trace_frame() + 1) % config.trace_interval;
    p_runtime->set_trace_frame(next);
    return next == 0;
}

void dilate_playhead(
    Runtime *p_runtime,
    const Timing &p_timing,
    PumpStats &p_stats,
    bool p_trace
) {
    NETW_ZONE_NC("Display dilate playhead", colors::INTERP);
    Playhead &playhead = p_runtime->display_playhead();
    const int effective = playhead.effective_tick(p_timing.display_tick);
    const LocalVector<Channel *> &states = p_runtime->channels();

    bool starving = false;
    int64_t newest = -1;
    for (int at = 0; at < int(states.size()); ++at) {
        Channel *state = states[at];
        const History &history = state->display_history();
        if (history.is_empty()) {
            continue;
        }
        if (!history.has_tick_after(effective)) {
            starving = true;
            newest = history.newest_tick();
            break;
        }
    }

    if (starving) {
        p_stats.starving += 1;
        for (int at = 0; at < int(states.size()); ++at) {
            Channel *state = states[at];
            state->display_history().set_sleeping(false);
        }
    }

    playhead.dilate(
        p_runtime->get_config(),
        p_timing.frame_ticks,
        p_timing.display_offset,
        p_timing.recommended_display_offset,
        starving
    );

    if (p_trace) {
        NETW_TRACE(
            sys::INTERPOLATION,
            "dilation dt=%d newest=%d starving=%d lag=%.2f",
            effective,
            int(newest),
            int(starving),
            playhead.get_display_lag()
        );
    }
}

void pump_history(
    Runtime *p_runtime,
    const Timing &p_timing,
    PumpStats &p_stats
) {
    NETW_ZONE_NC("Display pump history", colors::INTERP);
    Playhead &playhead = p_runtime->display_playhead();
    const Decl &config = p_runtime->get_config();
    const bool trace = take_trace_frame(p_runtime);
    const bool bracketed
        = p_runtime->get_pump_mode() != netw::display::PUMP_REMOTE;

    bool forecast = false;
    int64_t max_forecast_ticks = 0;
    if (!bracketed) {
        forecast = config.timeline_mode == netw::display::TIMELINE_FORECAST;
        max_forecast_ticks = config.max_forecast_ticks;
        if (config.enable_smart_dilation) {
            dilate_playhead(p_runtime, p_timing, p_stats, trace);
        } else {
            playhead.set_display_lag(0.0);
        }
    }

    const double factor = playhead.place(
        p_timing.display_tick,
        p_timing.tick_factor,
        bracketed
    );
    const int64_t dt = playhead.get_display_tick();
    p_stats.max_display_lag
        = std::max(p_stats.max_display_lag, playhead.get_display_lag());

    const int64_t eit = playhead.get_expected_interval_ticks();
    const double decay = glide(p_runtime, p_timing.frame_delta);
    const LocalVector<Channel *> &states = p_runtime->channels();
    const Tracks &tracks = p_runtime->display_tracks();

    for (int at = 0; at < int(states.size()); ++at) {
        Channel *state = states[at];
        if (state->get_self_feedback()
            && p_runtime->get_pump_mode() == netw::display::PUMP_BRACKETED) {
            continue;
        }
        History &history = state->display_history();
        const Ref<NetwInterpolate> spec = state->get_spec();
        const int verdict = history.pass_verdict(spec, forecast);
        if (verdict == History::PASS_SKIP_SLEEPING) {
            p_stats.sleeping += 1;
            continue;
        }
        if (verdict == History::PASS_SKIP_EMPTY) {
            continue;
        }

        const bool project = verdict == History::PASS_SAMPLE_PROJECT;
        Variant velocity;
        bool has_velocity = false;
        if (project && spec.is_valid()
            && spec->get_project_channel() != StringName()) {
            const int64_t sibling_at
                = tracks.by_key(spec->get_project_channel());
            if (sibling_at >= 0) {
                Channel *sibling = states[sibling_at];
                const History &shistory = sibling->display_history();
                if (!shistory.is_empty()) {
                    velocity = shistory.get_at(history.newest_tick());
                    has_velocity = velocity.get_type() != Variant::NIL;
                }
            }
        }

        Variant result = history.sample(
            dt,
            factor,
            state->get_last_written(),
            eit,
            project,
            max_forecast_ticks,
            p_timing.ticktime,
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
        if (history.has_projected()) {
            p_stats.projecting += 1;
            p_stats.max_forecast_age
                = std::max(p_stats.max_forecast_age, history.get_project_age());
        }
        if (history.is_sleeping()) {
            p_stats.sleeping += 1;
        }
        const double weight = spec.is_valid()
            ? spec->smoothing_weight(p_timing.frame_delta)
            : 1.0;
        result
            = history.smooth_toward(state->get_last_written(), result, weight);
        if (history.has_snapped()) {
            p_stats.snaps += 1;
        }
        if (trace) {
            NETW_TRACE(
                sys::INTERPOLATION,
                "interp %s dt=%d lag=%.2f val=%s",
                String(state->get_name()),
                int(dt),
                playhead.get_display_lag(),
                String(result)
            );
        }
        state->write(result);
        state->set_last_written(result);
    }
}

void pump_chase(Runtime *p_runtime, const Timing &p_timing) {
    NETW_ZONE_NC("Display pump chase", colors::INTERP);
    const double weight = 1.0
        - std::exp(-p_timing.frame_delta
                   / chase_smooth_time(p_runtime, p_timing));
    const double decay = glide(p_runtime, p_timing.frame_delta);
    const bool trace = take_trace_frame(p_runtime);
    const LocalVector<Channel *> &states = p_runtime->channels();

    for (int at = 0; at < int(states.size()); ++at) {
        Channel *state = states[at];
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
        const Variant result = state->display_history().smooth_toward(
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
    Runtime *p_runtime,
    const Timing &p_timing,
    PumpStats &p_stats,
    const Hooks &p_hooks
) {
    NETW_ZONE_NC("Display pump runtime", colors::INTERP);
    if (p_runtime->get_pump_mode() == netw::display::PUMP_UNRESOLVED) {
        if (p_runtime->owner() == nullptr) {
            return;
        }
        p_hooks.resolve(p_runtime->entity_rid());
    }
    switch (p_runtime->get_pump_mode()) {
        case netw::display::PUMP_DISABLED:
            return;
        case netw::display::PUMP_CHASE:
            p_runtime->set_pumped(p_runtime->get_pumped() + 1);
            p_stats.runtimes += 1;
            pump_chase(p_runtime, p_timing);
            return;
        default:
            p_runtime->set_pumped(p_runtime->get_pumped() + 1);
            p_stats.runtimes += 1;
            pump_history(p_runtime, p_timing, p_stats);
            return;
    }
}

void absorb_recovery(
    Runtime *p_runtime,
    const Dictionary &p_deltas,
    bool p_teleported,
    const Hooks &p_hooks
) {
    if (p_runtime->get_pump_mode() != netw::display::PUMP_CHASE) {
        return;
    }
    const LocalVector<Channel *> &states = p_runtime->channels();
    if (p_teleported) {
        for (int at = 0; at < int(states.size()); ++at) {
            Channel *state = states[at];
            state->render_offset().clear();
        }
        return;
    }
    const double limit = p_hooks.clamp_for(p_runtime->entity_rid());
    p_runtime->set_display_offset_limit(limit);
    for (int at = 0; at < int(states.size()); ++at) {
        Channel *state = states[at];
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

} // namespace netw::display

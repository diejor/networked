#pragma once

#include "godot/callable.hpp"
#include "netw/display_runtime.hpp"
#include "netw/display_role_facts.hpp"
#include "netw/display_timing.hpp"
#include "netw/pump_stats.hpp"

namespace netw {

namespace display {

struct DisplayHooks {
    godot::Callable resolve_role;
    godot::Callable chase_clamp;
    godot::Callable spec_reader;
    godot::Callable display_lane;
    godot::Callable sync_intervals;
    godot::Callable authors_streams;
    godot::Callable role_facts_reader;
    godot::Callable chase_hook_binder;

    void resolve(const godot::Ref<NetwDisplayRuntime> &p_runtime) const;
    double clamp_for(const godot::Ref<NetwDisplayRuntime> &p_runtime) const;
    godot::Array specs_of(godot::Node *p_owner) const;
    void compute_sync_intervals(
        const godot::Ref<NetwDisplayRuntime> &p_runtime
    ) const;
    bool authors(const godot::Ref<NetwDisplayRuntime> &p_runtime) const;
    godot::Ref<NetwDisplayRoleFacts> role_facts(
        const godot::Ref<NetwDisplayRuntime> &p_runtime,
        bool p_authors_streams
    ) const;
    void chase_hook(
        const godot::Ref<NetwDisplayRuntime> &p_runtime,
        bool p_bind
    ) const;
};

double glide(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    double p_frame_delta
);

double chase_smooth_time(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Ref<NetwDisplayTiming> &p_timing
);

bool take_trace_frame(const godot::Ref<NetwDisplayRuntime> &p_runtime);

void dilate_playhead(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Ref<NetwDisplayTiming> &p_timing,
    const godot::Ref<NetwPumpStats> &p_stats,
    bool p_trace
);

void pump_history(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Ref<NetwDisplayTiming> &p_timing,
    const godot::Ref<NetwPumpStats> &p_stats
);

void pump_chase(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Ref<NetwDisplayTiming> &p_timing
);

void pump_runtime(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Ref<NetwDisplayTiming> &p_timing,
    const godot::Ref<NetwPumpStats> &p_stats,
    const DisplayHooks &p_hooks
);

void absorb_recovery(
    const godot::Ref<NetwDisplayRuntime> &p_runtime,
    const godot::Dictionary &p_deltas,
    bool p_teleported,
    const DisplayHooks &p_hooks
);

} // namespace display

} // namespace netw

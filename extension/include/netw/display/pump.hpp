#pragma once

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "netw/display/pump_stats.hpp"
#include "netw/display/role_facts.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/spec_row.hpp"
#include "netw/display/timing.hpp"

namespace netw::display {

struct Hooks {
    godot::Callable resolve_role;
    godot::Callable chase_clamp;
    godot::LocalVector<SpecRow> spec_override;
    bool spec_override_armed = false;
    mutable int spec_asks = 0;
    godot::Callable display_lane;
    godot::Callable sync_intervals;
    godot::Callable authors_streams;
    godot::Callable role_reader;
    godot::Callable chase_hook_binder;

    void resolve(const godot::RID &p_entity) const;
    double clamp_for(const godot::RID &p_entity) const;
    godot::LocalVector<SpecRow> specs_of(godot::Node *p_owner) const;
    void compute_sync_intervals(const godot::RID &p_entity) const;
    bool authors(const godot::RID &p_entity) const;
    int role_of(const godot::RID &p_entity, bool p_authors_streams) const;
    void chase_hook(const godot::RID &p_entity, bool p_bind) const;
};

double glide(Runtime *p_runtime, double p_frame_delta);

double chase_smooth_time(Runtime *p_runtime, const Timing &p_timing);

bool take_trace_frame(Runtime *p_runtime);

void dilate_playhead(
    Runtime *p_runtime,
    const Timing &p_timing,
    PumpStats &p_stats,
    bool p_trace
);

void pump_history(
    Runtime *p_runtime,
    const Timing &p_timing,
    PumpStats &p_stats
);

void pump_chase(Runtime *p_runtime, const Timing &p_timing);

void pump_runtime(
    Runtime *p_runtime,
    const Timing &p_timing,
    PumpStats &p_stats,
    const Hooks &p_hooks
);

void absorb_recovery(
    Runtime *p_runtime,
    const godot::Dictionary &p_deltas,
    bool p_teleported,
    const Hooks &p_hooks
);

} // namespace netw::display

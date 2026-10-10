#include "netw/monitor.hpp"

#if defined(DEBUG_ENABLED)

#include <algorithm>

#include "godot/callable.hpp"
#include "godot/engine.hpp"
#include "godot/performance.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace netw::monitor {

const Spec SPECS[COUNT] = {
    {"Networked/peers", QUANTITY},
    {"Networked/round_trip", TIME},
    {"Networked/jitter", TIME},
    {"Networked/loss", PERCENTAGE},
    {"Networked/upload_budget_used", PERCENTAGE},
    {"Networked/display_delay", TIME},
    {"Networked/late_inputs_per_second", QUANTITY},
    {"Networked Replication/entities", QUANTITY},
    {"Networked Replication/awake_rows", QUANTITY},
    {"Networked Replication/rows_waiting", QUANTITY},
    {"Networked Replication/corrections_per_second", QUANTITY},
    {"Networked Replication/replayed_ticks_per_second", QUANTITY},
    {"Networked Replication/memory_replication", MEMORY},
    {"Networked Replication/memory_prediction", MEMORY},
    {"Networked Replication/memory_display", MEMORY},
};

namespace {

struct Cache {
    Sample before;
    double values[COUNT] = {};
    uint64_t frame = UINT64_MAX;
    uint64_t sampled_usec = 0;
    bool primed = false;
};

Cache *cache = nullptr;

double rate(int64_t p_before, int64_t p_now, double p_seconds) {
    if (p_seconds <= 0.0 || p_now < p_before) {
        return 0.0;
    }
    return double(p_now - p_before) / p_seconds;
}

double budget_used(
    const Sample &p_before,
    const Sample &p_now,
    double p_seconds
) {
    double worst = 0.0;
    for (const Upload &now : p_now.uploads) {
        if (now.budget_per_second <= 0.0) {
            continue;
        }
        for (const Upload &before : p_before.uploads) {
            if (before.peer != now.peer) {
                continue;
            }
            const double sent
                = rate(before.sent_bytes, now.sent_bytes, p_seconds);
            worst = std::max(worst, sent / now.budget_per_second);
        }
    }
    return worst;
}

void refresh() {
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (frame == cache->frame) {
        return;
    }
    cache->frame = frame;
    Sample now;
    NetwMultiplayer *session = NetwMultiplayer::of(gd::scene_root());
    if (session != nullptr) {
        session->monitor_sample(now);
    }
    const uint64_t usec = Time::get_singleton()->get_ticks_usec();
    const double seconds
        = cache->primed ? double(usec - cache->sampled_usec) / 1.0e6 : 0.0;
    read(cache->before, now, seconds, cache->values);
    cache->before = now;
    cache->sampled_usec = usec;
    cache->primed = true;
}

double read_one(int64_t p_monitor) {
    if (cache == nullptr || p_monitor < 0 || p_monitor >= COUNT) {
        return 0.0;
    }
    refresh();
    return cache->values[p_monitor];
}

} // namespace

void read(
    const Sample &p_before,
    const Sample &p_now,
    double p_seconds,
    double r_values[COUNT]
) {
    r_values[PEERS] = double(p_now.peers);
    r_values[ROUND_TRIP] = p_now.round_trip;
    r_values[JITTER] = p_now.jitter;
    r_values[LOSS] = p_now.loss;
    r_values[UPLOAD_BUDGET_USED] = budget_used(p_before, p_now, p_seconds);
    r_values[DISPLAY_DELAY] = p_now.display_delay;
    r_values[LATE_INPUTS]
        = rate(p_before.late_inputs, p_now.late_inputs, p_seconds);
    r_values[ENTITIES] = double(p_now.entities);
    r_values[AWAKE_ROWS] = double(p_now.awake_rows);
    r_values[ROWS_WAITING] = double(p_now.rows_waiting);
    r_values[CORRECTIONS]
        = rate(p_before.corrections, p_now.corrections, p_seconds);
    r_values[REPLAYED_TICKS]
        = rate(p_before.replayed_ticks, p_now.replayed_ticks, p_seconds);
    r_values[MEMORY_REPLICATION] = double(p_now.memory_replication);
    r_values[MEMORY_PREDICTION] = double(p_now.memory_prediction);
    r_values[MEMORY_DISPLAY] = double(p_now.memory_display);
}

void install() {
    if (cache == nullptr) {
        cache = memnew(Cache);
    }
    for (int at = 0; at < COUNT; ++at) {
        gd::add_custom_monitor(
            StringName(SPECS[at].id),
            callable_mp_static(&read_one).bind(int64_t(at)),
            int(SPECS[at].kind)
        );
    }
}

void remove() {
    for (int at = 0; at < COUNT; ++at) {
        gd::remove_custom_monitor(StringName(SPECS[at].id));
    }
    if (cache != nullptr) {
        memdelete(cache);
        cache = nullptr;
    }
}

} // namespace netw::monitor

#endif

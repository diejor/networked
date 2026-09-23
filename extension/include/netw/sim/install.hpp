#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/sim/body.hpp"

namespace netw::sim {

enum class Restore : uint8_t {
    EXACT = 0,
    EXTRAPOLATED = 1,
    BUFFERED = 2,
};

struct Sample {
    godot::ObjectID binding;
    int64_t comp = 0;
    int64_t tick = -1;
    int64_t sender = 0;
    uint64_t tenure = 0;
    godot::Array keys;
    godot::Array values;
};

struct InstallStats {
    int64_t installed = 0;
    int64_t held = 0;
    int64_t skipped = 0;
    int64_t dropped = 0;
    int64_t fenced = 0;
    int64_t unreconstructed = 0;
    int64_t newest_age = -1;
    int64_t youngest_age = -1;
    int64_t oldest_age = -1;
};

struct Installs {
    godot::LocalVector<Sample> held;
    godot::LocalVector<int64_t> reconstructed;
    InstallStats stats;
};

bool reconstructs(Installs &r_installs, int64_t p_comp, bool p_whole);

bool installs_on_arrival(Restore p_restore, int64_t p_tick, int64_t p_display);

void hold(Installs &r_installs, const Sample &p_sample);

void take_due(
    Installs &r_installs,
    int64_t p_display,
    godot::LocalVector<Sample> &r_due
);

void note_install(InstallStats &r_stats, int64_t p_age);

bool asleep(const Bodies &p_bodies);

} // namespace netw::sim

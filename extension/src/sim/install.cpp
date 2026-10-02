#include "netw/sim/install.hpp"

#include <algorithm>

#include "godot/node.hpp"
#include "godot/physics_body.hpp"

using namespace godot;

namespace netw::sim {

namespace {

const StringName &sleeping_name() {
    static const StringName name("sleeping");
    return name;
}

} // namespace

bool reconstructs(Installs &r_installs, int64_t p_comp, bool p_whole) {
    if (r_installs.reconstructed.find(p_comp) >= 0) {
        return true;
    }
    if (!p_whole) {
        r_installs.stats.unreconstructed += 1;
        return false;
    }
    r_installs.reconstructed.push_back(p_comp);
    return true;
}

bool installs_on_arrival(Restore p_restore, int64_t p_tick, int64_t p_display) {
    return p_restore != Restore::BUFFERED || p_display < 0 || p_tick < 0
        || p_tick <= p_display;
}

void note_install(InstallStats &r_stats, int64_t p_age) {
    r_stats.installed += 1;
    if (p_age < 0) {
        return;
    }
    r_stats.newest_age = p_age;
    r_stats.youngest_age = r_stats.youngest_age < 0
        ? p_age
        : std::min(r_stats.youngest_age, p_age);
    r_stats.oldest_age = std::max(r_stats.oldest_age, p_age);
}

bool asleep(const Bodies &p_bodies) {
    if (p_bodies.held.is_empty()) {
        return false;
    }
    for (const AuthoredBody &held : p_bodies.held) {
        const store::NodeCell &cell = resident_of(p_bodies, held);
        if (cell.solid != nullptr) {
            if (!cell.solid->is_sleeping()) {
                return false;
            }
        } else if (cell.flat != nullptr) {
            if (!cell.flat->is_sleeping()) {
                return false;
            }
        } else if (cell.object == nullptr
                   || !bool(cell.object->get(sleeping_name()))) {
            return false;
        }
    }
    return true;
}

} // namespace netw::sim

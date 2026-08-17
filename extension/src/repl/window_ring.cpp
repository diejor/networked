#include "netw/repl/window_ring.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

namespace netw::repl {

using namespace godot;

WindowRing WindowRing::open(uint32_t p_depth) {
    WindowRing ring;
    ring.depth = p_depth > 0 ? p_depth : 1;
    return ring;
}

WindowRing WindowRing::declare(
    const Ref<SchemaRecord> &p_schema,
    uint32_t p_depth
) {
    WindowRing ring = open(p_depth);
    ring.declaration = p_schema;
    ring.compiled = wire::WirePlan::compile(p_schema);
    return ring;
}

bool WindowRing::gather(const Array &p_values, wire::CodeRow &r_row) const {
    NETW_ZONE_NC("Window ring gather", colors::WIRE);
    return wire::gather_scalar_row(declaration, compiled, p_values, r_row);
}

bool WindowRing::record(int64_t p_tick, const wire::CodeRow &p_row) {
    if (p_tick <= confirmed) {
        return false;
    }
    for (uint32_t at = 0; at < samples.size(); ++at) {
        if (samples[at].tick == p_tick) {
            samples[at].row = p_row;
            return true;
        }
    }
    WindowSample sample;
    sample.tick = p_tick;
    sample.row = p_row;
    samples.push_back(sample);

    // The oldest goes when the ring is full. It is the one the receiver has had
    // the most chances to hear, so dropping it forfeits the least.
    while (samples.size() > depth) {
        samples.remove_at(0);
    }
    return true;
}

void WindowRing::confirm(int64_t p_tick) {
    if (p_tick > confirmed) {
        confirmed = p_tick;
    }
    uint32_t at = 0;
    while (at < samples.size()) {
        if (samples[at].tick <= confirmed) {
            samples.remove_at(at);
            continue;
        }
        at += 1;
    }
}

LocalVector<WindowSample> WindowRing::pending() const {
    NETW_ZONE_NC("Window ring pending", colors::WIRE);
    LocalVector<WindowSample> out;
    for (uint32_t at = 0; at < samples.size(); ++at) {
        if (samples[at].tick > confirmed) {
            out.push_back(samples[at]);
        }
    }
    return out;
}

} // namespace netw::repl

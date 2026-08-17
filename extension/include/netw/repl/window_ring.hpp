#pragma once

/* The recent ticks a windowed lane repeats, so a lost one heals on the next
 * send rather than on a retransmit.
 *
 * An input lane is unreliable and its samples are not interchangeable: tick 41
 * is not a stale tick 42, it is a different input the simulation still owes a
 * step to. Retransmitting one costs a round trip the simulation has already
 * moved past, so a windowed lane instead REPEATS the range still in flight on
 * every send. A receiver that missed 41 gets it inside the frame carrying 42,
 * before it needed it.
 *
 * The range is floored by what the other side confirmed. Without the floor the
 * window would either grow without bound or drop samples the receiver never
 * got, and the two failures look identical from the sender: one costs bytes
 * forever and the other loses inputs silently.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

struct WindowSample {
    int64_t tick = -1;
    wire::CodeRow row;
};

class WindowRing {
    godot::LocalVector<WindowSample> samples;
    godot::Ref<SchemaRecord> declaration;
    wire::WirePlan compiled;
    uint32_t depth = 1;
    int64_t confirmed = -1;

public:
    static WindowRing open(uint32_t p_depth);

    // Opens against the declaration a caller holds, so the same ring both
    // grids values and repeats them. A ring opened by depth alone cannot
    // gather, which is what a test driving rows directly wants.
    static WindowRing declare(
        const godot::Ref<SchemaRecord> &p_schema,
        uint32_t p_depth
    );

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }

    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    uint32_t held() const {
        return samples.size();
    }

    int64_t floor_tick() const {
        return confirmed;
    }

    // Records one tick's row. A tick at or before what the far side confirmed
    // is not recorded, because repeating it would spend bytes on a sample the
    // receiver has already stepped.
    bool record(int64_t p_tick, const wire::CodeRow &p_row);

    // Drops everything at or before `p_tick`, which is what keeps the window
    // from growing without bound.
    void confirm(int64_t p_tick);

    // The samples a send should carry, oldest first: everything held above the
    // floor. Empty when the far side is current.
    godot::LocalVector<WindowSample> pending() const;
};

} // namespace netw::repl

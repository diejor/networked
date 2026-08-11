#pragma once

/* What a run's session can be asked about itself, as against about one lane.
 *
 * Occupancy is a property of the session rather than of any one entity in it,
 * so no lane owns these answers. It reports what the session published, which
 * makes a key the session never published absent here rather than zero. That
 * is the difference a reader taking a rate cannot tell for itself.
 *
 * [codeblock]
 * const Occupancy o = run.occupancy();
 * NETW_CHECK(o.reports("timelines"));
 * NETW_CHECK_EQ(o.timelines(), 1);
 * [/codeblock]
 */

#include "netw_test.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

struct Occupancy {
    godot::Vector<godot::StringName> occupancy_keys;
    int occupancy_timelines = 0;

    // Rewindable entities the session is recording, which is what a declared
    // timeline earns and an undeclared one gives back.
    int timelines() const {
        return occupancy_timelines;
    }

    bool reports(const godot::StringName &p_key) const {
        return occupancy_keys.find(p_key) >= 0;
    }

    bool taken() const {
        return !occupancy_keys.is_empty();
    }
};

} // namespace netw_test

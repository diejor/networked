#pragma once

/* What one scene holds at the end of a run.
 *
 * Membership is a property of a scene rather than of the session or of any one
 * entity, so neither `Occupancy` nor `Lane` owns these answers. It reports the
 * scene's own two edges: who it encloses, which is ancestry, and whom it
 * admits, which is a boundary. A scene the run never asked about is absent
 * rather than empty, which is the difference a reader counting members cannot
 * tell for itself.
 *
 * [codeblock]
 * const Membership arena = run.membership("Arena");
 * NETW_CHECK(arena.encloses("Crate"));
 * NETW_CHECK(arena.admits(0));
 * [/codeblock]
 */

#include "netw_test.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

struct Membership {
    godot::StringName scene_name;
    godot::Vector<godot::StringName> enclosed;
    godot::Vector<int> admitted;
    int member_count = 0;
    // Whether the scene answers with an interest layer. A boundary opens when
    // the scene first admits someone, so this is not a property of having been
    // declared.
    bool boundary = false;
    bool asked = false;

    // Named members the run declared. `member_count` is what the scene
    // answered, which is larger whenever it encloses something no declaration
    // gave a name to.
    bool encloses(const godot::StringName &p_name) const {
        return enclosed.find(p_name) >= 0;
    }

    int members() const {
        return member_count;
    }

    bool admits(int p_client) const {
        return admitted.find(p_client) >= 0;
    }

    int viewers() const {
        return admitted.size();
    }

    bool has_boundary() const {
        return boundary;
    }

    bool taken() const {
        return asked;
    }
};

} // namespace netw_test

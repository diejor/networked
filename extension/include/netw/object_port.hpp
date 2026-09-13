#pragma once

/* One slot's handle on a game object.
 *
 * The id is resolved at each property-I/O moment rather than held across one,
 * because the user code between two moments may free the object: a held
 * pointer is a crash and a held reference is a leak. A resolve that fails
 * clears the port and counts the loss, which leaves the slot in its unbound
 * behaviour of performing no property I/O at all.
 */

#include <cstdint>

#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/subsystems.hpp"

namespace netw {

struct ObjectPort {
    godot::ObjectID id;
    int64_t lost = 0;

    bool bound() const;
    bool bind(godot::Object *p_owner);
    void unbind();
    godot::Object *resolve(SubsystemName p_module);
};

godot::Dictionary port_capture(
    ObjectPort &r_port,
    SubsystemName p_module,
    const godot::Array &p_keys
);

bool port_apply(
    ObjectPort &r_port,
    SubsystemName p_module,
    const godot::Dictionary &p_payload
);

// Godot's direct space state reflects the last physics sync, so a query run
// inside a rewind sees the moved body only after this.
void port_sync_transform(godot::Object *p_owner);

} // namespace netw

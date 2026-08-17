#pragma once

/* The per-peer aggregation the carrier fills between flushes.
 *
 * Frames bound for one peer are concatenated into one datagram rather than
 * sent one at a time, so the two lanes here are byte runs keyed by peer. They
 * are two rather than one because reliable and unreliable delivery are
 * different datagrams and a run cannot be half of each.
 *
 * The overflow rule lives here rather than at the call site, which is the
 * point of the class. An unreliable run has a budget, and a frame that would
 * take it past that budget does not truncate it and does not silently
 * oversize it: the accumulated run is handed back for sending and the frame
 * opens a fresh one. Because the rule is inside the append, there is no
 * second place that has to remember to check it.
 *
 * A reliable run has no budget. Reliable delivery fragments below this layer,
 * so a bound here would split what the transport is willing to carry whole.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwCarrierBuffers : public godot::RefCounted {
    GDCLASS(NetwCarrierBuffers, godot::RefCounted)

private:
    godot::HashMap<int64_t, godot::PackedByteArray> unreliable;
    godot::HashMap<int64_t, godot::PackedByteArray> reliable;

    godot::HashMap<int64_t, godot::PackedByteArray> &lane(bool p_reliable);
    const godot::HashMap<int64_t, godot::PackedByteArray> &lane(
        bool p_reliable
    ) const;

protected:
    static void _bind_methods();

public:
    godot::PackedByteArray append(
        int64_t p_peer,
        const godot::PackedByteArray &p_frame,
        bool p_reliable,
        int64_t p_budget
    );

    godot::PackedByteArray take(int64_t p_peer, bool p_reliable);

    godot::PackedInt32Array peers(bool p_reliable) const;

    int64_t pending(int64_t p_peer, bool p_reliable) const;
    void clear();
};

} // namespace netw

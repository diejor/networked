#pragma once

/* Where one entity is in its life, as a table of legal moves.
 *
 * The table is the contract, and it is public for the reason
 * `NetwSessionCore::edge_is_legal` is: a caller that can ASK whether a move is
 * legal does not need the move to crash to find out. The GDScript original
 * asserted, which meant a debug build died on an illegal edge and a release
 * build took it silently, so the one build that shipped was the one with no
 * check at all.
 *
 * Two shapes here differ from the session's state machine, deliberately:
 * a stage is never re-entered, so the same stage twice is illegal rather than
 * a no-op; and a reparent is not an edge at all, because a record keeps its
 * LIVE stage across a move.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

// The stage vocabulary. NetwEntity publishes it to ClassDB and nothing else
// does, so a bound method here takes and returns `int` rather than binding a
// second enum beside the one a caller reads.

enum class EntityStage : int {
    UNBOUND = 0,
    TEMPLATE = 1,
    ARMED = 2,
    LIVE = 3,
    DESPAWNING = 4,
    LINGERING = 5,
    FREED = 6,
};

class NetwEntityStage : public godot::RefCounted {
    GDCLASS(NetwEntityStage, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    static bool edge_is_legal(int64_t p_from, int64_t p_to);

    static bool can_begin_despawn(int64_t p_stage);
};

} // namespace netw

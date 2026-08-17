#pragma once

/* The committed interest matrix, turned into what each peer is owed.
 *
 * Deciding and doing are separate here on purpose: the plan is a value, so
 * what a peer is sent is a function of the rows and the peer set and never of
 * the order anything happened to be iterated in.
 *
 * The two orderings are the whole law. A gain is planned parent before child,
 * because a peer that receives a child before its parent has nowhere to put
 * it. A loss is planned child before parent, for the same reason read
 * backwards. A row's parent must therefore be decided before the row, which is
 * why rows arrive in book order and are read forward once for gains and
 * backward once for losses.
 *
 * A child is owed a route only where its parent is too: `local_desired` is
 * what the interest layer decided about this row alone, and the clamp against
 * the parent's answer is what makes the two orderings sufficient.
 *
 * A leave decision is carried, never interpreted. This plane decides WHETHER
 * a peer keeps a route it is no longer owed; the interest layer decides what
 * keeping it means, and `custom` is opaque all the way through.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::repl {

enum class SpawnAction { SPAWN, RETAIN, DESPAWN };

struct LeaveDecision {
    bool despawn = true;
    godot::Array custom;
};

struct SpawnRow {
    int64_t route = 0;
    int64_t parent_route = 0;
    godot::PackedInt32Array recipients;
    godot::HashMap<int64_t, bool> local_desired;
    godot::HashMap<int64_t, LeaveDecision> leave;
};

struct SpawnOp {
    SpawnAction action = SpawnAction::SPAWN;
    int64_t route = 0;
    int64_t peer = 0;
    LeaveDecision decision;
    // The parent's despawn forced this one, so the row's own decision loses.
    bool forced = false;
};

/* Answers `p_routes` with every route after its parent, the order
 * `reconcile` reads its rows in.
 *
 * Arm order is not ancestry order and the difference is not rare: a reparent
 * moves an entity under a parent armed after it, and a clamp that read a
 * parent verdict not yet computed would leave the mover permanently
 * unspawnable.
 *
 * Tree ancestry cannot cycle, so a pass that places nothing means the anchors
 * are mid-move. The routes left over are appended in arm order rather than
 * dropped, because a route missing from the plan is never sent to anyone.
 */
godot::LocalVector<int64_t> ancestry_order(
    const godot::LocalVector<int64_t> &p_routes,
    const godot::HashMap<int64_t, int64_t> &p_parents
);

/* Plans `p_peers` against `p_rows`, which arrive in parent-before-child book
 * order. A row whose parent is not in `p_rows` is unclamped.
 */
godot::LocalVector<SpawnOp> reconcile(
    const godot::LocalVector<SpawnRow> &p_rows,
    const godot::PackedInt32Array &p_peers
);

} // namespace netw::repl

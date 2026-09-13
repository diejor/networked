#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::repl {

enum class SpawnAction { SPAWN, RETAIN, HIDE };

struct LeaveDecision {
    bool hide = true;
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
    bool forced = false;
};

godot::LocalVector<int64_t> ancestry_order(
    const godot::LocalVector<int64_t> &p_routes,
    const godot::HashMap<int64_t, int64_t> &p_parents
);

godot::LocalVector<SpawnOp> reconcile(
    const godot::LocalVector<SpawnRow> &p_rows,
    const godot::PackedInt32Array &p_peers
);

} // namespace netw::repl

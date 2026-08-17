#include "netw/spawn_planner.hpp"

#include "godot/class_db.hpp"
#include "netw/repl/spawn_plan.hpp"

namespace netw {

using namespace godot;

namespace {

const StringName &key_action() {
    static const StringName name("action");
    return name;
}

const StringName &key_route() {
    static const StringName name("route");
    return name;
}

const StringName &key_peer() {
    static const StringName name("peer");
    return name;
}

const StringName &key_decision() {
    static const StringName name("decision");
    return name;
}

const StringName &key_forced() {
    static const StringName name("forced");
    return name;
}

const StringName &key_despawn() {
    static const StringName name("despawn");
    return name;
}

const StringName &key_custom() {
    static const StringName name("custom");
    return name;
}

repl::LeaveDecision read_decision(const Dictionary &p_decision) {
    repl::LeaveDecision out;
    out.despawn = p_decision.get(key_despawn(), true);
    out.custom = p_decision.get(key_custom(), Array());
    return out;
}

Dictionary write_decision(const repl::LeaveDecision &p_decision) {
    Dictionary out;
    out[key_despawn()] = p_decision.despawn;
    out[key_custom()] = p_decision.custom;
    return out;
}

repl::SpawnRow read_row(const Dictionary &p_row) {
    repl::SpawnRow row;
    row.route = p_row.get(key_route(), 0);
    row.parent_route = p_row.get(StringName("parent_route"), 0);

    const Array recipients = p_row.get(StringName("recipients"), Array());
    row.recipients.resize(recipients.size());
    for (int64_t i = 0; i < recipients.size(); ++i) {
        row.recipients.set(int(i), int32_t(int64_t(recipients[i])));
    }

    const Dictionary local
        = p_row.get(StringName("local_desired"), Dictionary());
    const Array local_peers = local.keys();
    for (int64_t i = 0; i < local_peers.size(); ++i) {
        row.local_desired[int64_t(local_peers[i])]
            = bool(local[local_peers[i]]);
    }

    const Dictionary leave = p_row.get(StringName("leave"), Dictionary());
    const Array leave_peers = leave.keys();
    for (int64_t i = 0; i < leave_peers.size(); ++i) {
        row.leave.insert(
            int64_t(leave_peers[i]),
            read_decision(leave[leave_peers[i]])
        );
    }
    return row;
}

const StringName &action_name(repl::SpawnAction p_action) {
    static const StringName spawn("spawn");
    static const StringName retain("retain");
    static const StringName despawn("despawn");
    switch (p_action) {
        case repl::SpawnAction::SPAWN:
            return spawn;
        case repl::SpawnAction::RETAIN:
            return retain;
        default:
            return despawn;
    }
}

} // namespace

PackedInt64Array NetwSpawnPlanner::ancestry_order(
    const PackedInt64Array &p_routes,
    const Dictionary &p_parents
) {
    LocalVector<int64_t> routes;
    routes.reserve(p_routes.size());
    for (int64_t i = 0; i < p_routes.size(); ++i) {
        routes.push_back(p_routes[i]);
    }
    HashMap<int64_t, int64_t> parents;
    const Array keys = p_parents.keys();
    for (int64_t i = 0; i < keys.size(); ++i) {
        parents.insert(int64_t(keys[i]), int64_t(p_parents[keys[i]]));
    }

    PackedInt64Array out;
    for (int64_t route : repl::ancestry_order(routes, parents)) {
        out.push_back(route);
    }
    return out;
}

Array NetwSpawnPlanner::reconcile(
    const Array &p_rows,
    const PackedInt32Array &p_peers
) {
    LocalVector<repl::SpawnRow> rows;
    rows.reserve(p_rows.size());
    for (int64_t i = 0; i < p_rows.size(); ++i) {
        rows.push_back(read_row(p_rows[i]));
    }

    Array out;
    for (const repl::SpawnOp &op : repl::reconcile(rows, p_peers)) {
        Dictionary entry;
        entry[key_action()] = action_name(op.action);
        entry[key_route()] = op.route;
        entry[key_peer()] = op.peer;
        // A gain carries no leave policy, because nothing is being left.
        if (op.action != repl::SpawnAction::SPAWN) {
            entry[key_decision()] = write_decision(op.decision);
            entry[key_forced()] = op.forced;
        }
        out.push_back(entry);
    }
    return out;
}

void NetwSpawnPlanner::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwSpawnPlanner",
        D_METHOD("ancestry_order", "routes", "parents"),
        &NetwSpawnPlanner::ancestry_order
    );
    ClassDB::bind_static_method(
        "NetwSpawnPlanner",
        D_METHOD("reconcile", "rows", "peers"),
        &NetwSpawnPlanner::reconcile
    );
}

} // namespace netw

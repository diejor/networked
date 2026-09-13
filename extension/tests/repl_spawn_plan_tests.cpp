#include "support/netw_test.h"

#include <cstdint>
#include <initializer_list>
#include <utility>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/repl/spawn_plan.hpp"

using namespace godot;

namespace TestNetwReplSpawnPlan {

using godot::LocalVector;
using godot::PackedInt32Array;
using netw::repl::LeaveDecision;
using netw::repl::reconcile;
using netw::repl::SpawnAction;
using netw::repl::SpawnOp;
using netw::repl::SpawnRow;

const int64_t PEER = 2;

PackedInt32Array only(int32_t p_peer) {
    PackedInt32Array peers;
    peers.push_back(p_peer);
    return peers;
}

PackedInt32Array held(int32_t p_peer) {
    return only(p_peer);
}

PackedInt32Array nobody() {
    return PackedInt32Array();
}

SpawnRow row(
    int64_t p_route,
    int64_t p_parent_route,
    const PackedInt32Array &p_recipients,
    bool p_desired
) {
    SpawnRow out;
    out.route = p_route;
    out.parent_route = p_parent_route;
    out.recipients = p_recipients;
    out.local_desired[PEER] = p_desired;
    return out;
}

void keeps(SpawnRow &r_row, int64_t p_peer) {
    LeaveDecision decision;
    decision.hide = false;
    r_row.leave.insert(p_peer, decision);
}

LocalVector<SpawnRow> nested(
    const PackedInt32Array &p_parent_held,
    const PackedInt32Array &p_child_held,
    bool p_parent_desired,
    bool p_child_desired
) {
    LocalVector<SpawnRow> rows;
    rows.push_back(row(1, 0, p_parent_held, p_parent_desired));
    rows.push_back(row(2, 1, p_child_held, p_child_desired));
    return rows;
}

LocalVector<int64_t> routes(
    const LocalVector<SpawnOp> &p_plan,
    SpawnAction p_action
) {
    LocalVector<int64_t> out;
    for (const SpawnOp &op : p_plan) {
        if (op.action == p_action) {
            out.push_back(op.route);
        }
    }
    return out;
}

int64_t count(const LocalVector<SpawnOp> &p_plan, SpawnAction p_action) {
    return int64_t(routes(p_plan, p_action).size());
}

godot::HashMap<int64_t, int64_t> anchored(
    std::initializer_list<std::pair<int64_t, int64_t>> p_pairs
) {
    godot::HashMap<int64_t, int64_t> out;
    for (const std::pair<int64_t, int64_t> &pair : p_pairs) {
        out.insert(pair.first, pair.second);
    }
    return out;
}

LocalVector<int64_t> arm_order(std::initializer_list<int64_t> p_routes) {
    LocalVector<int64_t> out;
    for (int64_t route : p_routes) {
        out.push_back(route);
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] arm order is ancestry order until a reparent "
    "moves a route under a later one"
) {
    const LocalVector<int64_t> flat = netw::repl::ancestry_order(
        arm_order({1, 2, 3}),
        anchored({{1, 0}, {2, 0}, {3, 0}})
    );
    REQUIRE(flat.size() == 3);
    NETW_CHECK_EQ(flat[0], 1);
    NETW_CHECK_EQ(flat[1], 2);
    NETW_CHECK_EQ(flat[2], 3);

    const LocalVector<int64_t> moved = netw::repl::ancestry_order(
        arm_order({1, 2, 3}),
        anchored({{1, 0}, {2, 3}, {3, 0}})
    );
    REQUIRE(moved.size() == 3);
    NETW_CHECK_EQ(moved[0], 1);
    NETW_CHECK_EQ(moved[1], 3);
    NETW_CHECK_EQ(moved[2], 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a chain armed back to front still comes out "
    "root first"
) {
    const LocalVector<int64_t> order = netw::repl::ancestry_order(
        arm_order({1, 2, 3, 4}),
        anchored({{1, 2}, {2, 3}, {3, 4}, {4, 0}})
    );

    REQUIRE(order.size() == 4);
    NETW_CHECK_EQ(order[0], 4);
    NETW_CHECK_EQ(order[1], 3);
    NETW_CHECK_EQ(order[2], 2);
    NETW_CHECK_EQ(order[3], 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] an anchor that is mid-move keeps its route in "
    "the order rather than dropping it"
) {
    const LocalVector<int64_t> order = netw::repl::ancestry_order(
        arm_order({1, 2}),
        anchored({{1, 2}, {2, 1}})
    );

    NETW_CHECK_EQ(int64_t(order.size()), 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a route whose parent is not in the book is "
    "placed with the roots"
) {
    const LocalVector<int64_t> order
        = netw::repl::ancestry_order(arm_order({5}), anchored({{5, 99}}));

    REQUIRE(order.size() == 1);
    NETW_CHECK_EQ(order[0], 5);
}

TEST_CASE("[Networked][Repl][Hosted] a gain is planned parent before child") {
    const LocalVector<int64_t> gained = routes(
        reconcile(nested(nobody(), nobody(), true, true), only(PEER)),
        SpawnAction::SPAWN
    );

    REQUIRE(gained.size() == 2);
    NETW_CHECK_EQ(gained[0], 1);
    NETW_CHECK_EQ(gained[1], 2);
}

TEST_CASE("[Networked][Repl][Hosted] a loss is planned child before parent") {
    const LocalVector<int64_t> lost = routes(
        reconcile(nested(held(PEER), held(PEER), false, false), only(PEER)),
        SpawnAction::HIDE
    );

    REQUIRE(lost.size() == 2);
    NETW_CHECK_EQ(lost[0], 2);
    NETW_CHECK_EQ(lost[1], 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a child a peer is owed is not sent where its "
    "parent is not"
) {
    const LocalVector<SpawnOp> plan
        = reconcile(nested(nobody(), nobody(), false, true), only(PEER));

    NETW_CHECK_EQ(count(plan, SpawnAction::SPAWN), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer already holding what it is owed is sent "
    "nothing"
) {
    const LocalVector<SpawnOp> plan
        = reconcile(nested(held(PEER), held(PEER), true, true), only(PEER));

    NETW_CHECK_EQ(int64_t(plan.size()), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a child may leave while its parent stays"
) {
    const LocalVector<int64_t> lost = routes(
        reconcile(nested(held(PEER), held(PEER), true, false), only(PEER)),
        SpawnAction::HIDE
    );

    REQUIRE(lost.size() == 1);
    NETW_CHECK_EQ(lost[0], 2);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a leave decision that keeps the route makes the "
    "loss a retain"
) {
    LocalVector<SpawnRow> rows;
    rows.push_back(row(1, 0, held(PEER), false));
    keeps(rows[0], PEER);

    const LocalVector<SpawnOp> plan = reconcile(rows, only(PEER));

    REQUIRE(plan.size() == 1);
    CHECK(plan[0].action == SpawnAction::RETAIN);
    CHECK(!plan[0].forced);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a parent that despawns takes a child that asked "
    "to be kept"
) {
    LocalVector<SpawnRow> rows = nested(held(PEER), held(PEER), false, true);
    keeps(rows[1], PEER);

    const LocalVector<SpawnOp> plan = reconcile(rows, only(PEER));
    const LocalVector<int64_t> lost = routes(plan, SpawnAction::HIDE);

    REQUIRE(lost.size() == 2);
    NETW_CHECK_EQ(lost[0], 2);
    NETW_CHECK_EQ(lost[1], 1);
    CHECK(plan[0].forced);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a parent that is retained lets its child be "
    "retained too"
) {
    LocalVector<SpawnRow> rows = nested(held(PEER), held(PEER), false, true);
    keeps(rows[0], PEER);
    keeps(rows[1], PEER);

    const LocalVector<SpawnOp> plan = reconcile(rows, only(PEER));

    NETW_CHECK_EQ(count(plan, SpawnAction::RETAIN), 2);
    NETW_CHECK_EQ(count(plan, SpawnAction::HIDE), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a peer outside the peer set is planned for "
    "neither way"
) {
    LocalVector<SpawnRow> rows;
    rows.push_back(row(1, 0, held(9), true));
    rows[0].local_desired[9] = false;

    NETW_CHECK_EQ(int64_t(reconcile(rows, only(PEER)).size()), 1);
    NETW_CHECK_EQ(int64_t(reconcile(rows, nobody()).size()), 0);
}

TEST_CASE("[Networked][Repl][Hosted] the same rows plan the same way twice") {
    LocalVector<SpawnRow> rows = nested(held(PEER), nobody(), true, true);
    rows[0].local_desired[PEER] = true;

    const LocalVector<SpawnOp> left = reconcile(rows, only(PEER));
    const LocalVector<SpawnOp> right = reconcile(rows, only(PEER));

    REQUIRE(left.size() == right.size());
    for (uint32_t i = 0; i < left.size(); ++i) {
        CHECK(left[i].action == right[i].action);
        NETW_CHECK_EQ(left[i].route, right[i].route);
        NETW_CHECK_EQ(left[i].peer, right[i].peer);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] P13 the same intake replays to the same plan, "
    "operation for operation, because the planner reads nothing but its rows"
) {
    LocalVector<SpawnRow> rows;
    rows.push_back(row(1, 0, nobody(), true));
    rows.push_back(row(2, 1, nobody(), true));
    rows.push_back(row(3, 0, held(PEER), false));

    const LocalVector<SpawnOp> left = reconcile(rows, only(PEER));
    const LocalVector<SpawnOp> right = reconcile(rows, only(PEER));

    REQUIRE(left.size() == right.size());
    for (uint32_t at = 0; at < left.size(); at++) {
        NETW_CHECK_EQ(int64_t(left[at].action), int64_t(right[at].action));
        NETW_CHECK_EQ(left[at].route, right[at].route);
        NETW_CHECK_EQ(left[at].peer, right[at].peer);
        NETW_CHECK_EQ(int64_t(left[at].forced), int64_t(right[at].forced));
    }
}

} // namespace TestNetwReplSpawnPlan

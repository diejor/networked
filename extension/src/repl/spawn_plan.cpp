#include "netw/repl/spawn_plan.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

namespace {

struct LeaveEffect {
    LeaveDecision decision;
    bool forced = false;
};

using PeerFlags = HashMap<int64_t, bool>;
using PeerEffects = HashMap<int64_t, LeaveEffect>;

bool holds(const PackedInt32Array &p_recipients, int64_t p_peer) {
    const int64_t count = p_recipients.size();
    for (int64_t i = 0; i < count; ++i) {
        if (int64_t(p_recipients[i]) == p_peer) {
            return true;
        }
    }
    return false;
}

bool flag(const PeerFlags &p_flags, int64_t p_peer) {
    PeerFlags::ConstIterator found = p_flags.find(p_peer);
    return found != p_flags.end() && found->value;
}

const LeaveEffect *effect_for(
    const HashMap<int64_t, PeerEffects> &p_effects,
    int64_t p_route,
    int64_t p_peer
) {
    HashMap<int64_t, PeerEffects>::ConstIterator route
        = p_effects.find(p_route);
    if (route == p_effects.end()) {
        return nullptr;
    }
    PeerEffects::ConstIterator peer = route->value.find(p_peer);
    return peer == route->value.end() ? nullptr : &peer->value;
}

LeaveDecision decision_for(const SpawnRow &p_row, int64_t p_peer) {
    HashMap<int64_t, LeaveDecision>::ConstIterator found
        = p_row.leave.find(p_peer);
    if (found == p_row.leave.end()) {
        return LeaveDecision();
    }
    LeaveDecision copy;
    copy.hide = found->value.hide;
    copy.custom = found->value.custom.duplicate(true);
    return copy;
}

} // namespace

LocalVector<int64_t> ancestry_order(
    const LocalVector<int64_t> &p_routes,
    const HashMap<int64_t, int64_t> &p_parents
) {
    NETW_ZONE_NC("Spawn ancestry order", colors::WIRE);
    LocalVector<int64_t> out;
    HashMap<int64_t, bool> placed;
    LocalVector<int64_t> pending;
    for (int64_t route : p_routes) {
        pending.push_back(route);
    }
    while (!pending.is_empty()) {
        LocalVector<int64_t> deferred;
        for (int64_t route : pending) {
            HashMap<int64_t, int64_t>::ConstIterator parent
                = p_parents.find(route);
            const int64_t parent_route
                = parent == p_parents.end() ? 0 : parent->value;
            if (parent_route > 0 && p_parents.has(parent_route)
                && !placed.has(parent_route)) {
                deferred.push_back(route);
                continue;
            }
            placed[route] = true;
            out.push_back(route);
        }
        if (deferred.size() == pending.size()) {
            for (int64_t route : deferred) {
                out.push_back(route);
            }
            break;
        }
        pending.clear();
        for (int64_t route : deferred) {
            pending.push_back(route);
        }
    }
    return out;
}

LocalVector<SpawnOp> reconcile(
    const LocalVector<SpawnRow> &p_rows,
    const PackedInt32Array &p_peers
) {
    NETW_ZONE_NC("Spawn reconcile", colors::WIRE);
    LocalVector<SpawnOp> plan;
    HashMap<int64_t, PeerFlags> desired;
    HashMap<int64_t, PeerEffects> leave_effects;

    const int64_t peer_count = p_peers.size();
    for (const SpawnRow &row : p_rows) {
        PeerFlags per_peer;
        PeerEffects effects;
        for (int64_t i = 0; i < peer_count; ++i) {
            const int64_t peer = int64_t(p_peers[i]);
            const bool local = flag(row.local_desired, peer);
            bool parent_desired = true;
            if (row.parent_route > 0) {
                HashMap<int64_t, PeerFlags>::ConstIterator parent
                    = desired.find(row.parent_route);
                parent_desired
                    = parent != desired.end() && flag(parent->value, peer);
            }
            const bool wants = local && parent_desired;
            per_peer[peer] = wants;

            const bool held = holds(row.recipients, peer);
            if (wants) {
                if (!held) {
                    SpawnOp op;
                    op.action = SpawnAction::SPAWN;
                    op.route = row.route;
                    op.peer = peer;
                    plan.push_back(op);
                }
                continue;
            }
            if (!held) {
                continue;
            }

            const LeaveEffect *parent_effect = row.parent_route > 0
                ? effect_for(leave_effects, row.parent_route, peer)
                : nullptr;
            const bool parent_hides = parent_effect != nullptr
                && (parent_effect->forced || parent_effect->decision.hide);

            LeaveEffect effect;
            effect.decision = decision_for(row, peer);
            effect.forced = parent_hides;
            if (parent_effect != nullptr && local) {
                effect.decision.hide = parent_hides;
            }
            effects.insert(peer, effect);
        }
        desired[row.route] = per_peer;
        if (!effects.is_empty()) {
            leave_effects[row.route] = effects;
        }
    }

    for (int64_t index = int64_t(p_rows.size()) - 1; index >= 0; --index) {
        const SpawnRow &row = p_rows[index];
        for (int64_t i = 0; i < peer_count; ++i) {
            const int64_t peer = int64_t(p_peers[i]);
            HashMap<int64_t, PeerFlags>::ConstIterator planned
                = desired.find(row.route);
            if (planned != desired.end() && flag(planned->value, peer)) {
                continue;
            }
            if (!holds(row.recipients, peer)) {
                continue;
            }
            const LeaveEffect *effect
                = effect_for(leave_effects, row.route, peer);
            SpawnOp op;
            op.route = row.route;
            op.peer = peer;
            op.decision
                = effect != nullptr ? effect->decision : LeaveDecision();
            op.forced = effect != nullptr && effect->forced;
            op.action = op.decision.hide || op.forced ? SpawnAction::HIDE
                                                      : SpawnAction::RETAIN;
            plan.push_back(op);
        }
    }
    return plan;
}

} // namespace netw::repl

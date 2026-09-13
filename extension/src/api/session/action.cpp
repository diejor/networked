#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/timeline.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/session/frames.hpp"
#include "netw/subsystems.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr int TIMING_TICK_ALIGNED = 0;
constexpr int TIMING_TICK_ALIGNED_STATE_READY = 1;
constexpr int TIMING_IMMEDIATE = 2;

} // namespace

Node *NetwMultiplayer::node_from_tree_path(const NodePath &p_path) const {
    if (!lagcomp_configured) {
        return nullptr;
    }
    Node *anchor = session_root();
    return anchor != nullptr ? anchor->get_node_or_null(p_path) : nullptr;
}

bool NetwMultiplayer::can_resolve_action(const PendingAction &p_request) const {
    Node *target = node_from_tree_path(p_request.target_path);
    return target != nullptr && target->has_method(p_request.method);
}

int NetwMultiplayer::input_readiness(
    const PendingAction &p_request,
    int64_t p_tick
) {
    if (p_tick - p_request.queued_at_tick >= input_gate_deadline_ticks) {
        return ACTION_READY_BY_DEADLINE;
    }
    Node *target = node_from_tree_path(p_request.target_path);
    const Ref<NetwEntity> entity
        = target != nullptr ? NetwEntity::of(target) : Ref<NetwEntity>();
    if (entity.is_null()) {
        return ACTION_NOT_READY;
    }
    NetwPredictSlotEngine *engine
        = predict_engine_for(entity->get_rid_handle());
    if (engine != nullptr
        && !engine->has_consumed_state_tick(p_request.view_tick)) {
        return ACTION_NOT_READY;
    }
    const Ref<NetwTimeline> history
        = lagcomp_core.timeline_history(lagcomp_core.timeline_slot_of(entity));
    if (history.is_null()
        || history->state_at(p_request.view_tick).is_empty()) {
        return ACTION_NOT_READY;
    }
    return ACTION_READY;
}

int NetwMultiplayer::action_readiness(
    const PendingAction &p_request,
    int64_t p_tick
) {
    switch (p_request.timing_mode) {
        case TIMING_IMMEDIATE:
            return ACTION_READY;
        case TIMING_TICK_ALIGNED:
            return p_request.view_tick <= p_tick ? ACTION_READY
                                                 : ACTION_NOT_READY;
        case TIMING_TICK_ALIGNED_STATE_READY:
            if (p_request.view_tick > p_tick) {
                return ACTION_NOT_READY;
            }
            return input_readiness(p_request, p_tick);
        default:
            break;
    }
    return ACTION_READY;
}

void NetwMultiplayer::execute_ready_action(
    const PendingAction &p_request,
    int64_t p_tick,
    int p_readiness
) {
    if (p_readiness == ACTION_READY_BY_DEADLINE) {
        gate_fallbacks += 1;
        NETW_TRACE(
            sys::PREDICTION,
            "action %s waited out its gate and resolves best effort",
            String(p_request.key).utf8().get_data()
        );
        emit_signal(
            StringName("lagcomp_action_gate_fallback"),
            p_request.key,
            p_request.view_tick
        );
    }
    execute_action(p_request, p_tick);
}

void NetwMultiplayer::execute_action(
    const PendingAction &p_request,
    int64_t p_tick
) {
    NETW_ZONE_NC("action execute", colors::PREDICTION);
    Node *target = node_from_tree_path(p_request.target_path);
    if (target == nullptr || !target->has_method(p_request.method)) {
        lagcomp_deny_action(p_request.requester, p_request.key);
        return;
    }
    const int64_t clamped
        = p_request.view_tick < p_tick ? p_request.view_tick : p_tick;
    Ref<NetwActionContext> context;
    context.instantiate();
    context->open(
        this,
        p_request.requester,
        clamped,
        p_request.view_tick,
        p_tick,
        p_request.key
    );
    if (p_request.data.get_type() == Variant::NIL) {
        target->call(p_request.method, context);
        return;
    }
    target->call(p_request.method, context, p_request.data);
}

int64_t NetwMultiplayer::action_slot_of(const Callable &p_authority) {
    Node *target = Object::cast_to<Node>(p_authority.get_object());
    if (target == nullptr) {
        return 0;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(target);
    if (entity.is_null()) {
        return 0;
    }
    const String route = String(entity->get_entity_id()) + ":"
        + String(p_authority.get_method());
    const HashMap<String, int64_t>::ConstIterator found
        = action_slots.find(route);
    if (found) {
        return found->value;
    }
    const int64_t minted = int64_t(action_slots.size());
    action_slots[route] = minted;
    return minted;
}

Ref<NetwAction> NetwMultiplayer::lagcomp_action(const Callable &p_authority) {
    const int64_t slot
        = lagcomp_is_configured() ? action_slot_of(p_authority) : 0;
    Ref<NetwAction> minted;
    minted.instantiate();
    minted->open(this, p_authority, slot);
    return minted;
}

void NetwMultiplayer::action_send_request(
    const NodePath &p_target_path,
    const StringName &p_method,
    int64_t p_view_tick,
    const Variant &p_data,
    const StringName &p_key,
    int p_timing_mode
) {
    if (!lagcomp_is_configured()) {
        return;
    }
    const bool is_remote = has_multiplayer_peer() && !is_server();
    const Ref<NetwEntity> entity
        = NetwEntity::of(node_from_tree_path(p_target_path));
    int64_t route = 0;
    if (entity.is_valid()) {
        route = liveness_route_of(entity.ptr());
        if (route <= 0 && !is_remote) {
            route = liveness_allocate_route(entity.ptr());
        }
    }
    if (route <= 0) {
        NETW_WARN(
            sys::LAGCOMP,
            "action target '%s' claims no route, so the request is denied",
            String(p_target_path)
        );
    }
    if (!is_remote) {
        submit_action(
            route,
            p_method,
            p_view_tick,
            p_data,
            p_key,
            p_timing_mode,
            0
        );
        return;
    }
    ReplicationCore *plane = (get_replication_plane());
    if (plane == nullptr) {
        NETW_TRACE(
            sys::LAGCOMP,
            "a remote action request found no replication plane"
        );
        return;
    }
    session::ActionRequest body;
    body.method = p_method;
    body.view_tick = p_view_tick;
    body.data = gd::var_to_bytes(p_data);
    body.key = p_key;
    body.timing = p_timing_mode;
    plane->send_to(
        MultiplayerPeer::TARGET_PEER_SERVER,
        route,
        wire::builtin_channel("ACTION"),
        session::frame_write(body),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::submit_action(
    int64_t p_route,
    const StringName &p_method,
    int64_t p_view_tick,
    const Variant &p_data,
    const StringName &p_key,
    int p_timing_mode,
    int64_t p_requester
) {
    NETW_ZONE_NC("action submit", colors::PREDICTION);
    int64_t requester = p_requester;
    if (requester == 0 && lagcomp_configured
        && NETW_API_VIRTUAL(get_multiplayer_peer)().is_valid()) {
        requester = int64_t(NETW_API_VIRTUAL(get_unique_id)());
    }

    PendingAction request;
    request.method = p_method;
    request.data = p_data;
    request.key = p_key;
    request.view_tick = p_view_tick;
    request.requester = requester;
    request.timing_mode = p_timing_mode;
    request.queued_at_tick = clock_engine().get_tick();

    Node *anchor = lagcomp_configured ? session_root() : nullptr;
    if (anchor != nullptr) {
        const Ref<NetwEntity> entity
            = Object::cast_to<NetwEntity>(wrapper_for_route(p_route).ptr());
        Node *owner = entity.is_valid() ? entity->get_owner() : nullptr;
        if (owner != nullptr) {
            request.target_path = anchor->get_path_to(owner);
        }
    }

    if (!can_resolve_action(request)) {
        NETW_TRACE(
            sys::PREDICTION,
            "action %s names no method this session can reach",
            String(p_key).utf8().get_data()
        );
        lagcomp_deny_action(requester, p_key);
        return;
    }
    const int64_t now = clock_engine().get_tick();
    const int readiness = action_readiness(request, now);
    if (readiness != ACTION_NOT_READY) {
        execute_ready_action(request, now, readiness);
        return;
    }
    if (p_view_tick > now + max_future_action_ticks) {
        NETW_TRACE(
            sys::PREDICTION,
            "action %s asks for tick %d, past the %d the session admits",
            String(p_key).utf8().get_data(),
            int(p_view_tick),
            int(now + max_future_action_ticks)
        );
        lagcomp_deny_action(requester, p_key);
        return;
    }
    pending_actions.push_back(request);
}

void NetwMultiplayer::drain_pending_actions(int64_t p_tick) {
    if (pending_actions.is_empty()) {
        return;
    }
    NETW_ZONE_NC("action drain", colors::PREDICTION);
    actions_still_waiting.clear();
    const LocalVector<PendingAction> queued(pending_actions);
    for (uint32_t at = 0; at < queued.size(); ++at) {
        const PendingAction &request = queued[at];
        if (!can_resolve_action(request)) {
            NETW_TRACE(
                sys::PREDICTION,
                "queued action %s lost the method it named",
                String(request.key).utf8().get_data()
            );
            lagcomp_deny_action(request.requester, request.key);
            continue;
        }
        const int readiness = action_readiness(request, p_tick);
        if (readiness == ACTION_NOT_READY) {
            actions_still_waiting.push_back(request);
            continue;
        }
        execute_ready_action(request, p_tick, readiness);
    }
    pending_actions = actions_still_waiting;
    actions_still_waiting.clear();
}

int64_t NetwMultiplayer::pending_action_count() const {
    return int64_t(pending_actions.size());
}

int64_t NetwMultiplayer::action_gate_fallbacks() const {
    return gate_fallbacks;
}

} // namespace netw

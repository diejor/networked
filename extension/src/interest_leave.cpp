#include "netw/interest_leave.hpp"

#include "netw/log.hpp"

using namespace godot;

namespace netw {

using LayerExits = InterestLeave::LayerExits;
using PerPeer = InterestLeave::PerPeer;

namespace {

const StringName &despawn_key() {
    static const StringName key("despawn");
    return key;
}

const StringName &custom_key() {
    static const StringName key("custom");
    return key;
}

Dictionary verdict(bool p_despawn, const Array &p_custom) {
    Dictionary out;
    out[despawn_key()] = p_despawn;
    out[custom_key()] = p_custom;
    return out;
}

} // namespace

void InterestLeave::record(
    int64_t key,
    int64_t peer,
    const StringName &layer_id
) {
    if (layer_id.is_empty()) {
        return;
    }
    LayerExits &layers = pending[key][peer];
    for (uint32_t index = 0; index < layers.size(); ++index) {
        if (layers[index] == layer_id) {
            return;
        }
    }
    layers.push_back(layer_id);
}

Array InterestLeave::pending_layers(int64_t key, int64_t peer) const {
    Array out;
    const HashMap<int64_t, PerPeer>::ConstIterator by_entity
        = pending.find(key);
    if (!by_entity) {
        return out;
    }
    const PerPeer::ConstIterator by_peer = by_entity->value.find(peer);
    if (!by_peer) {
        return out;
    }
    for (uint32_t index = 0; index < by_peer->value.size(); ++index) {
        out.push_back(by_peer->value[index]);
    }
    return out;
}

void InterestLeave::finish_sweep() {
    pending.clear();
}

bool InterestLeave::is_retained(int64_t key, int64_t peer) const {
    const HashMap<int64_t, HashSet<int64_t>>::ConstIterator found
        = retained.find(key);
    return found && found->value.has(peer);
}

void InterestLeave::release(int64_t key, int64_t peer) {
    const HashMap<int64_t, HashSet<int64_t>>::Iterator found
        = retained.find(key);
    if (found == retained.end()) {
        return;
    }
    found->value.erase(peer);
    if (found->value.is_empty()) {
        retained.erase(key);
    }
}

void InterestLeave::forget_entity(int64_t key) {
    pending.erase(key);
    retained.erase(key);
}

void InterestLeave::forget_peer(int64_t peer) {
    for (KeyValue<int64_t, PerPeer> &entry : pending) {
        entry.value.erase(peer);
    }
    for (KeyValue<int64_t, HashSet<int64_t>> &entry : retained) {
        entry.value.erase(peer);
    }
}

void InterestLeave::clear() {
    pending.clear();
    retained.clear();
}

Dictionary InterestLeave::resolve(
    int64_t key,
    int64_t peer,
    const Ref<NetwInterestDecl> &p_decl,
    const InterestEngine &p_engine
) const {
    const Array layers = pending_layers(key, peer);
    if (layers.is_empty()) {
        return verdict(!is_retained(key, peer), Array());
    }
    Array custom;
    for (int index = 0; index < layers.size(); ++index) {
        const StringName layer_id = layers[index];
        const int fallback = p_engine.layer_leave_policy(layer_id);
        const int policy = p_decl.is_valid()
            ? p_decl->leave_policy_for(layer_id, fallback)
            : fallback;
        if (policy == NetwInterestDecl::LEAVE_DESPAWN) {
            return verdict(true, Array());
        }
        if (policy != NetwInterestDecl::LEAVE_CUSTOM) {
            continue;
        }
        const Callable callback = p_decl.is_valid()
            ? p_decl->custom_leave_for(layer_id)
            : Callable();
        if (!callback.is_valid()) {
            NETW_ERROR(
                sys::INTEREST,
                "Layer '%s' declares a CUSTOM leave whose callback is gone, "
                "so the copy is despawned.",
                godot::String(layer_id)
            );
            return verdict(true, Array());
        }
        Array action;
        action.push_back(callback);
        action.push_back(layer_id);
        custom.push_back(action);
    }
    return verdict(false, custom);
}

void InterestLeave::commit(
    int64_t key,
    int64_t peer,
    const Dictionary &p_decision,
    bool p_forced
) {
    const HashMap<int64_t, PerPeer>::Iterator by_entity = pending.find(key);
    if (by_entity != pending.end()) {
        by_entity->value.erase(peer);
        if (by_entity->value.is_empty()) {
            pending.erase(key);
        }
    }
    const bool despawn
        = p_decision.get(despawn_key(), true).operator bool();
    if (p_forced || despawn) {
        release(key, peer);
        return;
    }
    retained[key].insert(peer);
    const Array actions = p_decision.get(custom_key(), Array());
    for (int index = 0; index < actions.size(); ++index) {
        const Array action = actions[index];
        if (action.size() != 2) {
            continue;
        }
        const Callable callback = action[0];
        if (!callback.is_valid()) {
            continue;
        }
        Array args;
        args.push_back(peer);
        args.push_back(action[1]);
        callback.callv(args);
    }
}


} // namespace netw

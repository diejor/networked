#include "netw/interest/leave.hpp"

#include "netw/log.hpp"

using namespace godot;

namespace netw::interest {

using LayerExits = Leave::LayerExits;
using PerPeer = Leave::PerPeer;

namespace {

const StringName &hide_key() {
    static const StringName key("hide");
    return key;
}

const StringName &custom_key() {
    static const StringName key("custom");
    return key;
}

Dictionary verdict(bool p_hide, const Array &p_custom) {
    Dictionary out;
    out[hide_key()] = p_hide;
    out[custom_key()] = p_custom;
    return out;
}

} // namespace

void Leave::record(int64_t key, int64_t peer, const StringName &layer_id) {
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

Array Leave::pending_layers(int64_t key, int64_t peer) const {
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

void Leave::finish_sweep() {
    pending.clear();
}

bool Leave::is_retained(int64_t key, int64_t peer) const {
    const HashMap<int64_t, HashSet<int64_t>>::ConstIterator found
        = retained.find(key);
    return found && found->value.has(peer);
}

void Leave::release(int64_t key, int64_t peer) {
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

void Leave::forget_entity(int64_t key) {
    pending.erase(key);
    retained.erase(key);
}

void Leave::forget_peer(int64_t peer) {
    for (KeyValue<int64_t, PerPeer> &entry : pending) {
        entry.value.erase(peer);
    }
    for (KeyValue<int64_t, HashSet<int64_t>> &entry : retained) {
        entry.value.erase(peer);
    }
}

void Leave::clear() {
    pending.clear();
    retained.clear();
}

Dictionary Leave::resolve(
    int64_t key,
    int64_t peer,
    const Decl *p_decl,
    const Engine &p_engine
) const {
    const Array layers = pending_layers(key, peer);
    if (layers.is_empty()) {
        return verdict(!is_retained(key, peer), Array());
    }
    Array custom;
    for (int index = 0; index < layers.size(); ++index) {
        const StringName layer_id = layers[index];
        const int fallback = p_engine.layer_leave_policy(layer_id);
        const int policy = p_decl != nullptr
            ? p_decl->leave_policy_for(layer_id, fallback)
            : fallback;
        if (policy == Decl::LEAVE_HIDE) {
            return verdict(true, Array());
        }
        if (policy != Decl::LEAVE_CUSTOM) {
            continue;
        }
        const Callable callback = p_decl != nullptr
            ? p_decl->custom_leave_for(layer_id)
            : Callable();
        if (!callback.is_valid()) {
            NETW_ERROR(
                sys::INTEREST,
                "Layer '%s' declares a CUSTOM leave whose callback is gone, "
                "so the copy is hidden.",
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

void Leave::commit(
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
    const bool hide = p_decision.get(hide_key(), true).operator bool();
    if (p_forced || hide) {
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

} // namespace netw::interest

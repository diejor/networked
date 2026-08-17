#include "netw/interest_leave.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

using LayerExits = NetwInterestLeave::LayerExits;
using PerPeer = NetwInterestLeave::PerPeer;

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

void NetwInterestLeave::record(
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

Array NetwInterestLeave::pending_layers(int64_t key, int64_t peer) const {
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

void NetwInterestLeave::finish_sweep() {
    pending.clear();
}

bool NetwInterestLeave::is_retained(int64_t key, int64_t peer) const {
    const HashMap<int64_t, HashSet<int64_t>>::ConstIterator found
        = retained.find(key);
    return found && found->value.has(peer);
}

void NetwInterestLeave::release(int64_t key, int64_t peer) {
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

void NetwInterestLeave::forget_entity(int64_t key) {
    pending.erase(key);
    retained.erase(key);
}

void NetwInterestLeave::forget_peer(int64_t peer) {
    for (KeyValue<int64_t, PerPeer> &entry : pending) {
        entry.value.erase(peer);
    }
    for (KeyValue<int64_t, HashSet<int64_t>> &entry : retained) {
        entry.value.erase(peer);
    }
}

void NetwInterestLeave::clear() {
    pending.clear();
    retained.clear();
}

Dictionary NetwInterestLeave::resolve(
    int64_t key,
    int64_t peer,
    const Ref<NetwInterestDecl> &p_decl,
    const Ref<NetwInterestEngine> &p_engine
) const {
    const Array layers = pending_layers(key, peer);
    if (layers.is_empty()) {
        return verdict(!is_retained(key, peer), Array());
    }
    Array custom;
    for (int index = 0; index < layers.size(); ++index) {
        const StringName layer_id = layers[index];
        const int fallback
            = p_engine.is_valid() ? p_engine->layer_leave_policy(layer_id)
                                  : int(DESPAWN);
        const int policy = p_decl.is_valid()
            ? p_decl->leave_policy_for(layer_id, fallback)
            : fallback;
        if (policy == DESPAWN) {
            return verdict(true, Array());
        }
        if (policy != CUSTOM) {
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

void NetwInterestLeave::commit(
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

void NetwInterestLeave::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("record", "key", "peer", "layer_id"),
        &NetwInterestLeave::record
    );
    ClassDB::bind_method(
        D_METHOD("pending_layers", "key", "peer"),
        &NetwInterestLeave::pending_layers
    );
    ClassDB::bind_method(
        D_METHOD("finish_sweep"),
        &NetwInterestLeave::finish_sweep
    );
    ClassDB::bind_method(
        D_METHOD("is_retained", "key", "peer"),
        &NetwInterestLeave::is_retained
    );
    ClassDB::bind_method(
        D_METHOD("release", "key", "peer"),
        &NetwInterestLeave::release
    );
    ClassDB::bind_method(
        D_METHOD("forget_entity", "key"),
        &NetwInterestLeave::forget_entity
    );
    ClassDB::bind_method(
        D_METHOD("forget_peer", "peer"),
        &NetwInterestLeave::forget_peer
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwInterestLeave::clear);
    ClassDB::bind_method(
        D_METHOD("resolve", "key", "peer", "decl", "engine"),
        &NetwInterestLeave::resolve
    );
    ClassDB::bind_method(
        D_METHOD("commit", "key", "peer", "decision", "forced"),
        &NetwInterestLeave::commit
    );
}

} // namespace netw

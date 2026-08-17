#include "netw/interest_perception.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

StringName hide_key() {
    return StringName("hide");
}

StringName custom_key() {
    return StringName("custom");
}

} // namespace

bool NetwInterestPerception::set_visible(int64_t key, bool p_visible) {
    const HashMap<int64_t, bool>::Iterator found = visible.find(key);
    if (found != visible.end() && found->value == p_visible) {
        return false;
    }
    visible[key] = p_visible;
    return true;
}

bool NetwInterestPerception::is_known(int64_t key) const {
    return visible.has(key);
}

void NetwInterestPerception::forget(int64_t key) {
    visible.erase(key);
}

void NetwInterestPerception::clear() {
    visible.clear();
    armed.clear();
}

void NetwInterestPerception::arm(int64_t key, const Array &p_actions) {
    if (p_actions.is_empty()) {
        return;
    }
    armed[key] = p_actions;
}

Array NetwInterestPerception::disarm(int64_t key) {
    const HashMap<int64_t, Array>::ConstIterator found = armed.find(key);
    if (!found) {
        return Array();
    }
    const Array out = found->value;
    armed.erase(key);
    return out;
}

PackedInt64Array NetwInterestPerception::armed_keys() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Array> &entry : armed) {
        out.push_back(entry.key);
    }
    return out;
}

Dictionary NetwInterestPerception::resolve(
    const Array &p_layers,
    const Ref<NetwInterestDecl> &p_decl,
    const Ref<NetwInterestEngine> &p_engine
) const {
    bool hide = false;
    Array custom;
    for (int index = 0; index < p_layers.size(); ++index) {
        const StringName layer_id = p_layers[index];
        const int fallback = p_engine.is_valid()
            ? p_engine->layer_perception_policy(layer_id)
            : int(HIDE);
        const int policy = p_decl.is_valid()
            ? p_decl->perception_policy_for(layer_id, fallback)
            : fallback;
        if (policy == HIDE) {
            hide = true;
            continue;
        }
        if (policy != CUSTOM) {
            continue;
        }
        const Callable callback = p_decl.is_valid()
            ? p_decl->custom_perception_for(layer_id)
            : Callable();
        if (!callback.is_valid()) {
            NETW_ERROR(
                sys::INTEREST,
                "Layer '%s' declares a CUSTOM perception whose callback is "
                "gone, so the subtree is hidden.",
                godot::String(layer_id)
            );
            hide = true;
            continue;
        }
        Array action;
        action.push_back(callback);
        action.push_back(layer_id);
        custom.push_back(action);
    }
    Dictionary out;
    out[hide_key()] = hide;
    out[custom_key()] = custom;
    return out;
}

void NetwInterestPerception::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_visible", "key", "visible"),
        &NetwInterestPerception::set_visible
    );
    ClassDB::bind_method(
        D_METHOD("is_known", "key"),
        &NetwInterestPerception::is_known
    );
    ClassDB::bind_method(
        D_METHOD("forget", "key"),
        &NetwInterestPerception::forget
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwInterestPerception::clear);
    ClassDB::bind_method(
        D_METHOD("arm", "key", "actions"),
        &NetwInterestPerception::arm
    );
    ClassDB::bind_method(
        D_METHOD("disarm", "key"),
        &NetwInterestPerception::disarm
    );
    ClassDB::bind_method(
        D_METHOD("armed_keys"),
        &NetwInterestPerception::armed_keys
    );
    ClassDB::bind_method(
        D_METHOD("resolve", "layers", "decl", "engine"),
        &NetwInterestPerception::resolve
    );
}

} // namespace netw

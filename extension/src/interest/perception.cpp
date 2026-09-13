#include "netw/interest/perception.hpp"

#include "netw/log.hpp"

using namespace godot;

namespace netw::interest {

namespace {

StringName hide_key() {
    return StringName("hide");
}

StringName custom_key() {
    return StringName("custom");
}

} // namespace

bool Perception::set_visible(int64_t key, bool p_visible) {
    const HashMap<int64_t, bool>::Iterator found = visible.find(key);
    if (found != visible.end() && found->value == p_visible) {
        return false;
    }
    visible[key] = p_visible;
    return true;
}

bool Perception::is_known(int64_t key) const {
    return visible.has(key);
}

void Perception::forget(int64_t key) {
    visible.erase(key);
}

void Perception::clear() {
    visible.clear();
    armed.clear();
}

void Perception::arm(int64_t key, const Array &p_actions) {
    if (p_actions.is_empty()) {
        return;
    }
    armed[key] = p_actions;
}

Array Perception::disarm(int64_t key) {
    const HashMap<int64_t, Array>::ConstIterator found = armed.find(key);
    if (!found) {
        return Array();
    }
    const Array out = found->value;
    armed.erase(key);
    return out;
}

PackedInt64Array Perception::armed_keys() const {
    PackedInt64Array out;
    for (const KeyValue<int64_t, Array> &entry : armed) {
        out.push_back(entry.key);
    }
    return out;
}

Dictionary Perception::resolve(
    const Array &p_layers,
    const Decl *p_decl,
    const Engine &p_engine
) const {
    bool hide = false;
    Array custom;
    for (int index = 0; index < p_layers.size(); ++index) {
        const StringName layer_id = p_layers[index];
        const int fallback = p_engine.layer_perception_policy(layer_id);
        const int policy = p_decl != nullptr
            ? p_decl->perception_policy_for(layer_id, fallback)
            : fallback;
        if (policy == HIDE) {
            hide = true;
            continue;
        }
        if (policy != CUSTOM) {
            continue;
        }
        const Callable callback = p_decl != nullptr
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

} // namespace netw::interest

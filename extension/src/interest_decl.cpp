#include "netw/interest_decl.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

int index_of(
    const LocalVector<StringName> &names,
    const StringName &name
) {
    for (uint32_t index = 0; index < names.size(); ++index) {
        if (names[index] == name) {
            return int(index);
        }
    }
    return -1;
}

bool guard_pair(
    const StringName &layer_id,
    int policy,
    int custom_policy,
    const Callable &custom,
    const char *verb
) {
    NETW_ERR_COND_V(
        layer_id.is_empty(),
        false,
        sys::INTEREST,
        "%s: layer id is empty.",
        verb
    );
    NETW_ERR_COND_V(
        policy < 0 || policy > custom_policy,
        false,
        sys::INTEREST,
        "%s: unknown policy %d.",
        verb,
        policy
    );
    NETW_ERR_COND_V(
        policy == custom_policy && !custom.is_valid(),
        false,
        sys::INTEREST,
        "%s: CUSTOM requires a callback.",
        verb
    );
    NETW_ERR_COND_V(
        policy != custom_policy && custom.is_valid(),
        false,
        sys::INTEREST,
        "%s: a callback requires CUSTOM.",
        verb
    );
    return true;
}

} // namespace

bool NetwInterestDecl::join(const StringName &layer_id) {
    NETW_ERR_COND_V(
        layer_id.is_empty(),
        false,
        sys::INTEREST,
        "NetwInterestDecl.join: layer id is empty."
    );
    if (index_of(label_ids, layer_id) >= 0) {
        return false;
    }
    label_ids.push_back(layer_id);
    return true;
}

bool NetwInterestDecl::leave(const StringName &layer_id) {
    const int at = index_of(label_ids, layer_id);
    if (at < 0) {
        return false;
    }
    label_ids.remove_at(uint32_t(at));
    return true;
}

bool NetwInterestDecl::has_label(const StringName &layer_id) const {
    return index_of(label_ids, layer_id) >= 0;
}

Array NetwInterestDecl::labels() const {
    Array out;
    for (uint32_t index = 0; index < label_ids.size(); ++index) {
        out.push_back(label_ids[index]);
    }
    return out;
}

bool NetwInterestDecl::set_leave_policy(
    const StringName &layer_id,
    int policy,
    const Callable &custom
) {
    if (!guard_pair(
            layer_id,
            policy,
            LEAVE_CUSTOM,
            custom,
            "NetwInterestDecl.set_leave_policy"
        )) {
        return false;
    }
    if (policy == LEAVE_CUSTOM) {
        leave_callbacks[layer_id] = custom;
    } else {
        leave_callbacks.erase(layer_id);
    }
    leave_policies[layer_id] = policy;
    return true;
}

bool NetwInterestDecl::set_perception_policy(
    const StringName &layer_id,
    int policy,
    const Callable &custom
) {
    if (!guard_pair(
            layer_id,
            policy,
            PERCEPTION_CUSTOM,
            custom,
            "NetwInterestDecl.set_perception_policy"
        )) {
        return false;
    }
    if (policy == PERCEPTION_CUSTOM) {
        perception_callbacks[layer_id] = custom;
    } else {
        perception_callbacks.erase(layer_id);
    }
    perception_policies[layer_id] = policy;
    return true;
}

int NetwInterestDecl::leave_policy_for(
    const StringName &layer_id,
    int fallback
) const {
    const HashMap<StringName, int32_t>::ConstIterator found
        = leave_policies.find(layer_id);
    return found ? found->value : fallback;
}

int NetwInterestDecl::perception_policy_for(
    const StringName &layer_id,
    int fallback
) const {
    const HashMap<StringName, int32_t>::ConstIterator found
        = perception_policies.find(layer_id);
    return found ? found->value : fallback;
}

Callable NetwInterestDecl::custom_leave_for(const StringName &layer_id) const {
    const HashMap<StringName, Callable>::ConstIterator found
        = leave_callbacks.find(layer_id);
    return found ? found->value : Callable();
}

Callable NetwInterestDecl::custom_perception_for(const StringName &layer_id
) const {
    const HashMap<StringName, Callable>::ConstIterator found
        = perception_callbacks.find(layer_id);
    return found ? found->value : Callable();
}

void NetwInterestDecl::clear() {
    label_ids.clear();
    leave_policies.clear();
    leave_callbacks.clear();
    perception_policies.clear();
    perception_callbacks.clear();
    reports_observers = false;
}

void NetwInterestDecl::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("join", "layer_id"),
        &NetwInterestDecl::join
    );
    ClassDB::bind_method(
        D_METHOD("leave", "layer_id"),
        &NetwInterestDecl::leave
    );
    ClassDB::bind_method(
        D_METHOD("has_label", "layer_id"),
        &NetwInterestDecl::has_label
    );
    ClassDB::bind_method(D_METHOD("labels"), &NetwInterestDecl::labels);
    ClassDB::bind_method(
        D_METHOD("set_leave_policy", "layer_id", "policy", "custom"),
        &NetwInterestDecl::set_leave_policy
    );
    ClassDB::bind_method(
        D_METHOD("set_perception_policy", "layer_id", "policy", "custom"),
        &NetwInterestDecl::set_perception_policy
    );
    ClassDB::bind_method(
        D_METHOD("leave_policy_for", "layer_id", "fallback"),
        &NetwInterestDecl::leave_policy_for
    );
    ClassDB::bind_method(
        D_METHOD("perception_policy_for", "layer_id", "fallback"),
        &NetwInterestDecl::perception_policy_for
    );
    ClassDB::bind_method(
        D_METHOD("custom_leave_for", "layer_id"),
        &NetwInterestDecl::custom_leave_for
    );
    ClassDB::bind_method(
        D_METHOD("custom_perception_for", "layer_id"),
        &NetwInterestDecl::custom_perception_for
    );
    ClassDB::bind_method(
        D_METHOD("set_reports_observers", "reports"),
        &NetwInterestDecl::set_reports_observers
    );
    ClassDB::bind_method(
        D_METHOD("get_reports_observers"),
        &NetwInterestDecl::get_reports_observers
    );
    ClassDB::bind_method(D_METHOD("clear"), &NetwInterestDecl::clear);

    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "reports_observers"),
        "set_reports_observers",
        "get_reports_observers"
    );
}

} // namespace netw

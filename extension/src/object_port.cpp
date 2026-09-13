#include "netw/object_port.hpp"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

bool ObjectPort::bound() const {
    return id.is_valid();
}

bool ObjectPort::bind(Object *p_owner) {
    if (p_owner == nullptr) {
        unbind();
        return false;
    }
    id = p_owner->get_instance_id();
    return true;
}

void ObjectPort::unbind() {
    id = ObjectID();
}

Object *ObjectPort::resolve(SubsystemName p_module) {
    if (!id.is_valid()) {
        return nullptr;
    }
    Object *found = gd::instance_from_id(id);
    if (found != nullptr) {
        return found;
    }
    id = ObjectID();
    lost += 1;
    NETW_WARN_ONCE(
        p_module,
        "a bound slot's owner was freed; the slot unbound itself and stopped "
        "touching properties"
    );
    return nullptr;
}

Dictionary port_capture(
    ObjectPort &r_port,
    SubsystemName p_module,
    const Array &p_keys
) {
    Dictionary out;
    Object *owner = r_port.resolve(p_module);
    if (owner == nullptr) {
        return out;
    }
    for (int at = 0; at < p_keys.size(); ++at) {
        const StringName key = p_keys[at];
        out[key] = owner->get(key);
    }
    return out;
}

bool port_apply(
    ObjectPort &r_port,
    SubsystemName p_module,
    const Dictionary &p_payload
) {
    Object *owner = r_port.resolve(p_module);
    if (owner == nullptr) {
        return false;
    }
    const Array keys = p_payload.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        owner->set(key, p_payload[key]);
    }
    return true;
}

void port_sync_transform(Object *p_owner) {
    const StringName sync_transform("force_update_transform");
    Node *node = Object::cast_to<Node>(p_owner);
    if (node != nullptr && node->is_inside_tree()
        && node->has_method(sync_transform)) {
        node->call(sync_transform);
    }
}

} // namespace netw

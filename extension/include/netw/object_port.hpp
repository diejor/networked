#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/subsystems.hpp"

namespace netw {

struct ObjectPort {
    godot::ObjectID id;
    int64_t lost = 0;

    bool bound() const;
    bool bind(godot::Object *p_owner);
    void unbind();
    godot::Object *resolve(SubsystemName p_module);
};

godot::Variant port_get(godot::Object *p_owner, const godot::StringName &p_key);

void port_set(
    godot::Object *p_owner,
    const godot::StringName &p_key,
    const godot::Variant &p_value
);

godot::Dictionary port_capture(
    ObjectPort &r_port,
    SubsystemName p_module,
    const godot::Array &p_keys
);

bool port_apply(
    ObjectPort &r_port,
    SubsystemName p_module,
    const godot::Dictionary &p_payload
);

void port_sync_transform(godot::Object *p_owner);

} // namespace netw

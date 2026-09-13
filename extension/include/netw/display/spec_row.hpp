#pragma once

#include "godot/node.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/object_port.hpp"

namespace netw::display {

struct SpecRow {
    ObjectPort node;
    godot::StringName source_prop;
    godot::StringName target_prop;
    godot::Ref<NetwInterpolate> spec;

    static SpecRow of_property(
        godot::Node *p_node,
        const godot::StringName &p_source_prop,
        const godot::Ref<NetwInterpolate> &p_spec
    );
    static SpecRow of_argument(
        godot::Node *p_node,
        const godot::Ref<NetwInterpolate> &p_spec
    );

    godot::Node *node_ptr() const;
    bool has_source() const {
        return !source_prop.is_empty();
    }
    bool is_displayable() const;
};

} // namespace netw::display

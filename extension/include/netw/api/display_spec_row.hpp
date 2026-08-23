#pragma once

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/object_port.hpp"

namespace netw {

class NetwDisplaySpecRow : public godot::RefCounted {
    GDCLASS(NetwDisplaySpecRow, godot::RefCounted)

private:
    ObjectPort node;
    godot::StringName source_prop;
    godot::StringName target_prop;
    godot::Ref<NetwInterpolate> spec;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwDisplaySpecRow> of_property(
        godot::Node *p_node,
        const godot::StringName &p_source_prop,
        const godot::Ref<NetwInterpolate> &p_spec
    );
    static godot::Ref<NetwDisplaySpecRow> of_argument(
        godot::Node *p_node,
        const godot::Ref<NetwInterpolate> &p_spec
    );

    void set_node(godot::Object *p_node);
    godot::Variant get_node() const;
    godot::Node *node_ptr() const;

    void set_source_prop(const godot::StringName &p_prop) {
        source_prop = p_prop;
    }
    godot::StringName get_source_prop() const { return source_prop; }

    void set_target_prop(const godot::StringName &p_prop) {
        target_prop = p_prop;
    }
    godot::StringName get_target_prop() const { return target_prop; }

    void set_spec(const godot::Ref<NetwInterpolate> &p_spec) {
        spec = p_spec;
    }
    godot::Ref<NetwInterpolate> get_spec() const { return spec; }

    bool has_source() const { return !source_prop.is_empty(); }
    bool is_displayable() const;
};

} // namespace netw

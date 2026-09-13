#include "netw/display/spec_row.hpp"

#include "godot/object.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw::display {

SpecRow SpecRow::of_property(
    Node *p_node,
    const StringName &p_source_prop,
    const Ref<NetwInterpolate> &p_spec
) {
    SpecRow row;
    row.node.bind(p_node);
    row.source_prop = p_source_prop;
    row.spec = p_spec;
    row.target_prop = (p_spec.is_valid() && !p_spec->get_target().is_empty())
        ? p_spec->get_target()
        : p_source_prop;
    return row;
}

SpecRow SpecRow::of_argument(Node *p_node, const Ref<NetwInterpolate> &p_spec) {
    SpecRow row;
    row.node.bind(p_node);
    row.spec = p_spec;
    row.target_prop = p_spec.is_valid() ? p_spec->get_target() : StringName();
    return row;
}

Node *SpecRow::node_ptr() const {
    return Object::cast_to<Node>(
        const_cast<ObjectPort &>(node).resolve(sys::INTERPOLATION)
    );
}

bool SpecRow::is_displayable() const {
    return spec.is_valid() && spec->get_mode() != NetwInterpolate::MODE_NONE
        && !target_prop.is_empty() && node_ptr() != nullptr;
}

} // namespace netw::display

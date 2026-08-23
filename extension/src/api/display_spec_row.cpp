#include "netw/api/display_spec_row.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

Ref<NetwDisplaySpecRow> NetwDisplaySpecRow::of_property(
    Node *p_node,
    const StringName &p_source_prop,
    const Ref<NetwInterpolate> &p_spec
) {
    Ref<NetwDisplaySpecRow> row;
    row.instantiate();
    row->set_node(p_node);
    row->source_prop = p_source_prop;
    row->spec = p_spec;
    row->target_prop
        = (p_spec.is_valid() && !p_spec->get_target().is_empty())
        ? p_spec->get_target()
        : p_source_prop;
    return row;
}

Ref<NetwDisplaySpecRow> NetwDisplaySpecRow::of_argument(
    Node *p_node,
    const Ref<NetwInterpolate> &p_spec
) {
    Ref<NetwDisplaySpecRow> row;
    row.instantiate();
    row->set_node(p_node);
    row->spec = p_spec;
    row->target_prop = p_spec.is_valid() ? p_spec->get_target() : StringName();
    return row;
}

void NetwDisplaySpecRow::set_node(Object *p_node) {
    node.bind(p_node);
}

Variant NetwDisplaySpecRow::get_node() const {
    return gd::held(node_ptr());
}

Node *NetwDisplaySpecRow::node_ptr() const {
    return Object::cast_to<Node>(
        const_cast<ObjectPort &>(node).resolve(sys::INTERPOLATION)
    );
}

bool NetwDisplaySpecRow::is_displayable() const {
    return spec.is_valid() && spec->get_mode() != NetwInterpolate::MODE_NONE
        && !target_prop.is_empty() && node_ptr() != nullptr;
}

void NetwDisplaySpecRow::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwDisplaySpecRow",
        D_METHOD("of_property", "node", "source_prop", "spec"),
        &NetwDisplaySpecRow::of_property
    );
    ClassDB::bind_static_method(
        "NetwDisplaySpecRow",
        D_METHOD("of_argument", "node", "spec"),
        &NetwDisplaySpecRow::of_argument
    );
    ClassDB::bind_method(
        D_METHOD("is_displayable"),
        &NetwDisplaySpecRow::is_displayable
    );

#define NETW_SPEC_ROW_PROPERTY(m_type, m_name)                                 \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwDisplaySpecRow::set_##m_name                                      \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwDisplaySpecRow::get_##m_name                                      \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

    NETW_SPEC_ROW_PROPERTY(Variant::OBJECT, node);
    NETW_SPEC_ROW_PROPERTY(Variant::STRING_NAME, source_prop);
    NETW_SPEC_ROW_PROPERTY(Variant::STRING_NAME, target_prop);
    NETW_SPEC_ROW_PROPERTY(Variant::OBJECT, spec);

#undef NETW_SPEC_ROW_PROPERTY
}

} // namespace netw

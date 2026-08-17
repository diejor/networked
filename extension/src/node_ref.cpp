#include "netw/node_ref.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

Ref<NetwNodeRef> NetwNodeRef::create(
    int64_t p_route,
    int64_t p_comp,
    const String &p_path
) {
    Ref<NetwNodeRef> out;
    out.instantiate();
    out->route = p_route;
    out->comp = p_comp;
    out->path = p_path;
    return out;
}

void NetwNodeRef::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwNodeRef",
        D_METHOD("create", "route", "comp", "path"),
        &NetwNodeRef::create,
        DEFVAL(0),
        DEFVAL(0),
        DEFVAL(String())
    );
    ClassDB::bind_method(D_METHOD("get_route"), &NetwNodeRef::get_route);
    ClassDB::bind_method(D_METHOD("set_route", "route"), &NetwNodeRef::set_route);
    ClassDB::bind_method(D_METHOD("get_comp"), &NetwNodeRef::get_comp);
    ClassDB::bind_method(D_METHOD("set_comp", "comp"), &NetwNodeRef::set_comp);
    ClassDB::bind_method(D_METHOD("get_path"), &NetwNodeRef::get_path);
    ClassDB::bind_method(D_METHOD("set_path", "path"), &NetwNodeRef::set_path);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "route"),
        "set_route",
        "get_route"
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "comp"), "set_comp", "get_comp");
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "path"),
        "set_path",
        "get_path"
    );
}

} // namespace netw

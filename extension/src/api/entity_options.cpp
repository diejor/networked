#include "netw/api/entity_options.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

Ref<NetwDespawnOpts> NetwDespawnOpts::create(const StringName &p_reason) {
    Ref<NetwDespawnOpts> out;
    out.instantiate();
    out->reason = p_reason;
    return out;
}

#define NETW_OPTION(m_class, m_type, m_name) \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &m_class::get_##m_name); \
    ClassDB::bind_method( \
        D_METHOD("set_" #m_name, #m_name), \
        &m_class::set_##m_name \
    ); \
    ADD_PROPERTY(PropertyInfo(m_type, #m_name), "set_" #m_name, "get_" #m_name)

void NetwDespawnOpts::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwDespawnOpts",
        D_METHOD("create", "reason"),
        &NetwDespawnOpts::create,
        DEFVAL(StringName())
    );
    NETW_OPTION(NetwDespawnOpts, Variant::STRING_NAME, reason);
    NETW_OPTION(NetwDespawnOpts, Variant::BOOL, flush_save);
    NETW_OPTION(NetwDespawnOpts, Variant::BOOL, defer_free);
    NETW_OPTION(NetwDespawnOpts, Variant::BOOL, linger);
    NETW_OPTION(NetwDespawnOpts, Variant::FLOAT, linger_seconds);
}

void NetwReparentOpts::_bind_methods() {
    NETW_OPTION(NetwReparentOpts, Variant::STRING_NAME, reason);
}

void NetwControlRequest::_bind_methods() {
    ClassDB::bind_method(D_METHOD("deny"), &NetwControlRequest::deny);
    NETW_OPTION(NetwControlRequest, Variant::INT, requester);
    NETW_OPTION(NetwControlRequest, Variant::BOOL, denied);
}

#undef NETW_OPTION

} // namespace netw

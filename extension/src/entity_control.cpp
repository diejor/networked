#include "netw/entity_control.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

int64_t NetwEntityControl::resolve(int64_t p_peer_id) const {
    if (configured) {
        return controller;
    }
    return initial == int(InitialController::REPRESENTED_PEER) ? p_peer_id : 0;
}

bool NetwEntityControl::set_controller(int64_t p_controller) {
    const int64_t previous = controller;
    configured = true;
    controller = p_controller;
    return previous != p_controller;
}

bool NetwEntityControl::policy_admits(
    int p_policy,
    int64_t p_sender,
    int64_t p_authority,
    int64_t p_controller
) {
    switch (WritePolicy(p_policy)) {
        case WritePolicy::AUTHORITY:
            return p_sender == p_authority;
        case WritePolicy::CONTROLLER:
            return p_sender == p_controller;
        case WritePolicy::ANY_PEER:
            return true;
    }
    return false;
}

bool NetwEntityControl::controlled_by(
    int64_t p_local_peer,
    int64_t p_peer_id
) const {
    if (p_local_peer == 0) {
        return false;
    }
    const int64_t steering = resolve(p_peer_id);
    return steering != 0 && steering == p_local_peer;
}

NetwEntityControl::Verdict NetwEntityControl::disconnect_verdict(
    int64_t p_disconnected,
    int64_t p_peer_id
) const {
    if (p_disconnected == 0) {
        return NOTHING;
    }
    if (p_peer_id == p_disconnected) {
        return DESPAWN_REPRESENTED;
    }
    if (resolve(p_peer_id) != p_disconnected) {
        return NOTHING;
    }
    return on_disconnect == int(DisconnectRule::DESPAWN)
        ? DESPAWN_CONTROLLER
        : REVERT_TO_SERVER;
}

void NetwEntityControl::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("resolve", "peer_id"),
        &NetwEntityControl::resolve
    );
    ClassDB::bind_method(
        D_METHOD("set_controller", "controller"),
        &NetwEntityControl::set_controller
    );
    ClassDB::bind_method(
        D_METHOD("controlled_by", "local_peer", "peer_id"),
        &NetwEntityControl::controlled_by
    );
    ClassDB::bind_static_method(
        "NetwEntityControl",
        D_METHOD("policy_admits", "policy", "sender", "authority", "controller"),
        &NetwEntityControl::policy_admits
    );
    ClassDB::bind_method(
        D_METHOD("admits_request"),
        &NetwEntityControl::admits_request
    );
    ClassDB::bind_method(
        D_METHOD("disconnect_verdict", "disconnected", "peer_id"),
        &NetwEntityControl::disconnect_verdict
    );

#define NETW_CONTROL_READ(m_type, m_name)                                      \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &NetwEntityControl::get_##m_name); \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "",                                                                    \
        "get_" #m_name                                                         \
    )

#define NETW_CONTROL_POLICY(m_name)                                            \
    ClassDB::bind_method(D_METHOD("get_" #m_name), &NetwEntityControl::get_##m_name); \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwEntityControl::set_##m_name                                       \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(Variant::INT, #m_name),                                   \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

    NETW_CONTROL_READ(Variant::INT, controller);
    NETW_CONTROL_READ(Variant::BOOL, configured);
    NETW_CONTROL_POLICY(initial);
    NETW_CONTROL_POLICY(transfer);
    NETW_CONTROL_POLICY(on_disconnect);

#undef NETW_CONTROL_READ
#undef NETW_CONTROL_POLICY

    BIND_ENUM_CONSTANT(NOTHING);
    BIND_ENUM_CONSTANT(DESPAWN_REPRESENTED);
    BIND_ENUM_CONSTANT(REVERT_TO_SERVER);
    BIND_ENUM_CONSTANT(DESPAWN_CONTROLLER);
}

} // namespace netw

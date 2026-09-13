#include "netw/api/nodes/service.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/object.hpp"

using namespace godot;

namespace netw {

namespace {

Callable &restricted_probe() {
    static Callable probe;
    return probe;
}

bool in_editor() {
    Engine *engine = Engine::get_singleton();
    return engine != nullptr && engine->is_editor_hint();
}

} // namespace

void NetwService::set_transport_restricted_probe(const Callable &p_probe) {
    restricted_probe() = p_probe;
}

Callable NetwService::get_transport_restricted_probe() {
    return restricted_probe();
}

bool NetwService::is_transport_restricted() {
    const Callable &probe = restricted_probe();
    return probe.is_valid() && bool(probe.call());
}

NetwMultiplayer *NetwService::resolve_api(Node *p_service) {
    if (p_service == nullptr) {
        return nullptr;
    }
    NetwMultiplayer *api = Object::cast_to<NetwMultiplayer>(
        NetwMultiplayer::session_of(p_service).ptr()
    );
    if (api != nullptr) {
        return api;
    }
    for (Node *walked = p_service; walked != nullptr;
         walked = walked->get_parent()) {
        NetwMultiplayer *owned = NetwMultiplayer::core_rooted_at(walked);
        if (owned != nullptr) {
            return owned;
        }
    }
    return nullptr;
}

void NetwService::register_on_session(
    Node *p_service,
    const Ref<Script> &p_type
) {
    NetwMultiplayer *api = Object::cast_to<NetwMultiplayer>(
        NetwMultiplayer::session_of(p_service).ptr()
    );
    if (api != nullptr) {
        api->service_register(p_service, p_type.ptr());
    }
}

void NetwService::unregister_from_session(
    Node *p_service,
    const Ref<Script> &p_type
) {
    NetwMultiplayer *api = Object::cast_to<NetwMultiplayer>(
        NetwMultiplayer::session_of(p_service).ptr()
    );
    if (api != nullptr) {
        api->service_unregister(p_service, p_type.ptr());
    }
}

Ref<Script> NetwService::service_type() {
    Ref<Script> declared;
    GDVIRTUAL_CALL(_service_type, declared);
    return declared;
}

void NetwService::service_entered(NetwMultiplayer *) {
}

void NetwService::service_exiting(NetwMultiplayer *) {
}

bool NetwService::should_register() {
    bool wanted = true;
    if (GDVIRTUAL_CALL(_should_register, wanted)) {
        return wanted;
    }
    return true;
}

void NetwService::enter_registry() {
    if (in_editor() || !should_register()) {
        return;
    }
    NetwMultiplayer *api = resolve_api(this);
    if (api == nullptr) {
        return;
    }
    const Ref<Script> declared = service_type();
    api->service_register(this, declared.ptr());
    service_entered(api);
    GDVIRTUAL_CALL(_service_entered, Ref<NetwMultiplayer>(api));
}

void NetwService::exit_registry() {
    if (in_editor() || !should_register()) {
        return;
    }
    NetwMultiplayer *api = resolve_api(this);
    if (api == nullptr) {
        return;
    }
    GDVIRTUAL_CALL(_service_exiting, Ref<NetwMultiplayer>(api));
    service_exiting(api);
    const Ref<Script> declared = service_type();
    api->service_unregister(this, declared.ptr());
}

void NetwService::_notification(int p_what) {
    switch (p_what) {
        case NOTIFICATION_ENTER_TREE:
            enter_registry();
            break;
        case NOTIFICATION_EXIT_TREE:
            exit_registry();
            break;
        default:
            break;
    }
}

void NetwService::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwService",
        D_METHOD("set_transport_restricted_probe", "probe"),
        &NetwService::set_transport_restricted_probe
    );
    ClassDB::bind_static_method(
        "NetwService",
        D_METHOD("get_transport_restricted_probe"),
        &NetwService::get_transport_restricted_probe
    );
    ClassDB::bind_static_method(
        "NetwService",
        D_METHOD("is_transport_restricted"),
        &NetwService::is_transport_restricted
    );
    ClassDB::bind_static_method(
        "NetwService",
        D_METHOD("register", "service", "type"),
        &NetwService::register_on_session,
        DEFVAL(Ref<Script>())
    );
    ClassDB::bind_static_method(
        "NetwService",
        D_METHOD("unregister", "service", "type"),
        &NetwService::unregister_from_session,
        DEFVAL(Ref<Script>())
    );

    GDVIRTUAL_BIND(_service_type);
    GDVIRTUAL_BIND(_should_register);
    GDVIRTUAL_BIND(_service_entered, "api");
    GDVIRTUAL_BIND(_service_exiting, "api");
}

} // namespace netw

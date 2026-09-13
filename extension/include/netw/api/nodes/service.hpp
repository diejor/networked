#pragma once

#include "netw/api/netw_multiplayer.hpp"

#include "godot/callable.hpp"
#include "godot/gdvirtual.hpp"
#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwService : public godot::Node {
    GDCLASS(NetwService, godot::Node)

    static NetwMultiplayer *resolve_api(godot::Node *p_service);

    void enter_registry();
    void exit_registry();

protected:
    static void _bind_methods();
    void _notification(int p_what);

    GDVIRTUAL0R(godot::Ref<godot::Script>, _service_type)
    GDVIRTUAL0R(bool, _should_register)
    GDVIRTUAL1(_service_entered, godot::Ref<NetwMultiplayer>)
    GDVIRTUAL1(_service_exiting, godot::Ref<NetwMultiplayer>)

public:
    virtual void service_entered(NetwMultiplayer *p_api);
    virtual void service_exiting(NetwMultiplayer *p_api);

    static void set_transport_restricted_probe(const godot::Callable &p_probe);
    static godot::Callable get_transport_restricted_probe();
    static bool is_transport_restricted();

    static void register_on_session(
        godot::Node *p_service,
        const godot::Ref<godot::Script> &p_type
    );
    static void unregister_from_session(
        godot::Node *p_service,
        const godot::Ref<godot::Script> &p_type
    );

    godot::Ref<godot::Script> service_type();
    bool should_register();
};

} // namespace netw

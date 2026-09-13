#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwMultiplayer;

class NetwConnectHandle : public godot::RefCounted {
    GDCLASS(NetwConnectHandle, godot::RefCounted)

    struct EndpointKey {
        godot::StringName peer_class;
        godot::String address;
    };

    godot::ObjectID session_id;
    godot::HashMap<godot::RID, EndpointKey> endpoint_keys;

    NetwMultiplayer *session() const;

    godot::RID resolve_transport(const godot::Variant &p_of) const;
    godot::RID resolve_endpoint(
        const godot::Variant &p_transport,
        const godot::String &p_address
    ) const;
    godot::Dictionary transport_snapshot(const godot::RID &p_transport) const;
    godot::Dictionary endpoint_snapshot(const godot::RID &p_target) const;
    void remember_endpoint(const godot::RID &p_target);

    void relay_join_failed(int64_t p_error, const godot::String &p_reason);
    void relay_endpoint_added(const godot::RID &p_target);
    void relay_endpoint_removed(const godot::RID &p_target);
    void relay_endpoint_updated(const godot::RID &p_target);

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    godot::RID create_peer(
        const godot::Variant &p_transport,
        int64_t p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings,
        const godot::Callable &p_completed,
        const godot::Callable &p_progress
    );
    void cancel_peer_creation(const godot::RID &p_ticket);

    godot::String get_join_address() const;
    godot::Dictionary diagnostics(int64_t p_peer_id) const;
    godot::Array transports() const;
    godot::Dictionary transport(const godot::Variant &p_of) const;
    bool register_transport(const godot::Ref<godot::Script> &p_type);
    bool unregister_transport(const godot::Variant &p_of);
    godot::Array join_schema() const;

    godot::Array endpoints() const;
    godot::Dictionary endpoint(
        const godot::Variant &p_transport,
        const godot::String &p_address
    ) const;
    bool endpoint_add(
        const godot::Variant &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    void endpoint_remove(
        const godot::Variant &p_transport,
        const godot::String &p_address
    );
    godot::Error endpoint_set_display_name(
        const godot::Variant &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    void endpoint_probe(
        const godot::Variant &p_transport,
        const godot::String &p_address
    );
    void endpoint_refresh();
};

} // namespace netw

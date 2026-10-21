#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/endpoint.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwMultiplayer;

class NetwConnection : public godot::RefCounted, public enums::NetwConnection {
    GDCLASS(NetwConnection, godot::RefCounted)

    godot::ObjectID session_id;
    godot::Ref<NetwEndpoint> current_endpoint;
    godot::HashMap<godot::RID, godot::Ref<NetwEndpoint>> endpoints_by_row;

    NetwMultiplayer *session() const;

    godot::RID resolve_transport(const godot::Variant &p_of) const;
    godot::RID resolve_endpoint(
        const godot::Variant &p_transport,
        const godot::String &p_address
    ) const;
    godot::Dictionary transport_snapshot(const godot::RID &p_transport) const;
    godot::Ref<NetwEndpoint> remember_endpoint(const godot::RID &p_target);

protected:
    static void _bind_methods();

public:
    void bind_session(NetwMultiplayer *p_session);

    void announce_endpoint_added(const godot::RID &p_target);
    void announce_endpoint_removed(const godot::RID &p_target);
    void announce_endpoint_updated(const godot::RID &p_target);
    void announce_attempt_moved(int64_t p_state);
    void announce_progressed(const godot::String &p_message, double p_ratio);
    bool announce_failed(int64_t p_error, const godot::String &p_reason);
    void announce_join_address_changed(const godot::String &p_address);
    void announce_invited(const godot::RID &p_target);

    godot::RID create_peer(
        const godot::Variant &p_transport,
        enums::NetwConnection::TransportMode p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings,
        const godot::Callable &p_completed
    );
    void cancel_peer_creation(const godot::RID &p_ticket);

    godot::Ref<NetwPromise> join(
        const godot::Ref<NetwEndpoint> &p_endpoint,
        const godot::Dictionary &p_settings
    );
    godot::Ref<NetwPromise> host(
        const godot::Variant &p_transport,
        const godot::Dictionary &p_settings
    );
    void cancel();
    godot::Error set_defaults(
        const godot::Variant &p_transport,
        const godot::Dictionary &p_settings
    );
    godot::Dictionary defaults(const godot::Variant &p_transport) const;

    godot::String get_join_address() const;
    void set_join_address(const godot::String &p_address);
    int64_t get_max_players() const;
    godot::Ref<NetwEndpoint> get_current_endpoint();
    godot::Dictionary diagnostics(int64_t p_peer_id) const;
    godot::Array transports() const;
    godot::Dictionary transport(const godot::Variant &p_of) const;
    bool register_transport(const godot::Ref<godot::Script> &p_type);
    bool unregister_transport(const godot::Variant &p_of);
    godot::Array join_schema() const;

    godot::TypedArray<NetwEndpoint> endpoints();
    godot::Ref<NetwEndpoint> add(
        const godot::Variant &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    godot::Ref<NetwEndpoint> invite(
        const godot::Variant &p_transport,
        const godot::String &p_address,
        const godot::String &p_from_name
    );
    godot::Ref<NetwEndpoint> find(
        const godot::Variant &p_transport,
        const godot::String &p_address
    );
    godot::Error remove(const godot::Ref<NetwEndpoint> &p_endpoint);
    void set_browsing(bool p_browsing);
    bool is_browsing() const;
    void set_probe_interval(double p_seconds);
    double get_probe_interval() const;
};

} // namespace netw

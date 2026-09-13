#pragma once

#include <cstdint>
#include <optional>

#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/server_info.hpp"
#include "netw/connect/browse_list.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/probe_client.hpp"
#include "netw/connect/transport.hpp"

namespace netw {

class NetwMultiplayer;

} // namespace netw

namespace netw::connect {

std::optional<JoinRequest> request_of(
    const godot::StringName &p_username,
    const godot::Array &p_args
);

class ConnectCore {
public:
    ConnectCore() = default;
    ~ConnectCore();

    void bind_session(NetwMultiplayer *p_session);
    void dispose();

    godot::RID create_peer(
        const godot::RID &p_transport,
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings,
        const godot::Callable &p_completed,
        const godot::Callable &p_progress
    );
    void cancel_peer_creation(const godot::RID &p_ticket);

    godot::RID target_add(
        const godot::RID &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    void target_remove(const godot::RID &p_target);
    bool target_is_available(const godot::RID &p_target) const;
    void probe_target(const godot::RID &p_target);
    void on_directory_delivered(
        const godot::Ref<godot::MultiplayerPeer> &p_peer,
        int64_t p_directory
    );
    void on_directory_failed(
        int64_t p_error,
        const godot::String &p_message,
        int64_t p_directory
    );
    void on_directory_listed(
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos,
        int64_t p_directory
    );
    void publish_targets(
        const godot::StringName &p_peer_class,
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos
    );

    godot::String join_address() const;
    godot::Dictionary diagnostics(int64_t p_peer_id) const;
    bool transport_facts(
        const TransportSlot &p_slot,
        TransportFacts &r_facts
    ) const;

    void on_peer_assigned(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    void on_poll(double p_delta);

    BrowseList &list() {
        return rows;
    }
    const BrowseList &list() const {
        return rows;
    }
    TransportRegistry &registry() {
        return registrations;
    }
    const TransportRegistry &registry() const {
        return registrations;
    }
    godot::RID register_transport(const godot::Ref<godot::Script> &p_type);
    godot::Error unregister_transport(const godot::RID &p_transport);
    void refresh();

    ProbeClient *probe_client() const {
        return prober;
    }

private:
    NetwMultiplayer *session = nullptr;
    Transport *live = nullptr;
    BrowseList rows;
    TransportRegistry registrations;
    godot::LocalVector<Transport *> browsers;
    ProbeClient *prober = nullptr;

    godot::LocalVector<godot::RID> creations;
    godot::LocalVector<godot::RID> starts;
    godot::RID answering_offer;

    godot::RID mint_ticket_for(
        Transport *p_made,
        int p_mode,
        const godot::String &p_address
    );
    void start_creation(const godot::RID &p_ticket);
    void settle_creation(
        const godot::RID &p_ticket,
        const godot::Ref<godot::MultiplayerPeer> &p_peer,
        godot::Error p_error,
        const godot::String &p_detail
    );
    void discard_creation(const godot::RID &p_ticket);
    void forget_creation(const godot::RID &p_ticket);
    bool claim_offer(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    Transport *make_transport(const godot::StringName &p_peer_class);
    Transport *make_transport(const TransportSlot &p_slot) const;
    Transport *transport_of_directory(int64_t p_directory) const;
    ProbeHooks probe_hooks() const;
    godot::Ref<NetwPromise> probe_row(const godot::RID &p_target);
    Transport *transport_of_target(
        const godot::RID &p_target,
        godot::String &r_address,
        godot::Dictionary &r_metadata
    ) const;
    void adopt_live(Transport *p_transport);
    void drop_live();
    void open_browsers();
    void drop_browsers();
};

} // namespace netw::connect

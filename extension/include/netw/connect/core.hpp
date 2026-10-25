#pragma once

#include <cstdint>
#include <optional>

#include "godot/hash_map.hpp"
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
        const godot::Callable &p_completed
    );
    void cancel_peer_creation(const godot::RID &p_ticket);

    godot::Ref<NetwPromise> join(
        const godot::RID &p_transport,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    );
    godot::Ref<NetwPromise> join_target(
        const godot::RID &p_target,
        const godot::Dictionary &p_settings
    );
    godot::Ref<NetwPromise> host(
        const godot::RID &p_transport,
        const godot::Dictionary &p_settings
    );
    void cancel_attempt();
    int attempt_state() const {
        return attempt.state;
    }
    bool answers_attempt() const;
    void on_progress(
        const godot::RID &p_ticket,
        const godot::String &p_message,
        double p_ratio
    );
    void on_session_edge(int p_old, int p_new);
    void on_server_answered();
    void on_session_online();
    void on_session_left();
    void on_transport_failed();

    godot::Error set_defaults(
        const godot::RID &p_transport,
        const godot::Dictionary &p_settings
    );
    godot::Dictionary defaults_of(const godot::RID &p_transport) const;

    godot::RID target_add(
        const godot::RID &p_transport,
        const godot::String &p_address,
        const godot::String &p_display_name
    );
    godot::RID target_find(
        const godot::RID &p_transport,
        const godot::String &p_address
    ) const;
    godot::String target_address(
        const godot::RID &p_transport,
        const godot::String &p_address
    ) const;
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
    godot::RID invite(
        const godot::RID &p_transport,
        const godot::String &p_address,
        const godot::String &p_from_name
    );
    void publish_targets(
        const godot::StringName &p_peer_class,
        godot::ObjectID p_source,
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos
    );
    void forget_directory(godot::ObjectID p_directory);
    void watch_directory(godot::Object *p_directory);

    godot::String join_address() const;
    godot::String endpoint_address() const;
    void advertise(const godot::String &p_address, int64_t p_max_players);
    int64_t advertised_max_players() const;
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
    void set_browsing(bool p_browsing);
    bool is_browsing() const {
        return browsing;
    }
    void set_probe_interval(double p_seconds);
    double get_probe_interval() const {
        return probe_interval;
    }

    ProbeClient *probe_client() const {
        return prober;
    }
    const Transport *live_transport() const {
        return live;
    }

private:
    struct Browser {
        Transport *transport = nullptr;
        double next_list = 0.0;
        bool listed = false;
    };

    struct Attempt {
        int state = ATTEMPT_OFFLINE;
        godot::RID ticket;
        int mode = PEER_MODE_CLIENT;
        double elapsed = 0.0;
        double deadline = 0.0;
        bool reported = false;
        bool answered = false;
        godot::Ref<NetwPromise> promise;
    };

    NetwMultiplayer *session = nullptr;
    Transport *live = nullptr;
    BrowseList rows;
    TransportRegistry registrations;
    godot::LocalVector<Browser> browsers;
    ProbeClient *prober = nullptr;
    bool browsing = false;
    double probe_interval = 10.0;

    godot::LocalVector<godot::RID> creations;
    godot::LocalVector<godot::RID> starts;
    godot::RID answering_offer;
    Attempt attempt;
    godot::HashMap<godot::StringName, godot::Dictionary> defaults;
    godot::String announced_address;
    godot::RID joined_target;
    godot::String joined_address;
    godot::String advertised_address;
    int64_t advertised_players = 0;

    godot::RID open_creation(
        const godot::RID &p_transport,
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_metadata,
        const godot::Dictionary &p_settings,
        const godot::Callable &p_completed
    );
    godot::Dictionary layered_settings(
        const godot::RID &p_transport,
        const godot::Dictionary &p_metadata,
        const godot::Dictionary &p_settings
    ) const;
    godot::Ref<NetwPromise> start_attempt(
        const godot::RID &p_transport,
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_metadata,
        const godot::Dictionary &p_settings
    );
    void begin_attempt(
        const godot::RID &p_ticket,
        int p_mode,
        const godot::Ref<NetwPromise> &p_promise
    );
    void move_attempt(int p_state);
    void end_attempt(godot::Error p_error, const godot::String &p_reason);
    void release_attempt(const Attempt &p_ended);
    void announce_end(
        const Attempt &p_ended,
        godot::Error p_error,
        const godot::String &p_reason
    );
    void tick_attempt(double p_delta);
    void announce_join_address();
    void release_live();
    bool live_online() const;

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
    Transport *make_creator(const godot::StringName &p_peer_class) const;
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
    bool open_browser(Transport *p_made);
    void open_browsers();
    void drop_browsers();
    void tick_browsing(double p_delta);
    void start_due_probes();
    void drop_row_probes();
};

} // namespace netw::connect

#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/link/schedule.hpp"

namespace netw {

class LocalLoopbackSession;

class LocalMultiplayerPeer : public MultiplayerPeerBase {
    GDCLASS(LocalMultiplayerPeer, MultiplayerPeerBase)

    friend class LocalLoopbackSession;

    struct Packet {
        godot::PackedByteArray data;
        int peer = 0;
        int channel = 0;
        TransferMode mode = TRANSFER_MODE_RELIABLE;
        uint32_t term = 0;
    };

    struct Install {
        uint32_t term = 0;
        int successor_was = 0;
        int former_now = 0;

        int relabelled(int p_id) const;
    };

    godot::HashMap<int, godot::ObjectID> links;
    godot::ObjectID session;

    godot::Vector<Packet> packet_queue;
    godot::Vector<Packet> packets_ahead;
    Packet retained_packet;
    godot::Vector<int> peers_to_emit_connected;
    godot::Vector<int> peers_to_emit_disconnected;
    godot::Vector<Install> pending_installs;

    uint32_t term = 0;
    int label = 0;
    bool relays = true;
    int unique_id = 0;
    int target_peer = 0;
    int transfer_channel = 0;
    TransferMode transfer_mode = TRANSFER_MODE_RELIABLE;
    bool server_side = false;
    bool closed = false;
    bool closing = false;
    int refused_send_count = 0;
    int delivered_send_count = 0;
    ConnectionStatus status = CONNECTION_DISCONNECTED;

    LocalMultiplayerPeer *peer_at(int p_peer_id) const;
    godot::Error send_to_peer(
        int p_peer_id,
        const godot::PackedByteArray &p_buffer
    );
    void receive_packet(const Packet &p_packet);
    void admit_packet(const Packet &p_packet);
    bool reads_any_term() const;
    void install(const Install &p_install);
    void purge_packets_from(int p_sender_id);
    void remote_closed(int p_remote_id, bool p_remote_was_server);
    void vanish();
    void finalize_close();
    void reset_state();

    godot::PackedByteArray take_packet();
    godot::Error send_packet(const godot::PackedByteArray &p_buffer);

protected:
    static void _bind_methods();

public:
    godot::Error create_server(int p_unique_id = 1);
    godot::Error create_client(int p_client_id);

    void force_connect_peer(int p_peer_id, LocalMultiplayerPeer *p_peer);
    bool is_linked_to(int p_peer_id) const;
    bool has_link_listener() const;
    godot::PackedInt32Array linked_peer_ids() const;

    void set_loopback_session(LocalLoopbackSession *p_session);
    LocalLoopbackSession *loopback_session() const;
    godot::Ref<LocalLoopbackSession> get_loopback_session() const;
#if defined(NETW_TESTS)
    godot::Error transfer_server(int p_peer_id);
#endif

    void NETW_PEER_VIRTUAL(set_transfer_channel)(int p_channel) override;
    int NETW_PEER_VIRTUAL(get_transfer_channel)() const override;
    void NETW_PEER_VIRTUAL(set_transfer_mode)(TransferMode p_mode) override;
    TransferMode NETW_PEER_VIRTUAL(get_transfer_mode)() const override;
    void NETW_PEER_VIRTUAL(set_refuse_new_connections)(bool p_enable) override;
    bool NETW_PEER_VIRTUAL(is_refusing_new_connections)() const override;
    bool NETW_PEER_VIRTUAL(is_server_relay_supported)() const override;

    void NETW_PEER_VIRTUAL(set_target_peer)(int p_peer) override;
    int NETW_PEER_VIRTUAL(get_packet_peer)() const override;
    TransferMode NETW_PEER_VIRTUAL(get_packet_mode)() const override;
    int NETW_PEER_VIRTUAL(get_packet_channel)() const override;
    int NETW_PEER_VIRTUAL(get_available_packet_count)() const override;
    int NETW_PEER_VIRTUAL(get_max_packet_size)() const override;

    void NETW_PEER_VIRTUAL(disconnect_peer)(
        int p_peer,
        bool p_force = false
    ) override;
    bool NETW_PEER_VIRTUAL(is_server)() const override;
    void NETW_PEER_VIRTUAL(poll)() override;
    void NETW_PEER_VIRTUAL(close)() override;
    int NETW_PEER_VIRTUAL(get_unique_id)() const override;
    ConnectionStatus NETW_PEER_VIRTUAL(get_connection_status)() const override;

#if defined(NETW_MODULE)
    godot::Error get_packet(
        const uint8_t **r_buffer,
        int &r_buffer_size
    ) override;
    godot::Error put_packet(
        const uint8_t *p_buffer,
        int p_buffer_size
    ) override;
#else
    godot::PackedByteArray _get_packet_script() override;
    godot::Error _put_packet_script(
        const godot::PackedByteArray &p_buffer
    ) override;
#endif

    int refused_sends() const {
        return refused_send_count;
    }
    int delivered_sends() const {
        return delivered_send_count;
    }
    void clear_refused_sends() {
        refused_send_count = 0;
        delivered_send_count = 0;
    }
};

class LocalLoopbackSession : public godot::Resource {
    GDCLASS(LocalLoopbackSession, godot::Resource)

    friend class LocalMultiplayerPeer;

    static constexpr double RELEASE_EPSILON_MS = 0.0001;

    struct InFlight {
        LocalMultiplayerPeer::Packet packet;
        double due_time_ms = 0.0;
        uint64_t seq = 0;
    };

    struct InFlightOrder {
        bool operator()(const InFlight &a, const InFlight &b) const;
    };

    struct LinkState {
        godot::HashMap<int, godot::Ref<NetwLinkConditions>>
            conditions_by_sender;
        link::Draws draws;
        godot::HashMap<godot::String, uint64_t> ordered_by_channel;
        godot::Vector<InFlight> in_flight;
        uint64_t seq = 0;
        bool held = false;

        bool has_conditions() const;
        bool is_idle() const;
    };

    static godot::Ref<LocalLoopbackSession> shared;

    godot::Ref<LocalMultiplayerPeer> server_peer;
    godot::Vector<godot::Ref<LocalMultiplayerPeer>> client_peers;
    godot::StringName server_app_id;

    double clock_ms = 0.0;
    int64_t last_scoped_poll_frame = -1;
    godot::HashMap<uint64_t, LinkState> links;
    godot::HashSet<int> used_ids;
    uint32_t transfer_term = 0;
    bool transfer_enabled = false;

    int draw_fresh_id();
    void link_to_clients(LocalMultiplayerPeer *p_client);
    LocalMultiplayerPeer *live_client_labelled(int p_label) const;
    godot::Error hand_over(LocalMultiplayerPeer *p_successor, int p_former_now);
    void relabel_link(
        const LocalMultiplayerPeer *p_peer,
        const LocalMultiplayerPeer::Install &p_install
    );

    LinkState &ensure_link(const LocalMultiplayerPeer *p_peer);
    LinkState *link_of(const LocalMultiplayerPeer *p_peer);

    void poll_peers();
    void poll_or_hold(LocalMultiplayerPeer *p_peer);
    void capture_held_packets(LocalMultiplayerPeer *p_peer);
    void capture_queued_packets(LocalMultiplayerPeer *p_peer);
    void release_due(LocalMultiplayerPeer *p_peer, bool p_flush_all = false);
    void drop_link_if_idle(const LocalMultiplayerPeer *p_peer);

    bool capture_incoming(
        const LocalMultiplayerPeer *p_peer,
        const LocalMultiplayerPeer::Packet &p_packet
    );
    void capture_packet(
        LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet,
        const godot::Ref<NetwLinkConditions> &p_conditions
    );
    void enqueue(
        LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet,
        double p_due_time_ms
    );

    godot::Ref<NetwLinkConditions> conditions_for(
        const LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet
    ) const;

protected:
    static void _bind_methods();

public:
    static godot::Ref<LocalLoopbackSession> get_shared_session();
    static void set_shared_session(
        const godot::Ref<LocalLoopbackSession> &p_session
    );
    static bool has_shared_session();

    bool has_live_server() const;
    void init_server_side();
    godot::Ref<LocalMultiplayerPeer> create_client_peer();
    godot::Ref<LocalMultiplayerPeer> get_server_peer();
    godot::Ref<LocalMultiplayerPeer> get_client_peer();
    godot::Array get_client_peers() const;

    void set_server_app_id(const godot::StringName &p_app_id);
    godot::StringName get_server_app_id() const;

    void poll();
    void poll_frame_scoped();
    void advance_time(double p_ms);

    void hold_inbound_packets(LocalMultiplayerPeer *p_peer);
    void release_inbound_packets(LocalMultiplayerPeer *p_peer);

    void set_link_conditions(
        LocalMultiplayerPeer *p_peer,
        const godot::Ref<NetwLinkConditions> &p_conditions,
        int p_sender_id = 0
    );
    void clear_link_conditions(
        LocalMultiplayerPeer *p_peer,
        int p_sender_id = 0
    );
    void clear_all_link_conditions();
    godot::Ref<NetwLinkConditions> get_link_conditions(
        const LocalMultiplayerPeer *p_peer,
        int p_sender_id = 0
    ) const;

    void purge_packets_from(int p_sender_id);

    int in_flight_count(const LocalMultiplayerPeer *p_peer) const;
    bool is_holding_inbound(const LocalMultiplayerPeer *p_peer) const;

    void enable_server_transfer();
    godot::Error transfer_server(int p_successor, bool p_keep_former);
    godot::Error lose_server();

    bool checks_terms = true;

    void reset();
};

} // namespace netw

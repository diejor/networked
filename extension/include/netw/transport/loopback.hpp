#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/random.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class LocalLoopbackSession;

// Declarative inbound impairment for one loopback link, in human units. The
// numbers are read at receive time and never quantized to the physics rate, so
// a latency shorter than one frame stays expressible.
class LocalLinkConditions : public godot::RefCounted {
    GDCLASS(LocalLinkConditions, godot::RefCounted)

    static constexpr double DEFAULT_RETRANSMIT_MS = 30.0;
    // A negative retransmit means "derive it from the latency when it is read".
    static constexpr double AUTO_RETRANSMIT = -1.0;

    double latency_ms = 0.0;
    double jitter_ms = 0.0;
    double packet_loss = 0.0;
    double reorder = 0.0;
    double duplicate = 0.0;
    double throttle = 0.0;
    double throttle_ms = 0.0;
    double retransmit_ms = AUTO_RETRANSMIT;
    int64_t seed = 0;

protected:
    static void _bind_methods();

public:
    // A registered class's `new()` takes no arguments, so the seed arrives
    // through a static instead.
    static godot::Ref<LocalLinkConditions> create(int64_t p_seed = 0);

    static godot::Ref<LocalLinkConditions> perfect();
    static godot::Ref<LocalLinkConditions> wifi();
    static godot::Ref<LocalLinkConditions> mobile_4g();
    static godot::Ref<LocalLinkConditions> poor_3g();
    static godot::Ref<LocalLinkConditions> satellite();

    // Converts a deterministic test cadence into the same millisecond unit a
    // real link uses. The default is Godot's default physics cadence.
    static godot::Ref<LocalLinkConditions> polls(
        int p_count,
        double p_period_ms = 1000.0 / 60.0
    );

    void set_latency_ms(double p_value);
    double get_latency_ms() const;
    void set_jitter_ms(double p_value);
    double get_jitter_ms() const;
    void set_packet_loss(double p_value);
    double get_packet_loss() const;
    void set_reorder(double p_value);
    double get_reorder() const;
    void set_duplicate(double p_value);
    double get_duplicate() const;
    void set_throttle(double p_value);
    double get_throttle() const;
    void set_throttle_ms(double p_value);
    double get_throttle_ms() const;
    void set_retransmit_ms(double p_value);
    double get_retransmit_ms() const;
    void set_seed(int64_t p_value);
    int64_t get_seed() const;

    godot::Ref<LocalLinkConditions> clone() const;

    double effective_latency_ms() const;
    double effective_retransmit_ms() const;
};

// An in-process peer that routes packets through memory instead of a socket.
// Peer IDs, connection status and packet queues answer exactly what
// ENetMultiplayerPeer's do, so a session cannot tell which one it is holding.
class LocalMultiplayerPeer : public MultiplayerPeerBase {
    GDCLASS(LocalMultiplayerPeer, MultiplayerPeerBase)

    friend class LocalLoopbackSession;

    struct Packet {
        godot::PackedByteArray data;
        int peer = 0;
        int channel = 0;
        TransferMode mode = TRANSFER_MODE_RELIABLE;
    };

    godot::HashMap<int, godot::ObjectID> links;
    godot::ObjectID session;

    godot::Vector<Packet> packet_queue;
    Packet current_packet;
    godot::Vector<int> peers_to_emit_connected;
    godot::Vector<int> peers_to_emit_disconnected;

    int unique_id = 0;
    int target_peer = 0;
    int transfer_channel = 0;
    TransferMode transfer_mode = TRANSFER_MODE_RELIABLE;
    bool server_side = false;
    bool closed = false;
    bool closing = false;
    ConnectionStatus status = CONNECTION_DISCONNECTED;

    LocalMultiplayerPeer *peer_at(int p_peer_id) const;
    godot::Error send_to_peer(
        int p_peer_id,
        const godot::PackedByteArray &p_buffer
    );
    void receive_packet(const Packet &p_packet);
    void purge_packets_from(int p_sender_id);
    void remote_closed(int p_remote_id, bool p_remote_was_server);
    void finalize_close();
    void reset_state();

    godot::PackedByteArray take_packet();
    godot::Error send_packet(const godot::PackedByteArray &p_buffer);

protected:
    static void _bind_methods();

public:
    godot::Error create_server();
    godot::Error create_client(int p_client_id);

    void force_connect_peer(int p_peer_id, LocalMultiplayerPeer *p_peer);
    bool is_linked_to(int p_peer_id) const;
    godot::PackedInt32Array linked_peer_ids() const;

    void set_loopback_session(LocalLoopbackSession *p_session);
    LocalLoopbackSession *get_loopback_session() const;

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

    // The one place the tiers differ in signature and not only in spelling:
    // the engine hands a raw buffer up from PacketPeer, and the extension base
    // hands a PackedByteArray down from the script surface.
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
};

// The in-process session that links peers to each other and simulates the link
// between them. Time advances only when something advances it, so a delay is
// counted in polls rather than in wall clock and a run reproduces.
class LocalLoopbackSession : public godot::Resource {
    GDCLASS(LocalLoopbackSession, godot::Resource)

    friend class LocalMultiplayerPeer;

    // Absorbs float-accumulation error so a packet due exactly on a poll
    // boundary releases on that poll rather than the next one.
    static constexpr double RELEASE_EPSILON_MS = 0.0001;

    struct InFlight {
        LocalMultiplayerPeer::Packet packet;
        double due_time_ms = 0.0;
        int seq = 0;
    };

    struct InFlightOrder {
        bool operator()(const InFlight &a, const InFlight &b) const;
    };

    struct LinkState {
        godot::HashMap<int, godot::Ref<LocalLinkConditions>>
            conditions_by_sender;
        godot::HashMap<godot::String, godot::Ref<godot::RandomNumberGenerator>>
            rng_by_stream;
        godot::HashMap<godot::String, double> reliable_due_by_channel;
        godot::Vector<InFlight> in_flight;
        double throttle_until_ms = 0.0;
        int seq = 0;
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
    void capture_reliable(
        LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet,
        const godot::Ref<LocalLinkConditions> &p_conditions
    );
    void capture_unreliable(
        LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet,
        const godot::Ref<LocalLinkConditions> &p_conditions
    );
    void enqueue(
        LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet,
        double p_due_time_ms
    );

    godot::Ref<LocalLinkConditions> conditions_for(
        const LinkState &p_state,
        const LocalMultiplayerPeer::Packet &p_packet
    ) const;
    godot::Ref<godot::RandomNumberGenerator> rng_for(
        LinkState &p_state,
        int p_sender_id,
        const char *p_stream,
        int64_t p_seed
    );
    bool roll(
        LinkState &p_state,
        int p_sender_id,
        const char *p_stream,
        int64_t p_seed,
        double p_probability
    );
    double draw_jitter(
        LinkState &p_state,
        int p_sender_id,
        const godot::Ref<LocalLinkConditions> &p_conditions
    );
    void clear_sender_streams(LinkState &p_state, int p_sender_id);
    void prune_reliable_due(LinkState &p_state);

protected:
    static void _bind_methods();

public:
    // The process-wide session, minted on first access. `has_shared_session`
    // is the way to ask whether one exists without minting one.
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
    // Advances by however many physics frames have actually passed, so a
    // latency in milliseconds stays a latency in milliseconds no matter how
    // many idle frames the engine ran.
    void poll_frame_scoped();
    void advance_time(double p_ms);

    void hold_inbound_packets(LocalMultiplayerPeer *p_peer);
    void release_inbound_packets(LocalMultiplayerPeer *p_peer);

    void set_link_conditions(
        LocalMultiplayerPeer *p_peer,
        const godot::Ref<LocalLinkConditions> &p_conditions,
        int p_sender_id = 0
    );
    void clear_link_conditions(
        LocalMultiplayerPeer *p_peer,
        int p_sender_id = 0
    );
    void clear_all_link_conditions();
    godot::Ref<LocalLinkConditions> get_link_conditions(
        const LocalMultiplayerPeer *p_peer,
        int p_sender_id = 0
    ) const;

    void purge_packets_from(int p_sender_id);

    int in_flight_count(const LocalMultiplayerPeer *p_peer) const;
    bool is_holding_inbound(const LocalMultiplayerPeer *p_peer) const;

    // Closes every peer and empties the session so a new server can host.
    void reset();
};

} // namespace netw

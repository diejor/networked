#pragma once

#if defined(DEBUG_ENABLED)

#include "godot/multiplayer.hpp"
#include "godot/templates.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/link/schedule.hpp"

namespace netw::link {

class ShapedPeer : public MultiplayerPeerBase {
    GDCLASS(ShapedPeer, MultiplayerPeerBase)

    struct Packet {
        godot::PackedByteArray bytes;
        int peer = 0;
        int channel = 0;
        TransferMode mode = TRANSFER_MODE_RELIABLE;
        double due_ms = 0.0;
        uint64_t sequence = 0;
    };

    struct PacketOrder {
        bool operator()(const Packet &p_left, const Packet &p_right) const;
    };

    godot::Ref<godot::MultiplayerPeer> base;
    godot::Ref<NetwLinkConditions> conditions;
    godot::HashMap<int, Draws> outbound_draws;
    godot::HashMap<int, Draws> inbound_draws;
    godot::HashMap<godot::String, uint64_t> outbound_ordered;
    godot::HashMap<godot::String, uint64_t> inbound_ordered;
    godot::HashSet<int> peers;
    godot::Vector<Packet> outbound;
    godot::Vector<Packet> inbound;
    godot::Vector<Packet> available;
    Packet retained;
    uint64_t sequence = 0;
    int target = 0;
    int channel = 0;
    TransferMode mode = TRANSFER_MODE_RELIABLE;
    bool closed = false;

    void enqueue(
        godot::Vector<Packet> &p_queue,
        godot::HashMap<int, Draws> &p_draws,
        Packet p_packet,
        double p_now_ms
    );
    void release(double p_now_ms);
    void purge(int p_peer);
    godot::Error send(const godot::PackedByteArray &p_bytes);
    godot::PackedByteArray take();

protected:
    static void _bind_methods();

public:
    void initialize(
        const godot::Ref<godot::MultiplayerPeer> &p_base,
        const godot::Ref<NetwLinkConditions> &p_conditions
    );
    void on_peer_connected(int p_peer);
    void on_peer_disconnected(int p_peer);
    ~ShapedPeer();

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
};

} // namespace netw::link

#endif

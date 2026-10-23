#pragma once

#include "godot/multiplayer.hpp"
#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "modules/enet/enet_connection.h"
#include "modules/enet/enet_multiplayer_peer.h"
#include "modules/enet/enet_packet_peer.h"

namespace godot {
using ::ENetConnection;
using ::ENetMultiplayerPeer;
using ::ENetPacketPeer;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/e_net_connection.hpp>
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#endif

namespace netw::gd {

inline void disconnect_server_later(
    const godot::Ref<godot::MultiplayerPeer> &p_link
) {
    godot::ENetMultiplayerPeer *enet
        = godot::Object::cast_to<godot::ENetMultiplayerPeer>(p_link.ptr());
    if (enet == nullptr
        || enet->get_connection_status()
            != godot::MultiplayerPeer::CONNECTION_CONNECTED
        || !enet->is_server_relay_supported() || enet->get_unique_id() == 1) {
        return;
    }
    const godot::Ref<godot::ENetPacketPeer> server = enet->get_peer(1);
    if (server.is_null()
        || server->get_state() != godot::ENetPacketPeer::STATE_CONNECTED) {
        return;
    }
    server->peer_disconnect_later();
    const godot::Ref<godot::ENetConnection> host = enet->get_host();
    if (host.is_valid()) {
        host->flush();
    }
}

inline void disconnect_clients_later(
    const godot::Ref<godot::MultiplayerPeer> &p_link,
    const godot::PackedInt32Array &p_clients
) {
    godot::ENetMultiplayerPeer *enet
        = godot::Object::cast_to<godot::ENetMultiplayerPeer>(p_link.ptr());
    if (enet == nullptr
        || enet->get_connection_status()
            != godot::MultiplayerPeer::CONNECTION_CONNECTED
        || enet->get_unique_id() != 1) {
        return;
    }
    for (int at = 0; at < p_clients.size(); ++at) {
        const godot::Ref<godot::ENetPacketPeer> client
            = enet->get_peer(p_clients[at]);
        if (client.is_valid()
            && client->get_state() == godot::ENetPacketPeer::STATE_CONNECTED) {
            client->peer_disconnect_later();
        }
    }
    const godot::Ref<godot::ENetConnection> host = enet->get_host();
    if (host.is_valid()) {
        host->flush();
    }
}

inline bool peer_is_relayed(
    const godot::Ref<godot::SceneMultiplayer> &p_api,
    int64_t p_peer
) {
    if (p_api.is_null() || p_peer == 1 || p_peer == 0) {
        return false;
    }
    if (p_api->get_unique_id() == 1 || !p_api->is_server_relay_enabled()) {
        return false;
    }
    const godot::Ref<godot::MultiplayerPeer> link
        = p_api->get_multiplayer_peer();
    return link.is_valid() && link->is_server_relay_supported();
}

inline bool peer_can_receive(
    const godot::Ref<godot::MultiplayerPeer> &p_link,
    int64_t p_peer
) {
    const godot::ENetMultiplayerPeer *enet
        = godot::Object::cast_to<godot::ENetMultiplayerPeer>(p_link.ptr());
    if (enet == nullptr) {
        return true;
    }
    const godot::Ref<godot::ENetPacketPeer> held
        = enet->get_peer(int32_t(p_peer));
    if (held.is_null()) {
        return false;
    }
    switch (held->get_state()) {
        case godot::ENetPacketPeer::STATE_DISCONNECT_LATER:
        case godot::ENetPacketPeer::STATE_DISCONNECTING:
        case godot::ENetPacketPeer::STATE_ACKNOWLEDGING_DISCONNECT:
        case godot::ENetPacketPeer::STATE_ZOMBIE:
        case godot::ENetPacketPeer::STATE_DISCONNECTED:
            return false;
        default:
            break;
    }
    return held->get_channels() > 0;
}

} // namespace netw::gd

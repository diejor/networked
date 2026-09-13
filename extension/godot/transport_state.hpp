#pragma once

#include "godot/multiplayer.hpp"
#include "godot/ref_counted.hpp"

#if defined(NETW_MODULE)
#include "modules/enet/enet_multiplayer_peer.h"
#include "modules/enet/enet_packet_peer.h"

namespace godot {
using ::ENetMultiplayerPeer;
using ::ENetPacketPeer;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/e_net_multiplayer_peer.hpp>
#include <godot_cpp/classes/e_net_packet_peer.hpp>
#endif

namespace netw::gd {

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

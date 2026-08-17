#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "modules/multiplayer/scene_multiplayer.h"
#include "scene/main/multiplayer_api.h"
#include "scene/main/multiplayer_peer.h"

namespace godot {
using ::MultiplayerAPI;
using ::MultiplayerAPIExtension;
using ::MultiplayerPeer;
using ::MultiplayerPeerExtension;
using ::SceneMultiplayer;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/multiplayer_api_extension.hpp>
#include <godot_cpp/classes/multiplayer_peer.hpp>
#include <godot_cpp/classes/multiplayer_peer_extension.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::PackedInt32Array api_peer_ids(
    const godot::Ref<godot::MultiplayerAPI> &p_api
) {
    if (p_api.is_null()) {
        return godot::PackedInt32Array();
    }
#if defined(NETW_MODULE)
    return p_api->get_peer_ids();
#else
    return p_api->get_peers();
#endif
}

} // namespace netw::gd

namespace netw {

#if defined(NETW_MODULE)
using MultiplayerPeerBase = godot::MultiplayerPeer;
using MultiplayerApiBase = godot::MultiplayerAPI;
#else
using MultiplayerPeerBase = godot::MultiplayerPeerExtension;
using MultiplayerApiBase = godot::MultiplayerAPIExtension;
#endif

} // namespace netw

#if defined(NETW_MODULE)
#define NETW_PEER_VIRTUAL(m_name) m_name
#define NETW_API_VIRTUAL(m_name) m_name
#define NETW_API_CONST
#define NETW_API_CONFIG_ARG godot::Variant
#else
#define NETW_PEER_VIRTUAL(m_name) _##m_name
#define NETW_API_VIRTUAL(m_name) _##m_name
#define NETW_API_CONST const
#define NETW_API_CONFIG_ARG const godot::Variant &
#endif

#pragma once

#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/object/class_db.h"
#include "modules/webrtc/webrtc_data_channel.h"
#include "modules/webrtc/webrtc_peer_connection.h"

namespace godot {
using ::WebRTCDataChannel;
using ::WebRTCPeerConnection;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/web_rtc_data_channel.hpp>
#include <godot_cpp/classes/web_rtc_peer_connection.hpp>
#include <godot_cpp/classes/web_rtc_peer_connection_extension.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::Ref<godot::WebRTCPeerConnection> new_peer_connection() {
#if defined(NETW_MODULE)
    return godot::Ref<godot::WebRTCPeerConnection>(
        godot::WebRTCPeerConnection::create()
    );
#else
    const godot::Variant held
        = godot::ClassDBSingleton::get_singleton()->instantiate(
            godot::StringName("WebRTCPeerConnection")
        );
    godot::Object *made = held;
    return godot::Ref<godot::WebRTCPeerConnection>(
        godot::Object::cast_to<godot::WebRTCPeerConnection>(made)
    );
#endif
}

inline godot::Ref<godot::MultiplayerPeer> new_webrtc_multiplayer_peer() {
#if defined(NETW_MODULE)
    godot::Object *made
        = ::ClassDB::instantiate(godot::StringName("WebRTCMultiplayerPeer"));
#else
    const godot::Variant held
        = godot::ClassDBSingleton::get_singleton()->instantiate(
            godot::StringName("WebRTCMultiplayerPeer")
        );
    godot::Object *made = held;
#endif
    godot::MultiplayerPeer *peer
        = godot::Object::cast_to<godot::MultiplayerPeer>(made);
    if (peer == nullptr) {
        if (made != nullptr
            && godot::Object::cast_to<godot::RefCounted>(made) == nullptr) {
            memdelete(made);
        }
        return godot::Ref<godot::MultiplayerPeer>();
    }
    return godot::Ref<godot::MultiplayerPeer>(peer);
}

inline godot::Error webrtc_create_server(
    const godot::Ref<godot::MultiplayerPeer> &p_peer
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(int(p_peer->call(godot::StringName("create_server"))));
}

inline godot::Error webrtc_create_client(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    int64_t p_id
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_peer->call(godot::StringName("create_client"), p_id))
    );
}

inline godot::Error webrtc_add_peer(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    const godot::Ref<godot::WebRTCPeerConnection> &p_connection,
    int64_t p_id
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_peer->call(godot::StringName("add_peer"), p_connection, p_id))
    );
}

inline bool webrtc_has_peer(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    int64_t p_id
) {
    if (p_peer.is_null()) {
        return false;
    }
    return bool(p_peer->call(godot::StringName("has_peer"), p_id));
}

inline godot::Dictionary webrtc_peers(
    const godot::Ref<godot::MultiplayerPeer> &p_peer
) {
    if (p_peer.is_null()) {
        return godot::Dictionary();
    }
    return p_peer->call(godot::StringName("get_peers"));
}

inline godot::Ref<godot::WebRTCPeerConnection> webrtc_connection_of(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    int64_t p_id
) {
    if (p_peer.is_null()) {
        return godot::Ref<godot::WebRTCPeerConnection>();
    }
    const godot::Variant row
        = p_peer->call(godot::StringName("get_peer"), p_id);
    if (row.get_type() != godot::Variant::DICTIONARY) {
        return godot::Ref<godot::WebRTCPeerConnection>();
    }
    const godot::Dictionary held = row;
    const godot::Variant found = held.get("connection", godot::Variant());
    godot::Object *made = found;
    return godot::Ref<godot::WebRTCPeerConnection>(
        godot::Object::cast_to<godot::WebRTCPeerConnection>(made)
    );
}

} // namespace netw::gd

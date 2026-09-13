#pragma once

#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/object/class_db.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::Ref<godot::MultiplayerPeer> new_peer(
    const godot::StringName &p_class
) {
#if defined(NETW_MODULE)
    godot::Object *made = ::ClassDB::instantiate(p_class);
#else
    const godot::Variant held
        = godot::ClassDBSingleton::get_singleton()->instantiate(p_class);
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

inline godot::Error enet_create_server(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    int64_t p_port,
    int64_t p_max_clients
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_peer->call(
            godot::StringName("create_server"),
            p_port,
            p_max_clients
        ))
    );
}

inline godot::Error enet_create_client(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    const godot::String &p_host,
    int64_t p_port
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_peer->call(godot::StringName("create_client"), p_host, p_port))
    );
}

inline int64_t enet_local_port(
    const godot::Ref<godot::MultiplayerPeer> &p_peer
) {
    if (p_peer.is_null()) {
        return 0;
    }
    const godot::Variant host = p_peer->call(godot::StringName("get_host"));
    godot::Object *connection = gd::live_object(host);
    if (connection == nullptr) {
        return 0;
    }
    return int64_t(connection->call(godot::StringName("get_local_port")));
}

inline godot::Error websocket_create_server(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    int64_t p_port
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_peer->call(godot::StringName("create_server"), p_port))
    );
}

inline godot::Error websocket_create_client(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    const godot::String &p_url
) {
    if (p_peer.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_peer->call(godot::StringName("create_client"), p_url))
    );
}

inline void websocket_set_outbound_buffer(
    const godot::Ref<godot::MultiplayerPeer> &p_peer,
    int64_t p_bytes
) {
    if (p_peer.is_valid()) {
        p_peer->call(godot::StringName("set_outbound_buffer_size"), p_bytes);
    }
}

} // namespace netw::gd

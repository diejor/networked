#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/object/class_db.h"
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

enum WebSocketState {
    WEB_SOCKET_CONNECTING = 0,
    WEB_SOCKET_OPEN = 1,
    WEB_SOCKET_CLOSING = 2,
    WEB_SOCKET_CLOSED = 3,
};

inline godot::Ref<godot::RefCounted> new_web_socket() {
#if defined(NETW_MODULE)
    godot::Object *made
        = ::ClassDB::instantiate(godot::StringName("WebSocketPeer"));
#else
    const godot::Variant held
        = godot::ClassDBSingleton::get_singleton()->instantiate(
            godot::StringName("WebSocketPeer")
        );
    godot::Object *made = held;
#endif
    godot::RefCounted *socket = godot::Object::cast_to<godot::RefCounted>(made);
    if (socket == nullptr) {
        if (made != nullptr) {
            memdelete(made);
        }
        return godot::Ref<godot::RefCounted>();
    }
    return godot::Ref<godot::RefCounted>(socket);
}

inline godot::Error web_socket_connect(
    const godot::Ref<godot::RefCounted> &p_socket,
    const godot::String &p_url
) {
    if (p_socket.is_null()) {
        return godot::ERR_INVALID_PARAMETER;
    }
    return godot::Error(
        int(p_socket->call(godot::StringName("connect_to_url"), p_url))
    );
}

inline void web_socket_poll(const godot::Ref<godot::RefCounted> &p_socket) {
    if (p_socket.is_valid()) {
        p_socket->call(godot::StringName("poll"));
    }
}

inline int web_socket_state(const godot::Ref<godot::RefCounted> &p_socket) {
    if (p_socket.is_null()) {
        return WEB_SOCKET_CLOSED;
    }
    return int(p_socket->call(godot::StringName("get_ready_state")));
}

inline int64_t web_socket_pending(
    const godot::Ref<godot::RefCounted> &p_socket
) {
    if (p_socket.is_null()) {
        return 0;
    }
    return int64_t(
        p_socket->call(godot::StringName("get_available_packet_count"))
    );
}

inline godot::PackedByteArray web_socket_take(
    const godot::Ref<godot::RefCounted> &p_socket
) {
    if (p_socket.is_null()) {
        return godot::PackedByteArray();
    }
    return p_socket->call(godot::StringName("get_packet"));
}

inline void web_socket_send_text(
    const godot::Ref<godot::RefCounted> &p_socket,
    const godot::String &p_text
) {
    if (p_socket.is_valid()) {
        p_socket->call(godot::StringName("send_text"), p_text);
    }
}

inline void web_socket_close(const godot::Ref<godot::RefCounted> &p_socket) {
    if (p_socket.is_valid()) {
        p_socket->call(godot::StringName("close"));
    }
}

} // namespace netw::gd

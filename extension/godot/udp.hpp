#pragma once

#include <cstdint>

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw::gd {

struct Datagram {
    godot::PackedByteArray bytes;
    godot::String ip;
    int64_t port = 0;
};

inline godot::Ref<godot::RefCounted> udp_bind(int64_t p_port) {
    godot::Ref<godot::RefCounted> made
        = instantiate_class(godot::StringName("PacketPeerUDP"));
    if (made.is_null()) {
        return made;
    }
    made->call(godot::StringName("set_broadcast_enabled"), true);
    const godot::Error bound = godot::Error(
        int(made->call(godot::StringName("bind"), p_port, godot::String("*")))
    );
    return bound == godot::OK ? made : godot::Ref<godot::RefCounted>();
}

inline int64_t udp_local_port(const godot::Ref<godot::RefCounted> &p_socket) {
    return p_socket.is_null()
        ? 0
        : int64_t(p_socket->call(godot::StringName("get_local_port")));
}

inline godot::Error udp_send(
    const godot::Ref<godot::RefCounted> &p_socket,
    const godot::String &p_ip,
    int64_t p_port,
    const godot::PackedByteArray &p_bytes
) {
    if (p_socket.is_null()) {
        return godot::ERR_UNCONFIGURED;
    }
    p_socket->call(godot::StringName("set_dest_address"), p_ip, p_port);
    return godot::Error(
        int(p_socket->call(godot::StringName("put_packet"), p_bytes))
    );
}

inline bool udp_take(
    const godot::Ref<godot::RefCounted> &p_socket,
    Datagram &r_datagram
) {
    if (p_socket.is_null()
        || int64_t(
               p_socket->call(godot::StringName("get_available_packet_count"))
           ) <= 0) {
        return false;
    }
    r_datagram.bytes = p_socket->call(godot::StringName("get_packet"));
    r_datagram.ip = p_socket->call(godot::StringName("get_packet_ip"));
    r_datagram.port
        = int64_t(p_socket->call(godot::StringName("get_packet_port")));
    return true;
}

inline void udp_close(const godot::Ref<godot::RefCounted> &p_socket) {
    if (p_socket.is_valid()) {
        p_socket->call(godot::StringName("close"));
    }
}

} // namespace netw::gd

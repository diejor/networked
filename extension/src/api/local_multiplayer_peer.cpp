#include "netw/api/loopback.hpp"

#include <cstring>

#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr int MAX_PACKET_SIZE = 16777215;

} // namespace

Error LocalMultiplayerPeer::create_server() {
    reset_state();
    unique_id = 1;
    server_side = true;
    status = CONNECTION_CONNECTED;
    NETW_INFO(sys::TRANSPORT, "peer is the server id=%d", unique_id);
    return Error::OK;
}

Error LocalMultiplayerPeer::create_client(int p_client_id) {
    reset_state();
    unique_id = p_client_id;
    server_side = false;
    status = CONNECTION_CONNECTING;
    NETW_INFO(sys::TRANSPORT, "peer is a client id=%d", unique_id);
    return Error::OK;
}

void LocalMultiplayerPeer::force_connect_peer(
    int p_peer_id,
    LocalMultiplayerPeer *p_peer
) {
    if (closed || closing) {
        return;
    }

    links.insert(p_peer_id, gd::instance_id(p_peer));
    NETW_TRACE(sys::TRANSPORT, "linked to peer=%d", p_peer_id);

    if (server_side) {
        peers_to_emit_connected.push_back(p_peer_id);
    }
}

bool LocalMultiplayerPeer::is_linked_to(int p_peer_id) const {
    return links.has(p_peer_id);
}

bool LocalMultiplayerPeer::has_link_listener() const {
    return has_connections(StringName("peer_connected"));
}

PackedInt32Array LocalMultiplayerPeer::linked_peer_ids() const {
    PackedInt32Array ids;
    for (const KeyValue<int, ObjectID> &link : links) {
        ids.push_back(link.key);
    }
    return ids;
}

void LocalMultiplayerPeer::set_loopback_session(
    LocalLoopbackSession *p_session
) {
    session = gd::instance_id(p_session);
}

LocalLoopbackSession *LocalMultiplayerPeer::loopback_session() const {
    return Object::cast_to<LocalLoopbackSession>(gd::instance_from_id(session));
}

Ref<LocalLoopbackSession> LocalMultiplayerPeer::get_loopback_session() const {
    return Ref<LocalLoopbackSession>(loopback_session());
}

LocalMultiplayerPeer *LocalMultiplayerPeer::peer_at(int p_peer_id) const {
    const ObjectID *found = links.getptr(p_peer_id);
    if (found == nullptr) {
        return nullptr;
    }
    return Object::cast_to<LocalMultiplayerPeer>(gd::instance_from_id(*found));
}

Error LocalMultiplayerPeer::send_packet(const PackedByteArray &p_buffer) {
    NETW_ZONE_NC("LocalMultiplayerPeer send", colors::TRANSPORT);
    NETW_ZONE_VALUE(p_buffer.size());
    if (closed || closing) {
        return Error::ERR_UNAVAILABLE;
    }

    if (target_peer == 0) {
        for (const int peer_id : linked_peer_ids()) {
            send_to_peer(peer_id, p_buffer);
        }
        return Error::OK;
    }

    if (target_peer < 0) {
        for (const int peer_id : linked_peer_ids()) {
            if (peer_id != -target_peer) {
                send_to_peer(peer_id, p_buffer);
            }
        }
        return Error::OK;
    }

    if (!server_side && target_peer != 1) {
        return send_to_peer(1, p_buffer);
    }

    return send_to_peer(target_peer, p_buffer);
}

Error LocalMultiplayerPeer::send_to_peer(
    int p_peer_id,
    const PackedByteArray &p_buffer
) {
    if (closed || closing) {
        ++refused_send_count;
        return Error::ERR_UNAVAILABLE;
    }

    LocalMultiplayerPeer *target = peer_at(p_peer_id);
    if (target == nullptr || target->closed || target->closing
        || !links.has(p_peer_id)) {
        links.erase(p_peer_id);
        ++refused_send_count;
        NETW_TRACE(sys::TRANSPORT, "send refused, peer gone=%d", p_peer_id);
        return Error::ERR_UNAVAILABLE;
    }

    ++delivered_send_count;

    Packet packet;
    packet.data = p_buffer;
    packet.peer = unique_id;
    packet.channel = transfer_channel;
    packet.mode = transfer_mode;
    target->receive_packet(packet);
    return Error::OK;
}

void LocalMultiplayerPeer::receive_packet(const Packet &p_packet) {
    NETW_ZONE_NC("LocalMultiplayerPeer receive", colors::TRANSPORT);
    NETW_ZONE_VALUE(p_packet.data.size());
    if (closed || closing) {
        return;
    }

    LocalLoopbackSession *owner = loopback_session();
    if (owner != nullptr && owner->capture_incoming(this, p_packet)) {
        return;
    }

    packet_queue.push_back(p_packet);
}

PackedByteArray LocalMultiplayerPeer::take_packet() {
    if (packet_queue.is_empty()) {
        return PackedByteArray();
    }

    retained_packet = packet_queue[0];
    packet_queue.remove_at(0);
    return retained_packet.data;
}

void LocalMultiplayerPeer::purge_packets_from(int p_sender_id) {
    for (int index = packet_queue.size() - 1; index >= 0; --index) {
        if (packet_queue[index].peer == p_sender_id) {
            packet_queue.remove_at(index);
        }
    }

    if (retained_packet.peer == p_sender_id) {
        retained_packet = Packet();
    }
}

void LocalMultiplayerPeer::remote_closed(
    int p_remote_id,
    bool p_remote_was_server
) {
    purge_packets_from(p_remote_id);
    LocalLoopbackSession *owner = loopback_session();
    if (owner != nullptr) {
        owner->purge_packets_from(p_remote_id);
    }
    links.erase(p_remote_id);

    peers_to_emit_disconnected.push_back(p_remote_id);

    if (p_remote_was_server && !server_side) {
        status = CONNECTION_DISCONNECTED;
    }
}

void LocalMultiplayerPeer::finalize_close() {
    closing = false;
    closed = true;
    session = ObjectID();

    peers_to_emit_connected.clear();
    peers_to_emit_disconnected.clear();

    links.clear();
    packet_queue.clear();
    retained_packet = Packet();

    unique_id = 0;
    target_peer = 0;
    transfer_channel = 0;
    transfer_mode = TRANSFER_MODE_RELIABLE;
    server_side = false;

    NETW_TRACE(sys::TRANSPORT, "peer fully closed");
}

void LocalMultiplayerPeer::reset_state() {
    links.clear();
    packet_queue.clear();
    retained_packet = Packet();
    peers_to_emit_connected.clear();
    peers_to_emit_disconnected.clear();

    closing = false;
    closed = false;

    unique_id = 0;
    target_peer = 0;
    transfer_channel = 0;
    transfer_mode = TRANSFER_MODE_RELIABLE;
    server_side = false;
    status = CONNECTION_DISCONNECTED;
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(poll)() {
    NETW_ZONE_NC("LocalMultiplayerPeer poll", colors::TRANSPORT);
    if (!server_side && status == CONNECTION_CONNECTING) {
        status = CONNECTION_CONNECTED;
        peers_to_emit_connected.push_back(1);
    }

    while (has_link_listener() && !peers_to_emit_connected.is_empty()) {
        const int peer_id = peers_to_emit_connected[0];
        peers_to_emit_connected.remove_at(0);
        emit_signal("peer_connected", peer_id);
    }

    while (!peers_to_emit_disconnected.is_empty()) {
        const int peer_id = peers_to_emit_disconnected[0];
        peers_to_emit_disconnected.remove_at(0);
        emit_signal("peer_disconnected", peer_id);
    }

    if (closing) {
        finalize_close();
    }
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(close)() {
    if (closed || closing) {
        return;
    }

    closing = true;
    status = CONNECTION_DISCONNECTED;

    const int my_id = unique_id;
    const PackedInt32Array peers = linked_peer_ids();
    LocalLoopbackSession *owner = loopback_session();
    if (owner != nullptr) {
        owner->purge_packets_from(my_id);
    }

    if (!server_side) {
        purge_packets_from(1);
        peers_to_emit_disconnected.push_back(1);
    }

    for (const int peer_id : peers) {
        LocalMultiplayerPeer *other = peer_at(peer_id);
        if (other != nullptr) {
            other->remote_closed(my_id, server_side);
        }
    }

    finalize_close();
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(disconnect_peer)(
    int p_peer,
    bool p_force
) {
    LocalMultiplayerPeer *other = peer_at(p_peer);

    links.erase(p_peer);
    purge_packets_from(p_peer);
    LocalLoopbackSession *owner = loopback_session();
    if (owner != nullptr) {
        owner->purge_packets_from(p_peer);
    }
    if (!p_force) {
        peers_to_emit_disconnected.push_back(p_peer);
    }

    if (other != nullptr && !other->closed && !other->closing) {
        other->remote_closed(unique_id, server_side);
    }

    if (!server_side && p_peer == 1) {
        status = CONNECTION_DISCONNECTED;
    }
}

int LocalMultiplayerPeer::
    NETW_PEER_VIRTUAL(get_available_packet_count)() const {
    return packet_queue.size();
}

int LocalMultiplayerPeer::NETW_PEER_VIRTUAL(get_max_packet_size)() const {
    return MAX_PACKET_SIZE;
}

MultiplayerPeer::ConnectionStatus LocalMultiplayerPeer::
    NETW_PEER_VIRTUAL(get_connection_status)() const {
    return status;
}

int LocalMultiplayerPeer::NETW_PEER_VIRTUAL(get_unique_id)() const {
    return unique_id;
}

bool LocalMultiplayerPeer::NETW_PEER_VIRTUAL(is_server)() const {
    return server_side;
}

bool LocalMultiplayerPeer::
    NETW_PEER_VIRTUAL(is_server_relay_supported)() const {
    return true;
}

bool LocalMultiplayerPeer::
    NETW_PEER_VIRTUAL(is_refusing_new_connections)() const {
    return false;
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(set_refuse_new_connections)(
    bool p_enable
) {
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(set_target_peer)(int p_peer) {
    target_peer = p_peer;
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(set_transfer_channel)(
    int p_channel
) {
    transfer_channel = p_channel;
}

int LocalMultiplayerPeer::NETW_PEER_VIRTUAL(get_transfer_channel)() const {
    return transfer_channel;
}

void LocalMultiplayerPeer::NETW_PEER_VIRTUAL(set_transfer_mode)(
    TransferMode p_mode
) {
    transfer_mode = p_mode;
}

MultiplayerPeer::TransferMode LocalMultiplayerPeer::
    NETW_PEER_VIRTUAL(get_transfer_mode)() const {
    return transfer_mode;
}

int LocalMultiplayerPeer::NETW_PEER_VIRTUAL(get_packet_channel)() const {
    if (!packet_queue.is_empty()) {
        return packet_queue[0].channel;
    }
    return retained_packet.channel;
}

MultiplayerPeer::TransferMode LocalMultiplayerPeer::
    NETW_PEER_VIRTUAL(get_packet_mode)() const {
    if (!packet_queue.is_empty()) {
        return packet_queue[0].mode;
    }
    return retained_packet.mode;
}

int LocalMultiplayerPeer::NETW_PEER_VIRTUAL(get_packet_peer)() const {
    if (!packet_queue.is_empty()) {
        return packet_queue[0].peer;
    }
    return retained_packet.peer;
}

#if defined(NETW_MODULE)

Error LocalMultiplayerPeer::get_packet(
    const uint8_t **r_buffer,
    int &r_buffer_size
) {
    if (packet_queue.is_empty()) {
        return Error::ERR_UNAVAILABLE;
    }
    take_packet();
    *r_buffer = retained_packet.data.ptr();
    r_buffer_size = retained_packet.data.size();
    return Error::OK;
}

Error LocalMultiplayerPeer::put_packet(
    const uint8_t *p_buffer,
    int p_buffer_size
) {
    PackedByteArray bytes;
    bytes.resize(p_buffer_size);
    if (p_buffer_size > 0) {
        memcpy(bytes.ptrw(), p_buffer, p_buffer_size);
    }
    return send_packet(bytes);
}

#else

PackedByteArray LocalMultiplayerPeer::_get_packet_script() {
    return take_packet();
}

Error LocalMultiplayerPeer::_put_packet_script(
    const PackedByteArray &p_buffer
) {
    return send_packet(p_buffer);
}

#endif

void LocalMultiplayerPeer::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("create_server"),
        &LocalMultiplayerPeer::create_server
    );
    ClassDB::bind_method(
        D_METHOD("create_client", "client_id"),
        &LocalMultiplayerPeer::create_client
    );
    ClassDB::bind_method(
        D_METHOD("force_connect_peer", "peer_id", "peer"),
        &LocalMultiplayerPeer::force_connect_peer
    );
    ClassDB::bind_method(
        D_METHOD("is_linked_to", "peer_id"),
        &LocalMultiplayerPeer::is_linked_to
    );
    ClassDB::bind_method(
        D_METHOD("linked_peer_ids"),
        &LocalMultiplayerPeer::linked_peer_ids
    );
    ClassDB::bind_method(
        D_METHOD("set_loopback_session", "session"),
        &LocalMultiplayerPeer::set_loopback_session
    );
    ClassDB::bind_method(
        D_METHOD("get_loopback_session"),
        &LocalMultiplayerPeer::get_loopback_session
    );

    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "loopback_session",
            PROPERTY_HINT_RESOURCE_TYPE,
            "LocalLoopbackSession"
        ),
        "set_loopback_session",
        "get_loopback_session"
    );
}

} // namespace netw

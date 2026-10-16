#include "netw/link/shaped_peer.hpp"

#if defined(DEBUG_ENABLED)

#include <algorithm>
#include <cstring>

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/net_peers.hpp"
#include "godot/time.hpp"
#include "netw/api/loopback.hpp"

using namespace godot;

namespace netw::link {

namespace {

double now_ms() {
    return double(Time::get_singleton()->get_ticks_usec()) / 1000.0;
}

double frame_ms() {
    return 1000.0 / Engine::get_singleton()->get_physics_ticks_per_second();
}

} // namespace

bool ShapedPeer::PacketOrder::operator()(
    const Packet &p_left,
    const Packet &p_right
) const {
    if (p_left.due_ms == p_right.due_ms) {
        return p_left.sequence < p_right.sequence;
    }
    return p_left.due_ms < p_right.due_ms;
}

void ShapedPeer::_bind_methods() {
}

void ShapedPeer::initialize(
    const Ref<MultiplayerPeer> &p_base,
    const Ref<NetwLinkConditions> &p_conditions
) {
    base = p_base;
    conditions = p_conditions->duplicate();
    base->connect(
        StringName("peer_connected"),
        callable_mp(this, &ShapedPeer::on_peer_connected)
    );
    base->connect(
        StringName("peer_disconnected"),
        callable_mp(this, &ShapedPeer::on_peer_disconnected)
    );
}

ShapedPeer::~ShapedPeer() {
    if (base.is_valid()) {
        base->disconnect(
            StringName("peer_connected"),
            callable_mp(this, &ShapedPeer::on_peer_connected)
        );
        base->disconnect(
            StringName("peer_disconnected"),
            callable_mp(this, &ShapedPeer::on_peer_disconnected)
        );
    }
}

void ShapedPeer::on_peer_connected(int p_peer) {
    peers.insert(p_peer);
    emit_signal(StringName("peer_connected"), p_peer);
}

void ShapedPeer::purge(int p_peer) {
    for (Vector<Packet> *queue : {&outbound, &inbound, &available}) {
        Vector<Packet> kept;
        for (const Packet &packet : *queue) {
            if (packet.peer != p_peer) {
                kept.push_back(packet);
            }
        }
        *queue = kept;
    }
    outbound_draws.erase(p_peer);
    inbound_draws.erase(p_peer);
    forget_ordered_sender(outbound_ordered, p_peer);
    forget_ordered_sender(inbound_ordered, p_peer);
}

void ShapedPeer::on_peer_disconnected(int p_peer) {
    peers.erase(p_peer);
    purge(p_peer);
    emit_signal(StringName("peer_disconnected"), p_peer);
}

void ShapedPeer::enqueue(
    Vector<Packet> &p_queue,
    HashMap<int, Draws> &p_draws,
    Packet p_packet,
    double p_now_ms
) {
    if (!p_draws.has(p_packet.peer)) {
        p_draws.insert(p_packet.peer, Draws());
    }
    Draws &draws = p_draws[p_packet.peer];
    const Arrival arrival = schedule(
        draws,
        **conditions,
        {p_packet.peer,
         p_packet.channel,
         p_packet.mode == TRANSFER_MODE_RELIABLE},
        p_now_ms,
        frame_ms()
    );
    if (arrival.dropped) {
        return;
    }
    p_packet.sequence = sequence++;
    p_packet.due_ms = arrival.due_ms;
    p_queue.push_back(p_packet);
    if (arrival.duplicate_due_ms >= 0.0) {
        p_packet.due_ms = arrival.duplicate_due_ms;
        p_queue.push_back(p_packet);
    }
    prune_reliable_due(draws, p_now_ms);
}

Error ShapedPeer::send(const PackedByteArray &p_bytes) {
    if (closed || base.is_null()
        || base->get_connection_status() == CONNECTION_DISCONNECTED) {
        return ERR_UNAVAILABLE;
    }
    if (p_bytes.size() > NETW_PEER_VIRTUAL(get_max_packet_size)()) {
        return ERR_INVALID_PARAMETER;
    }
    Packet packet;
    packet.bytes = p_bytes;
    packet.channel = channel;
    packet.mode = mode;
    const double time = now_ms();
    if (target > 0) {
        packet.peer = target;
        enqueue(outbound, outbound_draws, packet, time);
    } else {
        for (int peer : peers) {
            if (peer != -target) {
                packet.peer = peer;
                enqueue(outbound, outbound_draws, packet, time);
            }
        }
    }
    release(time);
    return OK;
}

void ShapedPeer::release(double p_now_ms) {
    if (base->get_connection_status() != CONNECTION_CONNECTED) {
        return;
    }
    outbound.sort_custom<PacketOrder>();
    Vector<Packet> waiting;
    for (const Packet &packet : outbound) {
        const Draws *draws = outbound_draws.getptr(packet.peer);
        if (packet.due_ms > p_now_ms
            || (draws != nullptr && draws->stall_until_ms > p_now_ms)) {
            waiting.push_back(packet);
            continue;
        }
        if (packet.mode == TRANSFER_MODE_UNRELIABLE_ORDERED
            && !accept_ordered(
                outbound_ordered,
                packet.peer,
                packet.channel,
                packet.sequence
            )) {
            continue;
        }
        base->set_target_peer(packet.peer);
        base->set_transfer_channel(packet.channel);
        base->set_transfer_mode(packet.mode);
#if defined(NETW_MODULE)
        base->put_packet(packet.bytes.ptr(), packet.bytes.size());
#else
        base->put_packet(packet.bytes);
#endif
    }
    outbound = waiting;
    inbound.sort_custom<PacketOrder>();
    waiting.clear();
    for (const Packet &packet : inbound) {
        const Draws *draws = inbound_draws.getptr(packet.peer);
        if (packet.due_ms > p_now_ms
            || (draws != nullptr && draws->stall_until_ms > p_now_ms)) {
            waiting.push_back(packet);
            continue;
        }
        if (packet.mode == TRANSFER_MODE_UNRELIABLE_ORDERED
            && !accept_ordered(
                inbound_ordered,
                packet.peer,
                packet.channel,
                packet.sequence
            )) {
            continue;
        }
        available.push_back(packet);
    }
    inbound = waiting;
}

void ShapedPeer::NETW_PEER_VIRTUAL(poll)() {
    if (closed || base.is_null()) {
        return;
    }
    if (base->get_connection_status() != CONNECTION_DISCONNECTED) {
        release(now_ms());
        base->poll();
    }
    if (base->get_connection_status() == CONNECTION_DISCONNECTED) {
        outbound.clear();
        inbound.clear();
        available.clear();
        outbound_draws.clear();
        inbound_draws.clear();
        outbound_ordered.clear();
        inbound_ordered.clear();
        peers.clear();
        return;
    }
    const double time = now_ms();
    while (base->get_available_packet_count() > 0) {
        Packet packet;
        packet.peer = base->get_packet_peer();
        packet.channel = base->get_packet_channel();
        packet.mode = base->get_packet_mode();
#if defined(NETW_MODULE)
        const uint8_t *bytes = nullptr;
        int size = 0;
        if (base->get_packet(&bytes, size) != OK) {
            break;
        }
        packet.bytes.resize(size);
        if (size > 0) {
            memcpy(packet.bytes.ptrw(), bytes, size);
        }
#else
        packet.bytes = base->get_packet();
        if (base->get_packet_error() != OK) {
            break;
        }
#endif
        enqueue(inbound, inbound_draws, packet, time);
    }
    release(time);
}

PackedByteArray ShapedPeer::take() {
    if (closed || available.is_empty()) {
        return PackedByteArray();
    }
    retained = available[0];
    available.remove_at(0);
    return retained.bytes;
}

void ShapedPeer::NETW_PEER_VIRTUAL(close)() {
    closed = true;
    outbound.clear();
    inbound.clear();
    available.clear();
    retained = Packet();
    outbound_draws.clear();
    inbound_draws.clear();
    outbound_ordered.clear();
    inbound_ordered.clear();
    peers.clear();
    if (base.is_valid()) {
        base->close();
    }
}

void ShapedPeer::NETW_PEER_VIRTUAL(disconnect_peer)(int p_peer, bool p_force) {
    purge(p_peer);
    peers.erase(p_peer);
    base->disconnect_peer(p_peer, p_force);
}

void ShapedPeer::NETW_PEER_VIRTUAL(set_target_peer)(int p_peer) {
    target = p_peer;
}

void ShapedPeer::NETW_PEER_VIRTUAL(set_transfer_channel)(int p_channel) {
    channel = p_channel;
}

int ShapedPeer::NETW_PEER_VIRTUAL(get_transfer_channel)() const {
    return channel;
}

void ShapedPeer::NETW_PEER_VIRTUAL(set_transfer_mode)(TransferMode p_mode) {
    mode = p_mode;
}

MultiplayerPeer::TransferMode ShapedPeer::
    NETW_PEER_VIRTUAL(get_transfer_mode)() const {
    return mode;
}

int ShapedPeer::NETW_PEER_VIRTUAL(get_packet_peer)() const {
    return available.is_empty() ? retained.peer : available[0].peer;
}

int ShapedPeer::NETW_PEER_VIRTUAL(get_packet_channel)() const {
    return available.is_empty() ? retained.channel : available[0].channel;
}

MultiplayerPeer::TransferMode ShapedPeer::
    NETW_PEER_VIRTUAL(get_packet_mode)() const {
    return available.is_empty() ? retained.mode : available[0].mode;
}

int ShapedPeer::NETW_PEER_VIRTUAL(get_available_packet_count)() const {
    return available.size();
}

int ShapedPeer::NETW_PEER_VIRTUAL(get_max_packet_size)() const {
    const LocalMultiplayerPeer *local
        = Object::cast_to<LocalMultiplayerPeer>(*base);
    return local != nullptr ? local->NETW_PEER_VIRTUAL(get_max_packet_size)()
                            : gd::peer_max_packet_size(base);
}

void ShapedPeer::NETW_PEER_VIRTUAL(set_refuse_new_connections)(bool p_enable) {
    base->set_refuse_new_connections(p_enable);
}

bool ShapedPeer::NETW_PEER_VIRTUAL(is_refusing_new_connections)() const {
    return base->is_refusing_new_connections();
}

bool ShapedPeer::NETW_PEER_VIRTUAL(is_server_relay_supported)() const {
    return base->is_server_relay_supported();
}

bool ShapedPeer::NETW_PEER_VIRTUAL(is_server)() const {
    const LocalMultiplayerPeer *local
        = Object::cast_to<LocalMultiplayerPeer>(*base);
    return local != nullptr ? local->NETW_PEER_VIRTUAL(is_server)()
                            : gd::peer_is_server(base);
}

int ShapedPeer::NETW_PEER_VIRTUAL(get_unique_id)() const {
    return base->get_unique_id();
}

MultiplayerPeer::ConnectionStatus ShapedPeer::
    NETW_PEER_VIRTUAL(get_connection_status)() const {
    return closed ? CONNECTION_DISCONNECTED : base->get_connection_status();
}

#if defined(NETW_MODULE)
Error ShapedPeer::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
    if (available.is_empty()) {
        return ERR_UNAVAILABLE;
    }
    take();
    *r_buffer = retained.bytes.ptr();
    r_buffer_size = retained.bytes.size();
    return OK;
}

Error ShapedPeer::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
    PackedByteArray bytes;
    bytes.resize(p_buffer_size);
    if (p_buffer_size > 0) {
        memcpy(bytes.ptrw(), p_buffer, p_buffer_size);
    }
    return send(bytes);
}
#else
PackedByteArray ShapedPeer::_get_packet_script() {
    return take();
}

Error ShapedPeer::_put_packet_script(const PackedByteArray &p_buffer) {
    return send(p_buffer);
}
#endif

} // namespace netw::link

#endif

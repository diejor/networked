#include "netw/transport/loopback.hpp"

#include <algorithm>
#include <limits>

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/math.hpp"
#include "godot/project_settings.hpp"
#include "godot/utility.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw {

namespace {

double physics_period_ms() {
    const Variant setting = ProjectSettings::get_singleton()->get_setting(
        "physics/common/physics_ticks_per_second",
        60
    );
    return 1000.0 / double(std::max(1, int(setting)));
}

uint64_t physics_frames() {
    return Engine::get_singleton()->get_physics_frames();
}

constexpr double NEVER_DUE = std::numeric_limits<double>::infinity();

String stream_key(int p_sender_id, const char *p_stream) {
    return String::num_int64(p_sender_id) + String(":") + String(p_stream);
}

String channel_key(int p_sender_id, int p_channel) {
    return String::num_int64(p_sender_id) + String(":")
        + String::num_int64(p_channel);
}

String sender_prefix(int p_sender_id) {
    return String::num_int64(p_sender_id) + String(":");
}

uint64_t peer_key(const LocalMultiplayerPeer *p_peer) {
    return uint64_t(gd::instance_id(p_peer));
}

} // namespace

Ref<LocalLoopbackSession> LocalLoopbackSession::shared;

bool LocalLoopbackSession::LinkState::has_conditions() const {
    return !conditions_by_sender.is_empty();
}

bool LocalLoopbackSession::LinkState::is_idle() const {
    return in_flight.is_empty() && !has_conditions() && !held;
}

Ref<LocalLoopbackSession> LocalLoopbackSession::get_shared_session() {
    if (shared.is_null()) {
        shared.instantiate();
    }
    return shared;
}

void LocalLoopbackSession::set_shared_session(
    const Ref<LocalLoopbackSession> &p_session
) {
    shared = p_session;
}

bool LocalLoopbackSession::has_shared_session() {
    return shared.is_valid();
}

bool LocalLoopbackSession::has_live_server() const {
    return server_peer.is_valid()
        && server_peer->get_connection_status()
        != MultiplayerPeer::CONNECTION_DISCONNECTED;
}

void LocalLoopbackSession::init_server_side() {
    if (has_live_server()) {
        return;
    }

    if (server_peer.is_valid()) {
        server_peer->set_loopback_session(nullptr);
        server_peer->close();
    }

    server_peer.instantiate();
    server_peer->set_loopback_session(this);
    if (server_peer->create_server() != Error::OK) {
        NETW_WARN(sys::TRANSPORT, "loopback server could not be created");
    }
}

Ref<LocalMultiplayerPeer> LocalLoopbackSession::create_client_peer() {
    init_server_side();

    Ref<LocalMultiplayerPeer> client;
    client.instantiate();
    client->set_loopback_session(this);
    const int client_id = int(gd::randi_range(2, 2147483647));
    if (client->create_client(client_id) != Error::OK) {
        NETW_WARN(sys::TRANSPORT, "loopback client could not be created");
        return client;
    }

    server_peer->force_connect_peer(client_id, client.ptr());
    client->force_connect_peer(1, server_peer.ptr());
    client_peers.push_back(client);
    NETW_INFO(
        sys::TRANSPORT,
        "loopback handshake complete for client=%d",
        client_id
    );
    return client;
}

Ref<LocalMultiplayerPeer> LocalLoopbackSession::get_server_peer() {
    init_server_side();
    return server_peer;
}

Ref<LocalMultiplayerPeer> LocalLoopbackSession::get_client_peer() {
    return create_client_peer();
}

Array LocalLoopbackSession::get_client_peers() const {
    Array peers;
    for (const Ref<LocalMultiplayerPeer> &client : client_peers) {
        peers.push_back(client);
    }
    return peers;
}

void LocalLoopbackSession::set_server_app_id(const StringName &p_app_id) {
    server_app_id = p_app_id;
}

StringName LocalLoopbackSession::get_server_app_id() const {
    return server_app_id;
}

void LocalLoopbackSession::poll() {
    clock_ms += physics_period_ms();
    poll_peers();
}

void LocalLoopbackSession::poll_frame_scoped() {
    const int64_t frame = int64_t(physics_frames());
    if (frame != last_scoped_poll_frame) {
        if (last_scoped_poll_frame < 0) {
            clock_ms += physics_period_ms();
        } else {
            clock_ms
                += double(frame - last_scoped_poll_frame) * physics_period_ms();
        }
        last_scoped_poll_frame = frame;
    }
    poll_peers();
}

void LocalLoopbackSession::advance_time(double p_ms) {
    clock_ms += std::max(0.0, p_ms);
    poll_peers();
}

void LocalLoopbackSession::poll_peers() {
    NETW_ZONE_NC("LocalLoopbackSession poll peers", colors::TRANSPORT);
    if (server_peer.is_valid()) {
        poll_or_hold(server_peer.ptr());
    }
    for (const Ref<LocalMultiplayerPeer> &client : client_peers) {
        if (client.is_valid() && !client->closed) {
            poll_or_hold(client.ptr());
        }
    }
}

void LocalLoopbackSession::poll_or_hold(LocalMultiplayerPeer *p_peer) {
    NETW_ASSERT(
        p_peer != nullptr,
        sys::TRANSPORT,
        "A loopback poll requires a peer."
    );
    LinkState *state = link_of(p_peer);
    if (state != nullptr && state->held) {
        capture_held_packets(p_peer);
        return;
    }

    p_peer->poll();
    if (link_of(p_peer) != nullptr) {
        release_due(p_peer);
    }
}

void LocalLoopbackSession::hold_inbound_packets(LocalMultiplayerPeer *p_peer) {
    if (p_peer == nullptr) {
        return;
    }
    ensure_link(p_peer).held = true;
    capture_held_packets(p_peer);
}

void LocalLoopbackSession::release_inbound_packets(
    LocalMultiplayerPeer *p_peer
) {
    if (p_peer == nullptr) {
        return;
    }
    LinkState *state = link_of(p_peer);
    if (state == nullptr || !state->held) {
        return;
    }
    capture_held_packets(p_peer);
    state->held = false;
    release_due(p_peer, true);
}

void LocalLoopbackSession::set_link_conditions(
    LocalMultiplayerPeer *p_peer,
    const Ref<LocalLinkConditions> &p_conditions,
    int p_sender_id
) {
    if (p_peer == nullptr) {
        return;
    }
    LinkState &state = ensure_link(p_peer);
    if (p_conditions.is_null()) {
        state.conditions_by_sender.erase(p_sender_id);
        clear_sender_streams(state, p_sender_id);
        return;
    }
    state.conditions_by_sender.insert(p_sender_id, p_conditions->clone());
    clear_sender_streams(state, p_sender_id);
    capture_queued_packets(p_peer);
}

void LocalLoopbackSession::clear_link_conditions(
    LocalMultiplayerPeer *p_peer,
    int p_sender_id
) {
    if (p_peer == nullptr) {
        return;
    }
    LinkState *state = link_of(p_peer);
    if (state == nullptr) {
        return;
    }
    state->conditions_by_sender.erase(p_sender_id);
    clear_sender_streams(*state, p_sender_id);
    if (!state->has_conditions() && !state->held) {
        release_due(p_peer, true);
        drop_link_if_idle(p_peer);
    }
}

void LocalLoopbackSession::clear_all_link_conditions() {
    Vector<uint64_t> peer_ids;
    for (const KeyValue<uint64_t, LinkState> &link : links) {
        peer_ids.push_back(link.key);
    }

    for (const uint64_t peer_id : peer_ids) {
        LinkState *state = links.getptr(peer_id);
        if (state == nullptr) {
            continue;
        }
        state->conditions_by_sender.clear();
        state->rng_by_stream.clear();
        state->reliable_due_by_channel.clear();
        if (state->held) {
            continue;
        }
        LocalMultiplayerPeer *peer = Object::cast_to<LocalMultiplayerPeer>(
            gd::instance_from_id(ObjectID(peer_id))
        );
        if (peer != nullptr) {
            release_due(peer, true);
            drop_link_if_idle(peer);
        }
    }
}

void LocalLoopbackSession::purge_packets_from(int p_sender_id) {
    Vector<uint64_t> peer_ids;
    for (const KeyValue<uint64_t, LinkState> &link : links) {
        peer_ids.push_back(link.key);
    }

    for (const uint64_t peer_id : peer_ids) {
        LinkState *state = links.getptr(peer_id);
        if (state == nullptr) {
            continue;
        }
        for (int index = state->in_flight.size() - 1; index >= 0; --index) {
            if (state->in_flight[index].packet.peer == p_sender_id) {
                state->in_flight.remove_at(index);
            }
        }
        clear_sender_streams(*state, p_sender_id);
        if (state->is_idle()) {
            links.erase(peer_id);
        }
    }
}

Ref<LocalLinkConditions> LocalLoopbackSession::get_link_conditions(
    const LocalMultiplayerPeer *p_peer,
    int p_sender_id
) const {
    if (p_peer == nullptr) {
        return Ref<LocalLinkConditions>();
    }
    const LinkState *state = links.getptr(peer_key(p_peer));
    if (state == nullptr) {
        return Ref<LocalLinkConditions>();
    }
    const Ref<LocalLinkConditions> *found
        = state->conditions_by_sender.getptr(p_sender_id);
    return found == nullptr ? Ref<LocalLinkConditions>() : *found;
}

int LocalLoopbackSession::in_flight_count(
    const LocalMultiplayerPeer *p_peer
) const {
    if (p_peer == nullptr) {
        return 0;
    }
    const LinkState *state = links.getptr(peer_key(p_peer));
    return state == nullptr ? 0 : state->in_flight.size();
}

bool LocalLoopbackSession::is_holding_inbound(
    const LocalMultiplayerPeer *p_peer
) const {
    if (p_peer == nullptr) {
        return false;
    }
    const LinkState *state = links.getptr(peer_key(p_peer));
    return state != nullptr && state->held;
}

void LocalLoopbackSession::reset() {
    if (server_peer.is_valid()) {
        server_peer->set_loopback_session(nullptr);
        server_peer->close();
    }
    for (const Ref<LocalMultiplayerPeer> &client : client_peers) {
        if (client.is_valid()) {
            client->set_loopback_session(nullptr);
            client->close();
        }
    }
    server_peer.unref();
    server_app_id = StringName();
    client_peers.clear();
    clock_ms = 0.0;
    last_scoped_poll_frame = -1;
    links.clear();
}

LocalLoopbackSession::LinkState &LocalLoopbackSession::ensure_link(
    const LocalMultiplayerPeer *p_peer
) {
    const uint64_t peer_id = peer_key(p_peer);
    if (!links.has(peer_id)) {
        links.insert(peer_id, LinkState());
    }
    return *links.getptr(peer_id);
}

LocalLoopbackSession::LinkState *LocalLoopbackSession::link_of(
    const LocalMultiplayerPeer *p_peer
) {
    return links.getptr(peer_key(p_peer));
}

void LocalLoopbackSession::drop_link_if_idle(
    const LocalMultiplayerPeer *p_peer
) {
    const uint64_t peer_id = peer_key(p_peer);
    const LinkState *state = links.getptr(peer_id);
    if (state != nullptr && state->is_idle()) {
        links.erase(peer_id);
    }
}

bool LocalLoopbackSession::capture_incoming(
    const LocalMultiplayerPeer *p_peer,
    const LocalMultiplayerPeer::Packet &p_packet
) {
    NETW_ZONE_NC("LocalLoopbackSession capture", colors::TRANSPORT);
    LinkState *state = link_of(p_peer);
    if (state != nullptr && state->held) {
        enqueue(*state, p_packet, NEVER_DUE);
        return true;
    }

    if (state == nullptr || !state->has_conditions()) {
        return false;
    }
    const Ref<LocalLinkConditions> conditions
        = conditions_for(*state, p_packet);
    if (conditions.is_null()) {
        return false;
    }

    if (p_packet.mode == MultiplayerPeer::TRANSFER_MODE_RELIABLE) {
        capture_reliable(*state, p_packet, conditions);
    } else {
        capture_unreliable(*state, p_packet, conditions);
    }
    return true;
}

void LocalLoopbackSession::capture_held_packets(LocalMultiplayerPeer *p_peer) {
    if (p_peer->packet_queue.is_empty()) {
        return;
    }
    LinkState &state = ensure_link(p_peer);
    for (const LocalMultiplayerPeer::Packet &packet : p_peer->packet_queue) {
        enqueue(state, packet, NEVER_DUE);
    }
    p_peer->packet_queue.clear();
}

void LocalLoopbackSession::capture_queued_packets(
    LocalMultiplayerPeer *p_peer
) {
    if (p_peer->packet_queue.is_empty()) {
        return;
    }

    LinkState &state = ensure_link(p_peer);
    Vector<LocalMultiplayerPeer::Packet> remaining;
    for (const LocalMultiplayerPeer::Packet &packet : p_peer->packet_queue) {
        const Ref<LocalLinkConditions> conditions
            = conditions_for(state, packet);
        if (conditions.is_null()) {
            remaining.push_back(packet);
        } else if (packet.mode == MultiplayerPeer::TRANSFER_MODE_RELIABLE) {
            capture_reliable(state, packet, conditions);
        } else {
            capture_unreliable(state, packet, conditions);
        }
    }
    p_peer->packet_queue = remaining;
}

bool LocalLoopbackSession::InFlightOrder::operator()(
    const InFlight &a,
    const InFlight &b
) const {
    if (Math::is_equal_approx(a.due_time_ms, b.due_time_ms)) {
        return a.seq < b.seq;
    }
    return a.due_time_ms < b.due_time_ms;
}

void LocalLoopbackSession::release_due(
    LocalMultiplayerPeer *p_peer,
    bool p_flush_all
) {
    NETW_ZONE_NC("LocalLoopbackSession release", colors::TRANSPORT);
    LinkState *state = link_of(p_peer);
    if (state == nullptr || p_peer->closed || p_peer->closing) {
        return;
    }

    state->in_flight.sort_custom<InFlightOrder>();

    Vector<InFlight> due;
    Vector<InFlight> remaining;
    for (const InFlight &entry : state->in_flight) {
        if (p_flush_all || entry.due_time_ms <= clock_ms + RELEASE_EPSILON_MS) {
            due.push_back(entry);
        } else {
            remaining.push_back(entry);
        }
    }
    state->in_flight = remaining;

    if (due.is_empty()) {
        return;
    }

    for (const InFlight &entry : due) {
        if (entry.packet.peer == 0 || p_peer->is_linked_to(entry.packet.peer)) {
            p_peer->packet_queue.push_back(entry.packet);
        }
    }
    prune_reliable_due(*state);
    drop_link_if_idle(p_peer);
}

void LocalLoopbackSession::enqueue(
    LinkState &p_state,
    const LocalMultiplayerPeer::Packet &p_packet,
    double p_due_time_ms
) {
    InFlight entry;
    entry.packet = p_packet;
    entry.due_time_ms = p_due_time_ms;
    entry.seq = p_state.seq;
    p_state.in_flight.push_back(entry);
    p_state.seq += 1;
}

void LocalLoopbackSession::capture_reliable(
    LinkState &p_state,
    const LocalMultiplayerPeer::Packet &p_packet,
    const Ref<LocalLinkConditions> &p_conditions
) {
    const int sender_id = p_packet.peer;
    double due_ms = clock_ms + p_conditions->effective_latency_ms();
    if (roll(
            p_state,
            sender_id,
            "loss",
            p_conditions->get_seed(),
            p_conditions->get_packet_loss()
        )) {
        due_ms += p_conditions->effective_retransmit_ms();
    }
    const String key = channel_key(sender_id, p_packet.channel);
    const double *last_due = p_state.reliable_due_by_channel.getptr(key);
    if (last_due != nullptr) {
        due_ms = std::max(due_ms, *last_due);
    }
    p_state.reliable_due_by_channel.insert(key, due_ms);
    enqueue(p_state, p_packet, due_ms);
}

void LocalLoopbackSession::capture_unreliable(
    LinkState &p_state,
    const LocalMultiplayerPeer::Packet &p_packet,
    const Ref<LocalLinkConditions> &p_conditions
) {
    const int sender_id = p_packet.peer;
    const int64_t seed = p_conditions->get_seed();
    if (roll(
            p_state,
            sender_id,
            "loss",
            seed,
            p_conditions->get_packet_loss()
        )) {
        return;
    }

    double due_ms = clock_ms + p_conditions->effective_latency_ms();
    due_ms += draw_jitter(p_state, sender_id, p_conditions);
    if (roll(
            p_state,
            sender_id,
            "reorder",
            seed,
            p_conditions->get_reorder()
        )) {
        due_ms += physics_period_ms();
    }
    if (p_state.throttle_until_ms <= clock_ms
        && roll(
            p_state,
            sender_id,
            "throttle",
            seed,
            p_conditions->get_throttle()
        )) {
        p_state.throttle_until_ms = clock_ms
            + std::max(physics_period_ms(), p_conditions->get_throttle_ms());
    }
    if (p_state.throttle_until_ms > clock_ms) {
        due_ms = std::max(due_ms, p_state.throttle_until_ms);
    }

    enqueue(p_state, p_packet, due_ms);
    if (roll(
            p_state,
            sender_id,
            "duplicate",
            seed,
            p_conditions->get_duplicate()
        )) {
        enqueue(p_state, p_packet, due_ms + physics_period_ms());
    }
}

double LocalLoopbackSession::draw_jitter(
    LinkState &p_state,
    int p_sender_id,
    const Ref<LocalLinkConditions> &p_conditions
) {
    if (p_conditions->get_jitter_ms() <= 0.0) {
        return 0.0;
    }
    const Ref<RandomNumberGenerator> rng
        = rng_for(p_state, p_sender_id, "jitter", p_conditions->get_seed());
    return rng->randf_range(0.0, p_conditions->get_jitter_ms());
}

bool LocalLoopbackSession::roll(
    LinkState &p_state,
    int p_sender_id,
    const char *p_stream,
    int64_t p_seed,
    double p_probability
) {
    if (p_probability <= 0.0) {
        return false;
    }
    if (p_probability >= 1.0) {
        return true;
    }
    return rng_for(p_state, p_sender_id, p_stream, p_seed)->randf()
        < p_probability;
}

Ref<RandomNumberGenerator> LocalLoopbackSession::rng_for(
    LinkState &p_state,
    int p_sender_id,
    const char *p_stream,
    int64_t p_seed
) {
    const String key = stream_key(p_sender_id, p_stream);
    const Ref<RandomNumberGenerator> *found = p_state.rng_by_stream.getptr(key);
    if (found != nullptr) {
        return *found;
    }
    Ref<RandomNumberGenerator> rng;
    rng.instantiate();
    rng->set_seed(
        uint64_t(p_seed ^ int64_t(uint32_t(String(p_stream).hash())))
    );
    p_state.rng_by_stream.insert(key, rng);
    return rng;
}

void LocalLoopbackSession::clear_sender_streams(
    LinkState &p_state,
    int p_sender_id
) {
    const String prefix = sender_prefix(p_sender_id);

    Vector<String> stream_keys;
    for (const KeyValue<String, Ref<RandomNumberGenerator>> &stream :
         p_state.rng_by_stream) {
        if (stream.key.begins_with(prefix)) {
            stream_keys.push_back(stream.key);
        }
    }
    for (const String &key : stream_keys) {
        p_state.rng_by_stream.erase(key);
    }

    Vector<String> channel_keys;
    for (const KeyValue<String, double> &channel :
         p_state.reliable_due_by_channel) {
        if (channel.key.begins_with(prefix)) {
            channel_keys.push_back(channel.key);
        }
    }
    for (const String &key : channel_keys) {
        p_state.reliable_due_by_channel.erase(key);
    }
}

void LocalLoopbackSession::prune_reliable_due(LinkState &p_state) {
    Vector<String> expired;
    for (const KeyValue<String, double> &channel :
         p_state.reliable_due_by_channel) {
        if (channel.value <= clock_ms) {
            expired.push_back(channel.key);
        }
    }
    for (const String &key : expired) {
        p_state.reliable_due_by_channel.erase(key);
    }
}

Ref<LocalLinkConditions> LocalLoopbackSession::conditions_for(
    const LinkState &p_state,
    const LocalMultiplayerPeer::Packet &p_packet
) const {
    const Ref<LocalLinkConditions> *by_sender
        = p_state.conditions_by_sender.getptr(p_packet.peer);
    if (by_sender != nullptr) {
        return *by_sender;
    }
    const Ref<LocalLinkConditions> *for_everyone
        = p_state.conditions_by_sender.getptr(0);
    return for_everyone == nullptr ? Ref<LocalLinkConditions>() : *for_everyone;
}

void LocalLoopbackSession::_bind_methods() {
    ClassDB::bind_static_method(
        "LocalLoopbackSession",
        D_METHOD("get_shared_session"),
        &LocalLoopbackSession::get_shared_session
    );
    ClassDB::bind_static_method(
        "LocalLoopbackSession",
        D_METHOD("set_shared_session", "session"),
        &LocalLoopbackSession::set_shared_session
    );
    ClassDB::bind_static_method(
        "LocalLoopbackSession",
        D_METHOD("has_shared_session"),
        &LocalLoopbackSession::has_shared_session
    );

    ClassDB::bind_method(
        D_METHOD("has_live_server"),
        &LocalLoopbackSession::has_live_server
    );
    ClassDB::bind_method(
        D_METHOD("init_server_side"),
        &LocalLoopbackSession::init_server_side
    );
    ClassDB::bind_method(
        D_METHOD("create_client_peer"),
        &LocalLoopbackSession::create_client_peer
    );
    ClassDB::bind_method(
        D_METHOD("get_server_peer"),
        &LocalLoopbackSession::get_server_peer
    );
    ClassDB::bind_method(
        D_METHOD("get_client_peer"),
        &LocalLoopbackSession::get_client_peer
    );
    ClassDB::bind_method(
        D_METHOD("get_client_peers"),
        &LocalLoopbackSession::get_client_peers
    );
    ClassDB::bind_method(
        D_METHOD("set_server_app_id", "app_id"),
        &LocalLoopbackSession::set_server_app_id
    );
    ClassDB::bind_method(
        D_METHOD("get_server_app_id"),
        &LocalLoopbackSession::get_server_app_id
    );

    ClassDB::bind_method(D_METHOD("poll"), &LocalLoopbackSession::poll);
    ClassDB::bind_method(
        D_METHOD("poll_frame_scoped"),
        &LocalLoopbackSession::poll_frame_scoped
    );
    ClassDB::bind_method(
        D_METHOD("advance_time", "ms"),
        &LocalLoopbackSession::advance_time
    );

    ClassDB::bind_method(
        D_METHOD("hold_inbound_packets", "peer"),
        &LocalLoopbackSession::hold_inbound_packets
    );
    ClassDB::bind_method(
        D_METHOD("release_inbound_packets", "peer"),
        &LocalLoopbackSession::release_inbound_packets
    );

    ClassDB::bind_method(
        D_METHOD("set_link_conditions", "peer", "conditions", "sender_id"),
        &LocalLoopbackSession::set_link_conditions,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("clear_link_conditions", "peer", "sender_id"),
        &LocalLoopbackSession::clear_link_conditions,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("clear_all_link_conditions"),
        &LocalLoopbackSession::clear_all_link_conditions
    );
    ClassDB::bind_method(
        D_METHOD("get_link_conditions", "peer", "sender_id"),
        &LocalLoopbackSession::get_link_conditions,
        DEFVAL(0)
    );
    ClassDB::bind_method(
        D_METHOD("purge_packets_from", "sender_id"),
        &LocalLoopbackSession::purge_packets_from
    );
    ClassDB::bind_method(
        D_METHOD("in_flight_count", "peer"),
        &LocalLoopbackSession::in_flight_count
    );
    ClassDB::bind_method(
        D_METHOD("is_holding_inbound", "peer"),
        &LocalLoopbackSession::is_holding_inbound
    );
    ClassDB::bind_method(D_METHOD("reset"), &LocalLoopbackSession::reset);

    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "server_app_id"),
        "set_server_app_id",
        "get_server_app_id"
    );
}

} // namespace netw

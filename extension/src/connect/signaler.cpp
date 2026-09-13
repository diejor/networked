#include "netw/connect/signaler.hpp"

#include "godot/json.hpp"
#include "netw/connect/tracker_client.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

const double TrackerSignaler::SIGNALING_CLOSE_DELAY = 3.0;

void Signaler::publish(const SignalerEvent &p_event) {
    inbound.push_back(p_event);
}

void Signaler::drain(LocalVector<SignalerEvent> &r_out) {
    for (int64_t at = 0; at < int64_t(inbound.size()); at++) {
        r_out.push_back(inbound[at]);
    }
    inbound.clear();
}

String candidate_digest(const Dictionary &p_payload) {
    const Variant found = p_payload.get("candidates", Array());
    if (found.get_type() != Variant::ARRAY) {
        return String("").sha1_text();
    }
    return JSON::stringify(found, "", true, false).sha1_text();
}

String TrackerSignaler::derived_info_hash(
    const String &p_namespace,
    const String &p_room
) {
    if (!p_namespace.is_empty()) {
        return (p_namespace + String(":") + p_room).sha1_text().substr(0, 20);
    }
    if (p_room.length() != 20) {
        return p_room.sha1_text().substr(0, 20);
    }
    return p_room;
}

String TrackerSignaler::host_peer_id(const String &p_info_hash) {
    return p_info_hash.substr(0, 10) + String("1").lpad(10, "0");
}

int64_t TrackerSignaler::peer_of_address(const String &p_address) {
    if (p_address.length() != 20) {
        return 0;
    }
    return p_address.substr(10, 10).to_int();
}

TrackerSignaler::TrackerSignaler(
    const PackedStringArray &p_trackers,
    const String &p_namespace,
    const String &p_characters
)
    : trackers(p_trackers), signaling_namespace(p_namespace),
      room_characters(p_characters) {
    dice.instantiate();
    dice->randomize();
}

TrackerSignaler::~TrackerSignaler() {
    release();
}

String TrackerSignaler::random_hex(int64_t p_length) {
    const String glyphs = "0123456789abcdef";
    String out;
    for (int64_t at = 0; at < p_length; at++) {
        out += glyphs[dice->randi_range(0, glyphs.length() - 1)];
    }
    return out;
}

String TrackerSignaler::short_code() {
    String glyphs = room_characters;
    if (glyphs.is_empty()) {
        glyphs = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    }
    String out;
    for (int64_t at = 0; at < 5; at++) {
        out += glyphs[dice->randi_range(0, glyphs.length() - 1)];
    }
    return out;
}

String TrackerSignaler::peer_id_for(int64_t p_peer) {
    const String prefix = p_peer == 1 ? hash.substr(0, 10) : random_hex(10);
    return prefix + String::num_int64(p_peer).lpad(10, "0");
}

Error TrackerSignaler::open(const String &p_room, int64_t p_local_peer) {
    local_peer = p_local_peer;
    is_server = p_local_peer == 1;
    if (is_server) {
        room = signaling_namespace.is_empty() ? random_hex(20) : short_code();
    } else {
        room = p_room;
    }
    hash = derived_info_hash(signaling_namespace, room);
    local_peer_id = peer_id_for(p_local_peer);
    if (!is_server) {
        server_peer_id = host_peer_id(hash);
    }
    NETW_DEBUG(
        sys::TRANSPORT,
        "the tracker signaler opened room %s as id %d",
        room,
        local_peer
    );

    Error err = OK;
    tracker = TrackerBook::shared().acquire(trackers, err);
    shared = tracker != nullptr;
    if (err != OK || tracker == nullptr) {
        SignalerEvent lost;
        lost.kind = SignalerEvent::LOST;
        publish(lost);
        return err != OK ? err : ERR_CANT_CONNECT;
    }
    subscription = tracker->subscribe();
    if (tracker->has_open()) {
        SignalerEvent up;
        up.kind = SignalerEvent::READY;
        publish(up);
        announce_open_sockets();
        flush_pending();
    }
    return OK;
}

Dictionary TrackerSignaler::presence() const {
    Dictionary out;
    out["action"] = "announce";
    out["info_hash"] = hash;
    out["peer_id"] = local_peer_id;
    out["numwant"] = 1;
    return out;
}

void TrackerSignaler::announce_open_sockets() {
    if (tracker == nullptr) {
        return;
    }
    const PackedInt64Array open = tracker->open_sockets();
    for (int64_t at = 0; at < open.size(); at++) {
        tracker->send(open[at], presence());
    }
}

void TrackerSignaler::flush_pending() {
    if (pending.size() == 0 || tracker == nullptr || !tracker->has_open()) {
        return;
    }
    for (int64_t at = 0; at < int64_t(pending.size()); at++) {
        tracker->broadcast(pending[at].message);
    }
    pending.clear();
}

void TrackerSignaler::poll(
    double p_delta,
    int64_t p_now_usec,
    int64_t p_frame
) {
    if (tracker == nullptr) {
        return;
    }
    tracker->poll(p_now_usec, p_frame);

    LocalVector<TrackerEvent> events;
    tracker->drain(subscription, events);
    for (int64_t at = 0; at < int64_t(events.size()); at++) {
        switch (events[at].kind) {
            case TrackerEvent::OPENED: {
                SignalerEvent up;
                up.kind = SignalerEvent::READY;
                publish(up);
            } break;
            case TrackerEvent::SOCKET_OPENED: {
                tracker->send(events[at].socket, presence());
                flush_pending();
            } break;
            case TrackerEvent::CLOSED: {
                NETW_INFO(
                    sys::TRANSPORT,
                    "the tracker signaler lost every tracker"
                );
                SignalerEvent down;
                down.kind = SignalerEvent::LOST;
                publish(down);
            } break;
            case TrackerEvent::UNREACHABLE: {
                NETW_INFO(
                    sys::TRANSPORT,
                    "the tracker signaler reached no tracker"
                );
                SignalerEvent none;
                none.kind = SignalerEvent::UNREACHABLE;
                publish(none);
            } break;
            case TrackerEvent::MESSAGE: {
                read_packet(events[at].data);
            } break;
        }
    }

    announce_timer += p_delta;
    drive_presence(p_delta);

    if (close_delay >= 0.0) {
        close_delay -= p_delta;
        if (close_delay <= 0.0) {
            close_delay = -1.0;
            NETW_TRACE(
                sys::TRANSPORT,
                "the tracker signaler is winding down behind a live link"
            );
            send_stop();
            release();
        }
    }
}

void TrackerSignaler::drive_presence(double p_delta) {
    if (native_up || tracker == nullptr) {
        return;
    }
    if (announce_timer < reannounce_period()) {
        return;
    }
    announce_timer = 0.0;
    if (tracker->has_open()) {
        tracker->broadcast(presence());
    }
}

double TrackerSignaler::reannounce_period() const {
    return min_announce_period > 2.0 ? min_announce_period : 2.0;
}

void TrackerSignaler::send(
    int64_t p_to_peer,
    const String &p_to_address,
    const String &p_kind,
    const Dictionary &p_payload
) {
    (void)p_to_peer;
    if (p_kind == "offer") {
        send_directed(server_peer_id, "offer", p_payload);
    } else if (p_kind == "answer") {
        send_directed(p_to_address, "answer", p_payload);
    }
}

void TrackerSignaler::send_directed(
    const String &p_to,
    const String &p_type,
    const Dictionary &p_payload
) {
    if (p_to.is_empty()) {
        return;
    }
    Variant found = p_payload.get("candidates", Array());
    if (found.get_type() != Variant::ARRAY) {
        found = Array();
    }
    Dictionary answer;
    answer["type"] = p_type;
    answer["sdp"] = String(p_payload.get("sdp", String()));
    answer["candidates"] = found;

    Dictionary message;
    message["action"] = "announce";
    message["info_hash"] = hash;
    message["peer_id"] = local_peer_id;
    message["to_peer_id"] = p_to;
    message["offer_id"] = "0";
    message["answer"] = answer;

    if (tracker != nullptr && tracker->has_open()) {
        tracker->broadcast(message);
        return;
    }
    const String key = p_to + String(":") + p_type;
    for (int64_t at = 0; at < int64_t(pending.size()); at++) {
        if (pending[at].key == key) {
            pending[at].message = message;
            return;
        }
    }
    Pending held;
    held.key = key;
    held.message = message;
    pending.push_back(held);
}

void TrackerSignaler::read_packet(const Dictionary &p_data) {
    if (String(p_data.get("info_hash", String())) != hash) {
        return;
    }
    const Variant spacing
        = p_data.get("min interval", p_data.get("min_interval", Variant()));
    if (spacing.get_type() == Variant::INT
        || spacing.get_type() == Variant::FLOAT) {
        min_announce_period = double(spacing);
    }

    const String remote = p_data.get("peer_id", String());
    if (remote == local_peer_id || remote.length() != 20) {
        return;
    }
    const Variant found = p_data.get("answer", Variant());
    if (found.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary payload = found;
    const String type = payload.get("type", String());
    const int64_t peer = peer_of_address(remote);
    if (type == "offer") {
        take_inbound(peer, remote, "offer", payload, handled_offers);
    } else if (type == "answer") {
        take_inbound(peer, remote, "answer", payload, handled_answers);
    }
}

void TrackerSignaler::take_inbound(
    int64_t p_peer,
    const String &p_address,
    const String &p_kind,
    const Dictionary &p_payload,
    Dictionary &r_handled
) {
    const String key = p_address + String(":")
        + String(p_payload.get("sdp", String())).sha1_text() + String(":")
        + candidate_digest(p_payload);
    if (r_handled.has(key)) {
        return;
    }
    r_handled[key] = true;

    SignalerEvent one;
    one.kind = SignalerEvent::RECEIVED;
    one.from_peer = p_peer;
    one.from_address = p_address;
    one.kind_text = p_kind;
    one.payload = p_payload;
    publish(one);
}

void TrackerSignaler::on_session_connected(int64_t p_peer) {
    if (!is_server && p_peer == 1) {
        native_up = true;
        close_delay = SIGNALING_CLOSE_DELAY;
    }
}

void TrackerSignaler::send_stop() {
    if (tracker == nullptr) {
        return;
    }
    Dictionary message;
    message["action"] = "announce";
    message["info_hash"] = hash;
    message["peer_id"] = local_peer_id;
    message["event"] = "stopped";
    tracker->broadcast(message);
}

void TrackerSignaler::release() {
    if (tracker == nullptr) {
        return;
    }
    tracker->unsubscribe(subscription);
    subscription = 0;
    if (shared) {
        TrackerBook::shared().release(trackers, tracker);
    } else {
        tracker->close();
    }
    tracker = nullptr;
    shared = false;
}

void TrackerSignaler::close() {
    pending.clear();
    send_stop();
    release();
}

} // namespace netw::connect

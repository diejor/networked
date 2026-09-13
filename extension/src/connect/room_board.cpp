#include "netw/connect/room_board.hpp"

#include "godot/json.hpp"
#include "netw/api/server_info.hpp"
#include "netw/connect/tracker_client.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

const double RoomBoard::RECONNECT_COOLDOWN = 5.0;

String board_hash_of(const String &p_filter_uid) {
    return (p_filter_uid + String(":board")).sha1_text().substr(0, 20);
}

Dictionary RoomCard::to_dictionary() const {
    Dictionary card;
    card["t"] = "room";
    card["hash"] = room_hash;
    card["name"] = room_name;
    card["players"] = players;
    card["max"] = max_players;
    card["uid"] = filter_uid;
    card["app_id"] = String(app_id);
    card["signaling_namespace"] = signaling_namespace;
    return card;
}

bool RoomCard::from_dictionary(const Dictionary &p_card, RoomCard &r_out) {
    if (String(p_card.get("t", String())) != "room") {
        return false;
    }
    const String hash = p_card.get("hash", String());
    if (hash.is_empty()) {
        return false;
    }
    r_out.room_hash = hash;
    r_out.room_name = String(p_card.get("name", String()));
    r_out.players = int64_t(p_card.get("players", 0));
    r_out.max_players = int64_t(p_card.get("max", 0));
    r_out.filter_uid = String(p_card.get("uid", String()));
    r_out.app_id = StringName(String(p_card.get("app_id", String())));
    r_out.signaling_namespace
        = String(p_card.get("signaling_namespace", String()));
    return true;
}

TargetRow RoomCard::to_row() const {
    TargetRow row;
    row.address = room_hash;
    row.display_name = room_name;

    Dictionary metadata;
    metadata["room_hash"] = room_hash;
    metadata["host"] = room_name;
    metadata["browser_filter_uid"] = filter_uid;
    metadata["app_id"] = String(app_id);
    metadata["signaling_namespace"] = signaling_namespace;
    row.metadata = metadata;

    Ref<NetwServerInfo> info;
    info.instantiate();
    info->set_app_id(app_id);
    info->set_players(players);
    info->set_max_players(max_players);
    row.advertised = info;
    return row;
}

RoomBoard::~RoomBoard() {
    release_tracker();
}

String RoomBoard::random_hex() {
    if (dice.is_null()) {
        dice.instantiate();
        dice->randomize();
    }
    const String glyphs = "0123456789abcdef";
    String out;
    for (int64_t at = 0; at < 20; at++) {
        out += glyphs[dice->randi_range(0, glyphs.length() - 1)];
    }
    return out;
}

void RoomBoard::set_trackers(const PackedStringArray &p_trackers) {
    trackers = p_trackers;
}

void RoomBoard::ensure_tracker() {
    if (tracker != nullptr) {
        return;
    }
    if (board_hash.is_empty()) {
        board_hash = board_hash_of(filter_uid);
    }
    if (board_peer_id.is_empty()) {
        board_peer_id = random_hex();
    }
    Error err = OK;
    tracker = TrackerBook::shared().acquire(trackers, err);
    shared = tracker != nullptr;
    if (tracker == nullptr) {
        return;
    }
    subscription = tracker->subscribe();
}

void RoomBoard::release_tracker() {
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

Dictionary RoomBoard::announce_with_card() const {
    const String sdp
        = JSON::stringify(advertised.to_dictionary(), "", true, false);
    Array offers;
    for (int64_t at = 0; at < fanout; at++) {
        Dictionary offer;
        offer["type"] = "offer";
        offer["sdp"] = sdp;
        Dictionary slot;
        slot["offer_id"] = String::num_int64(at).lpad(20, "0");
        slot["offer"] = offer;
        offers.push_back(slot);
    }
    Dictionary message;
    message["action"] = "announce";
    message["info_hash"] = board_hash;
    message["peer_id"] = board_peer_id;
    message["numwant"] = fanout;
    message["offers"] = offers;
    return message;
}

Dictionary RoomBoard::query_announce() const {
    Dictionary card;
    card["t"] = "query";
    const String sdp = JSON::stringify(card, "", true, false);
    Array offers;
    for (int64_t at = 0; at < fanout; at++) {
        Dictionary offer;
        offer["type"] = "offer";
        offer["sdp"] = sdp;
        Dictionary slot;
        slot["offer_id"] = String::num_int64(at).lpad(20, "0");
        slot["offer"] = offer;
        offers.push_back(slot);
    }
    Dictionary message;
    message["action"] = "announce";
    message["info_hash"] = board_hash;
    message["peer_id"] = board_peer_id;
    message["numwant"] = fanout;
    message["offers"] = offers;
    return message;
}

void RoomBoard::advertise(const RoomCard &p_card) {
    advertised = p_card;
    advertised.filter_uid = filter_uid;
    if (advertised.room_hash.is_empty()) {
        NETW_WARN(
            sys::TRANSPORT,
            "the board refused to advertise an empty room"
        );
        return;
    }
    advertising = true;
    advertise_acc = advertise_interval;
    ensure_tracker();
}

void RoomBoard::stop_advertising() {
    advertising = false;
}

void RoomBoard::browse() {
    collected.clear();
    collecting = true;
    listing_ready = false;
    collect_left = browse_window;
    query_acc = 0.7;
    ensure_tracker();
}

bool RoomBoard::take_listing(LocalVector<TargetRow> &r_rows) {
    if (!listing_ready) {
        return false;
    }
    listing_ready = false;
    for (int64_t at = 0; at < int64_t(collected.size()); at++) {
        r_rows.push_back(collected[at].to_row());
    }
    return true;
}

void RoomBoard::keep_warm(double p_delta) {
    if (tracker == nullptr) {
        ensure_tracker();
        reconnect_acc = 0.0;
        return;
    }
    if (tracker->is_active()) {
        reconnect_acc = 0.0;
        return;
    }
    reconnect_acc += p_delta;
    if (reconnect_acc >= RECONNECT_COOLDOWN) {
        reconnect_acc = 0.0;
        release_tracker();
        ensure_tracker();
    }
}

void RoomBoard::maintain(double p_delta) {
    if (advertising || collecting) {
        idle_acc = 0.0;
        keep_warm(p_delta);
        return;
    }
    idle_acc += p_delta;
    if (idle_acc < idle_timeout) {
        keep_warm(p_delta);
        return;
    }
    if (tracker != nullptr) {
        NETW_DEBUG(
            sys::TRANSPORT,
            "the board went idle and closed its trackers"
        );
        release_tracker();
    }
    reconnect_acc = 0.0;
}

void RoomBoard::poll(double p_delta, int64_t p_now_usec, int64_t p_frame) {
    maintain(p_delta);
    if (tracker == nullptr) {
        return;
    }
    tracker->poll(p_now_usec, p_frame);

    LocalVector<TrackerEvent> events;
    tracker->drain(subscription, events);
    for (int64_t at = 0; at < int64_t(events.size()); at++) {
        if (events[at].kind == TrackerEvent::MESSAGE) {
            read_packet(events[at].data);
        }
    }

    if (advertising) {
        advertise_acc += p_delta;
        if (advertise_acc >= advertise_interval && tracker->has_open()) {
            advertise_acc = 0.0;
            tracker->broadcast(announce_with_card());
        }
    }

    if (collecting) {
        query_acc += p_delta;
        if (query_acc >= 0.75 && tracker->has_open()) {
            query_acc = 0.0;
            tracker->broadcast(query_announce());
        }
        collect_left -= p_delta;
        if (collect_left <= 0.0) {
            collecting = false;
            listing_ready = true;
            NETW_DEBUG(
                sys::TRANSPORT,
                "the board browse found %d room(s)",
                int64_t(collected.size())
            );
        }
    }
}

void RoomBoard::read_packet(const Dictionary &p_data) {
    if (String(p_data.get("info_hash", String())) != board_hash) {
        return;
    }
    const String sender = p_data.get("peer_id", String());
    if (sender == board_peer_id || sender.length() != 20) {
        return;
    }
    Variant found = p_data.get("offer", Variant());
    if (found.get_type() != Variant::DICTIONARY) {
        found = p_data.get("answer", Variant());
    }
    if (found.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary slot = found;
    const Variant parsed
        = JSON::parse_string(String(slot.get("sdp", String())));
    if (parsed.get_type() != Variant::DICTIONARY) {
        return;
    }
    const Dictionary card = parsed;
    const String tag = card.get("t", String());
    if (tag == "query") {
        if (advertising) {
            answer_card(sender, String(p_data.get("offer_id", String())));
        }
        return;
    }
    RoomCard read;
    if (tag == "room" && collecting && RoomCard::from_dictionary(card, read)) {
        collect(read);
    }
}

void RoomBoard::collect(const RoomCard &p_card) {
    if (p_card.filter_uid != filter_uid) {
        return;
    }
    for (int64_t at = 0; at < int64_t(collected.size()); at++) {
        if (collected[at].room_hash == p_card.room_hash) {
            collected[at] = p_card;
            return;
        }
    }
    NETW_DEBUG(
        sys::TRANSPORT,
        "the board discovered room %s (%d/%d)",
        p_card.room_hash,
        p_card.players,
        p_card.max_players
    );
    collected.push_back(p_card);
}

void RoomBoard::answer_card(const String &p_to, const String &p_offer_id) {
    if (tracker == nullptr) {
        return;
    }
    Dictionary answer;
    answer["type"] = "answer";
    answer["sdp"]
        = JSON::stringify(advertised.to_dictionary(), "", true, false);

    Dictionary message;
    message["action"] = "announce";
    message["info_hash"] = board_hash;
    message["peer_id"] = board_peer_id;
    message["to_peer_id"] = p_to;
    message["answer"] = answer;
    if (!p_offer_id.is_empty()) {
        message["offer_id"] = p_offer_id;
    }
    tracker->broadcast(message);
}

void RoomBoard::close() {
    advertising = false;
    collecting = false;
    listing_ready = false;
    collected.clear();
    release_tracker();
}

} // namespace netw::connect

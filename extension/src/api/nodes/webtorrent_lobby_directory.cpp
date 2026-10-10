#include "netw/api/nodes/webtorrent_lobby_directory.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/tracker_client.hpp"
#include "netw/connect/webrtc_transport.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

const char *WEBRTC_PEER_CLASS = "WebRTCMultiplayerPeer";

bool is_tracker_url(const String &p_url) {
    return p_url.begins_with("wss://") || p_url.begins_with("ws://");
}

PackedStringArray tracker_urls(const Variant &p_listed) {
    PackedStringArray listed;
    if (p_listed.get_type() == Variant::PACKED_STRING_ARRAY) {
        listed = p_listed;
    } else if (p_listed.get_type() == Variant::ARRAY) {
        const Array items = p_listed;
        for (int64_t at = 0; at < items.size(); at++) {
            listed.push_back(String(items[at]));
        }
    }
    PackedStringArray kept;
    for (int64_t at = 0; at < listed.size(); at++) {
        if (is_tracker_url(listed[at])) {
            kept.push_back(listed[at]);
        }
    }
    return kept;
}

} // namespace

void WebTorrentLobbyDirectory::set_trackers(
    const PackedStringArray &p_trackers
) {
    trackers = p_trackers;
    for (int64_t at = 0; at < trackers.size(); at++) {
        if (!trackers[at].is_empty() && !is_tracker_url(trackers[at])) {
            NETW_WARN(
                sys::TRANSPORT,
                "WebTorrentLobbyDirectory.trackers has '%s', which is not a "
                "tracker URL, so it is skipped. Use a ws:// or wss:// URL.",
                trackers[at]
            );
        }
    }
}

PackedStringArray WebTorrentLobbyDirectory::get_trackers() const {
    return trackers;
}

void WebTorrentLobbyDirectory::set_list_interval(double p_seconds) {
    listing_interval = p_seconds;
}

double WebTorrentLobbyDirectory::get_list_interval() const {
    return listing_interval;
}

double WebTorrentLobbyDirectory::list_interval() {
    return listing_interval;
}

StringName WebTorrentLobbyDirectory::peer_class() {
    return StringName("WebRTCMultiplayerPeer");
}

String WebTorrentLobbyDirectory::display_name() {
    return String("WebTorrent");
}

LobbyDirectory::Capability WebTorrentLobbyDirectory::capabilities() {
    return CAPABILITY_BROWSE;
}

Dictionary WebTorrentLobbyDirectory::webrtc_defaults() const {
    if (session == nullptr) {
        return Dictionary();
    }
    return session->transport_get_defaults(
        session->transport_find(StringName(WEBRTC_PEER_CLASS))
    );
}

PackedStringArray WebTorrentLobbyDirectory::effective_trackers() const {
    const PackedStringArray authored = tracker_urls(trackers);
    if (!authored.is_empty()) {
        return authored;
    }
    const PackedStringArray preset
        = tracker_urls(webrtc_defaults().get("trackers", Variant()));
    if (!preset.is_empty()) {
        return preset;
    }
    if (connect::under_test_runner()) {
        return PackedStringArray();
    }
    return connect::WebRTCTransport::default_trackers();
}

String WebTorrentLobbyDirectory::effective_namespace() const {
    const String preset
        = webrtc_defaults().get("signaling_namespace", String());
    if (!preset.is_empty() || session == nullptr) {
        return preset;
    }
    return String(session->session_get_app_id());
}

void WebTorrentLobbyDirectory::list_lobbies() {
    const PackedStringArray reached = effective_trackers();
    if (reached.is_empty()) {
        return;
    }
    board.set_trackers(reached);
    board.signaling_namespace = effective_namespace();
    board.browse();
}

void WebTorrentLobbyDirectory::stop_listing() {
    board.stop_browsing();
}

bool WebTorrentLobbyDirectory::listed_card(connect::RoomCard &r_card) {
    const connect::WebRTCTransport *live = session == nullptr
        ? nullptr
        : dynamic_cast<const connect::WebRTCTransport *>(
              session->connect_plane().live_transport()
          );
    if (live == nullptr || !live->tracker_signaled()) {
        return false;
    }
    const Ref<NetwServerInfo> info = listed_info(session);
    if (info.is_null()) {
        return false;
    }
    r_card.room_hash = live->join_address();
    r_card.signaling_namespace = live->namespace_of_signaling();
    r_card.room_name = info->get_motd();
    r_card.app_id = info->get_app_id();
    r_card.players = info->get_players();
    r_card.max_players = info->get_max_players();
    return !r_card.room_hash.is_empty();
}

void WebTorrentLobbyDirectory::publish_listing() {
    LocalVector<connect::TargetRow> listed;
    if (!board.take_listing(listed)) {
        return;
    }
    PackedStringArray addresses;
    PackedStringArray names;
    TypedArray<NetwServerInfo> infos;
    for (uint32_t at = 0; at < listed.size(); at++) {
        addresses.push_back(listed[at].address);
        names.push_back(listed[at].display_name);
        infos.push_back(listed[at].advertised);
    }
    publish_lobbies(addresses, names, infos);
}

void WebTorrentLobbyDirectory::service_entered(NetwMultiplayer *p_api) {
    session = p_api;
    listed_check = 0.0;
    set_process_internal(true);
}

void WebTorrentLobbyDirectory::service_exiting(NetwMultiplayer *) {
    board.close();
    session = nullptr;
    set_process_internal(false);
}

void WebTorrentLobbyDirectory::_notification(int p_what) {
    if (p_what != NOTIFICATION_INTERNAL_PROCESS) {
        return;
    }
    const double delta = get_process_delta_time();
    listed_check -= delta;
    if (listed_check <= 0.0) {
        listed_check = board.advertise_interval;
        connect::RoomCard card;
        const PackedStringArray reached = effective_trackers();
        if (!reached.is_empty() && listed_card(card)) {
            board.set_trackers(reached);
            board.advertise(card);
        } else {
            board.stop_advertising();
        }
    }
    board.poll(
        delta,
        int64_t(Time::get_singleton()->get_ticks_usec()),
        int64_t(Engine::get_singleton()->get_process_frames())
    );
    publish_listing();
}

void WebTorrentLobbyDirectory::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_trackers", "trackers"),
        &WebTorrentLobbyDirectory::set_trackers
    );
    ClassDB::bind_method(
        D_METHOD("get_trackers"),
        &WebTorrentLobbyDirectory::get_trackers
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::PACKED_STRING_ARRAY, "trackers"),
        "set_trackers",
        "get_trackers"
    );
    ClassDB::bind_method(
        D_METHOD("set_list_interval", "seconds"),
        &WebTorrentLobbyDirectory::set_list_interval
    );
    ClassDB::bind_method(
        D_METHOD("get_list_interval"),
        &WebTorrentLobbyDirectory::get_list_interval
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "list_interval",
            PROPERTY_HINT_RANGE,
            "0,60,0.1,or_greater,suffix:s"
        ),
        "set_list_interval",
        "get_list_interval"
    );
}

} // namespace netw

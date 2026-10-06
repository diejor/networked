#include "netw/api/nodes/webtorrent_lobby_directory.hpp"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/tracker_client.hpp"
#include "netw/connect/webrtc_transport.hpp"

using namespace godot;

namespace netw {

void WebTorrentLobbyDirectory::set_trackers(const PackedStringArray &p_trackers) {
    trackers = p_trackers;
}

PackedStringArray WebTorrentLobbyDirectory::get_trackers() const {
    return trackers;
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

Error WebTorrentLobbyDirectory::set_browse_settings(
    const Dictionary &p_settings
) {
    const Variant named = p_settings.get("signaling_namespace", String());
    const Variant urls = p_settings.get("trackers", PackedStringArray());
    if (named.get_type() != Variant::STRING
        || urls.get_type() != Variant::PACKED_STRING_ARRAY) {
        return ERR_INVALID_PARAMETER;
    }
    const Array keys = p_settings.keys();
    for (int64_t at = 0; at < keys.size(); at++) {
        if (String(keys[at]) != "signaling_namespace"
            && String(keys[at]) != "trackers") {
            return ERR_INVALID_PARAMETER;
        }
    }
    const PackedStringArray offered = urls;
    for (int64_t at = 0; at < offered.size(); at++) {
        if (!offered[at].begins_with("wss://")
            && !offered[at].begins_with("ws://")) {
            return ERR_INVALID_PARAMETER;
        }
    }
    signaling_namespace = named;
    if (p_settings.has("trackers")) {
        trackers = offered;
    }
    return OK;
}

PackedStringArray WebTorrentLobbyDirectory::effective_trackers() const {
    if (!trackers.is_empty()) {
        return trackers;
    }
    if (connect::under_test_runner()) {
        return PackedStringArray();
    }
    return connect::WebRTCTransport::default_trackers();
}

String WebTorrentLobbyDirectory::effective_namespace() const {
    if (!signaling_namespace.is_empty() || session == nullptr) {
        return signaling_namespace;
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

bool WebTorrentLobbyDirectory::listed_card(connect::RoomCard &r_card) {
    const connect::WebRTCTransport *live
        = session == nullptr
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
}

} // namespace netw

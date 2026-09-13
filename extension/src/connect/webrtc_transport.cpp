#include "netw/connect/webrtc_transport.hpp"

#include "godot/engine.hpp"
#include "godot/file_access.hpp"
#include "godot/javascript.hpp"
#include "godot/json.hpp"
#include "godot/os.hpp"
#include "godot/time.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/webrtc_signaler.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/transports.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

const char *WebRTCTransport::LOCAL_ROOMS_PATH = "user://local_webrtc_rooms.txt";

namespace {

bool may_reach_public_trackers() {
    static int cached = -1;
    if (cached >= 0) {
        return cached == 1;
    }
    cached = gd::engine_has_meta(StringName("GdUnitRunner")) ? 0 : 1;
    if (cached == 1) {
        const PackedStringArray args = gd::cmdline_args();
        for (int at = 0; at < args.size(); ++at) {
            if (args[at].contains("GdUnit")) {
                cached = 0;
                break;
            }
        }
    }
    return cached == 1;
}

} // namespace

Array WebRTCTransport::default_ice_servers() {
    Array servers;
    Dictionary stun;
    Array stun_urls;
    stun_urls.push_back("stun:stun.l.google.com:19302");
    stun["urls"] = stun_urls;
    servers.push_back(stun);
    return servers;
}

bool WebRTCTransport::servers_differ_from_default(const Array &p_servers) {
    if (p_servers.is_empty()) {
        return false;
    }
    return JSON::stringify(p_servers) != JSON::stringify(default_ice_servers());
}

Array WebRTCTransport::effective_ice_servers() const {
    if (ice_servers_authored) {
        return ice_servers;
    }
    const Array fetched = TurnCredentials::cached();
    return fetched.is_empty() ? ice_servers : fetched;
}

PackedStringArray WebRTCTransport::default_trackers() {
    PackedStringArray urls;
    urls.push_back("wss://tracker.openwebtorrent.com");
    urls.push_back("wss://tracker.webtorrent.dev");
    return urls;
}

bool WebRTCTransport::is_url_supported_native(const String &p_url) {
    const String url = p_url.to_lower().strip_edges();
    return !(
        url.begins_with("turns:") || url.contains("transport=tcp")
        || url.contains("transport=tls")
    );
}

Array WebRTCTransport::filter_ice_servers(const Array &p_servers) {
    if (OS::get_singleton()->has_feature("web")) {
        return p_servers;
    }
    Array kept;
    for (int64_t at = 0; at < p_servers.size(); at++) {
        if (p_servers[at].get_type() != Variant::DICTIONARY) {
            continue;
        }
        const Dictionary server = p_servers[at];
        const Variant found = server.get("urls", Array());
        if (found.get_type() != Variant::ARRAY) {
            continue;
        }
        const Array urls = found;
        Array clean;
        for (int64_t seat = 0; seat < urls.size(); seat++) {
            if (is_url_supported_native(String(urls[seat]))) {
                clean.push_back(urls[seat]);
            }
        }
        if (clean.is_empty()) {
            continue;
        }
        Dictionary copy = server.duplicate();
        copy["urls"] = clean;
        kept.push_back(copy);
    }
    return kept;
}

PackedStringArray WebRTCTransport::read_local_rooms() {
    PackedStringArray rooms;
    if (!gd::file_exists(LOCAL_ROOMS_PATH)) {
        return rooms;
    }
    Ref<FileAccess> file = FileAccess::open(LOCAL_ROOMS_PATH, FileAccess::READ);
    if (file.is_null()) {
        return rooms;
    }
    while (!file->eof_reached()) {
        const String line = file->get_line().strip_edges();
        if (!line.is_empty()) {
            rooms.push_back(line);
        }
    }
    return rooms;
}

void WebRTCTransport::write_local_rooms(const PackedStringArray &p_rooms) {
    Ref<FileAccess> file
        = FileAccess::open(LOCAL_ROOMS_PATH, FileAccess::WRITE);
    if (file.is_null()) {
        return;
    }
    for (int64_t at = 0; at < p_rooms.size(); at++) {
        file->store_line(p_rooms[at]);
    }
}

void WebRTCTransport::register_local_room(const String &p_room) {
    if (p_room.is_empty()) {
        return;
    }
    PackedStringArray rooms = read_local_rooms();
    if (rooms.find(p_room) < 0) {
        rooms.push_back(p_room);
    }
    write_local_rooms(rooms);
}

void WebRTCTransport::unregister_local_room(const String &p_room) {
    if (p_room.is_empty()) {
        return;
    }
    PackedStringArray rooms = read_local_rooms();
    const int64_t at = rooms.find(p_room);
    if (at < 0) {
        return;
    }
    rooms.remove_at(at);
    write_local_rooms(rooms);
}

bool WebRTCTransport::is_local_room(const String &p_room) {
    return !p_room.is_empty() && read_local_rooms().find(p_room) >= 0;
}

WebRTCTransport::~WebRTCTransport() {
    teardown();
}

StringName WebRTCTransport::peer_class() const {
    return StringName("WebRTCMultiplayerPeer");
}

String WebRTCTransport::display_name() const {
    return "WebRTC";
}

String WebRTCTransport::address_label() const {
    return String("Room ID");
}

String WebRTCTransport::address_placeholder() const {
    return signaling_namespace.is_empty() ? String("20-char hex")
                                          : String("5-char code");
}

String WebRTCTransport::address_help() const {
    return String("Room identifier copied from the host.");
}

Dictionary WebRTCTransport::client_settings() const {
    Dictionary settings;
    settings["signaling_namespace"] = signaling_namespace;
    settings["ice_servers"] = ice_servers;
    settings["trackers"] = trackers;
    settings["connect_retry"] = connect_retry;
    settings["max_connect_attempts"] = max_connect_attempts;
    settings["gather_timeout"] = gather_timeout;
    return settings;
}

Dictionary WebRTCTransport::host_settings() const {
    Dictionary settings = client_settings();
    settings["signaler"] = Variant();
    settings["room_name"] = String();
    settings["max_players"] = int64_t(0);
    return settings;
}

double WebRTCTransport::timeout_hint() const {
    return gather_timeout + connect_retry * double(max_connect_attempts) + 4.0;
}

void WebRTCTransport::apply_settings(const Dictionary &p_settings) {
    if (p_settings.has("signaling_namespace")) {
        signaling_namespace = String(p_settings["signaling_namespace"]);
    }
    if (p_settings.has("ice_servers")
        && p_settings["ice_servers"].get_type() == Variant::ARRAY) {
        ice_servers = p_settings["ice_servers"];
        ice_servers_authored = servers_differ_from_default(ice_servers);
    }
    if (p_settings.has("trackers")) {
        const Variant found = p_settings["trackers"];
        if (found.get_type() == Variant::PACKED_STRING_ARRAY) {
            trackers = found;
        } else if (found.get_type() == Variant::ARRAY) {
            const Array listed = found;
            PackedStringArray urls;
            for (int64_t at = 0; at < listed.size(); at++) {
                urls.push_back(String(listed[at]));
            }
            trackers = urls;
        }
    }
    if (p_settings.has("connect_retry")) {
        connect_retry = double(p_settings["connect_retry"]);
    }
    if (p_settings.has("max_connect_attempts")) {
        max_connect_attempts = int64_t(p_settings["max_connect_attempts"]);
    }
    if (p_settings.has("gather_timeout")) {
        gather_timeout = double(p_settings["gather_timeout"]);
    }
    if (signaling_namespace.is_empty()) {
        NetwMultiplayer *held = bound_session();
        if (held != nullptr) {
            signaling_namespace = String(held->session_get_app_id());
        }
    }
}

void WebRTCTransport::build(const Dictionary &p_settings) {
    Ref<NetwWebRTCSignaler> authored = p_settings.get("signaler", Variant());
    if (authored.is_valid()) {
        signaler = memnew(ScriptSignaler(authored));
        board_backed = false;
    } else {
        signaler = memnew(
            TrackerSignaler(trackers, signaling_namespace, room_characters)
        );
        board_backed = !trackers.is_empty() && may_reach_public_trackers();
    }

    session_core = memnew(WebRTCSession);
    session_core->ice_servers.clear();
    const Array offered = effective_ice_servers();
    const Array active
        = filter_unsupported_turn ? filter_ice_servers(offered) : offered;
    for (int64_t at = 0; at < active.size(); at++) {
        if (active[at].get_type() == Variant::DICTIONARY) {
            session_core->ice_servers.push_back(active[at]);
        }
    }
    session_core->connect_retry = connect_retry;
    session_core->max_connect_attempts = max_connect_attempts;
    session_core->gather_timeout = gather_timeout;
    session_core->topup_interval = topup_interval;
}

void WebRTCTransport::teardown() {
    if (credentials != nullptr) {
        memdelete(credentials);
        credentials = nullptr;
        pending_mode = -1;
        pending_address = String();
        pending_settings = Dictionary();
    }
    if (session_core != nullptr) {
        session_core->close();
        memdelete(session_core);
        session_core = nullptr;
    }
    if (signaler != nullptr) {
        signaler->close();
        memdelete(signaler);
        signaler = nullptr;
    }
    offer_reported = false;
}

void WebRTCTransport::make_peer(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (arm_credential_fetch(p_mode, p_address, p_settings)) {
        return;
    }
    if (p_mode == PEER_MODE_CLIENT) {
        join_peer(p_address, p_settings);
        return;
    }
    host_peer(p_settings);
}

bool WebRTCTransport::arm_credential_fetch(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (TurnCredentials::attempted() || !may_reach_public_trackers()) {
        return false;
    }
    if (p_settings.has("ice_servers")
        && p_settings["ice_servers"].get_type() == Variant::ARRAY
        && servers_differ_from_default(p_settings["ice_servers"])) {
        return false;
    }
    const String url = TurnCredentials::resolve_url(
        TurnCredentials::configured_url(),
        gd::document_origin()
    );
    if (url.is_empty()) {
        return false;
    }
    credentials = memnew(TurnCredentials);
    const int64_t now = int64_t(Time::get_singleton()->get_ticks_msec());
    if (credentials->begin(url, TurnCredentials::configured_headers(), now)
        != OK) {
        memdelete(credentials);
        credentials = nullptr;
        return false;
    }
    pending_mode = p_mode;
    pending_address = p_address;
    pending_settings = p_settings;
    report(StringName("discovery"), "Fetching relay credentials...", 0.2);
    return true;
}

void WebRTCTransport::drive_credential_fetch(int64_t p_now_msec) {
    credentials->poll(p_now_msec);
    const TurnCredentials::Phase reached = credentials->phase();
    if (reached != TurnCredentials::PHASE_READY
        && reached != TurnCredentials::PHASE_FAILED) {
        return;
    }
    memdelete(credentials);
    credentials = nullptr;
    resume_peer_creation();
}

void WebRTCTransport::resume_peer_creation() {
    const int mode = pending_mode;
    const String address = pending_address;
    const Dictionary settings = pending_settings;
    pending_mode = -1;
    pending_address = String();
    pending_settings = Dictionary();
    if (mode == PEER_MODE_CLIENT) {
        join_peer(address, settings);
        return;
    }
    host_peer(settings);
}

void WebRTCTransport::host_peer(const Dictionary &p_settings) {
    apply_settings(p_settings);
    hosting = true;
    build(p_settings);

    const int64_t now = int64_t(Time::get_singleton()->get_ticks_msec());
    if (session_core->create_server(now) != OK) {
        teardown();
        fail(ERR_CANT_CREATE, "the WebRTC session refused to open.");
        return;
    }
    const Error err = signaler->open(String(), 1);
    if (err != OK) {
        teardown();
        fail(
            ERR_CANT_CREATE,
            String("WebRTC signaler open failed: ") + itos(int(err))
        );
        return;
    }
    const String room = signaler->room_id();
    NETW_INFO(sys::TRANSPORT, "the WebRTC room %s is ready", room);
    register_local_room(room);
    if (!board_backed) {
        deliver(session_core->peer());
        return;
    }
    rooms.set_trackers(trackers);

    RoomCard card;
    card.room_hash = room;
    card.signaling_namespace = signaling_namespace;
    card.room_name
        = setting_string(p_settings, StringName("room_name"), String());
    card.players = 1;
    card.max_players = setting_int(p_settings, StringName("max_players"), 0);
    NetwMultiplayer *held = bound_session();
    if (held != nullptr) {
        card.app_id = held->session_get_app_id();
        if (card.max_players == 0) {
            card.max_players
                = NetwServerInfo::from_session(held)->get_max_players();
        }
    }
    rooms.advertise(card);
    deliver(session_core->peer());
}

void WebRTCTransport::join_peer(
    const String &p_address,
    const Dictionary &p_settings
) {
    apply_settings(p_settings);
    hosting = false;
    build(p_settings);

    if (is_local_room(p_address)) {
        session_core->is_local_session = true;
    }
    Ref<RandomNumberGenerator> dice;
    dice.instantiate();
    dice->randomize();
    const int64_t local_id = dice->randi_range(2, 1000001);

    const int64_t now = int64_t(Time::get_singleton()->get_ticks_msec());
    if (session_core->create_client(local_id, now) != OK) {
        teardown();
        fail(ERR_CANT_CREATE, "the WebRTC session refused to open.");
        return;
    }
    connect_started_msec = now;
    const Error err = signaler->open(p_address, local_id);
    if (err != OK) {
        teardown();
        fail(
            ERR_CANT_CONNECT,
            String("WebRTC signaler open failed: ") + itos(int(err))
        );
        return;
    }
    report(StringName("discovery"), "Reaching signaling...", 0.55);
    deliver(session_core->peer());
}

void WebRTCTransport::pump_signals() {
    LocalVector<SessionSignal> sending;
    session_core->drain_signals(sending);
    for (int64_t at = 0; at < int64_t(sending.size()); at++) {
        signaler->send(
            sending[at].to_peer,
            sending[at].to_address,
            sending[at].kind,
            sending[at].payload
        );
        if (sending[at].kind == "offer" && !offer_reported) {
            offer_reported = true;
            report(StringName("traversal"), "Negotiating peer link...", 0.8);
        }
    }
}

void WebRTCTransport::pump_outcomes() {
    LocalVector<SessionOutcome> reported;
    session_core->drain_outcomes(reported);
    for (int64_t at = 0; at < int64_t(reported.size()); at++) {
        switch (reported[at].kind) {
            case SessionOutcome::NATIVE_CONNECTED: {
                NETW_INFO(
                    sys::TRANSPORT,
                    "the WebRTC link to %d is open",
                    reported[at].peer
                );
                signaler->on_session_connected(reported[at].peer);
            } break;
            case SessionOutcome::NATIVE_DISCONNECTED: {
                NETW_INFO(
                    sys::TRANSPORT,
                    "the WebRTC link to %d is gone",
                    reported[at].peer
                );
            } break;
            case SessionOutcome::FAILED: {
                connect_started_msec = 0;
                NETW_WARN(
                    sys::TRANSPORT,
                    "the WebRTC link to %d failed: %s",
                    reported[at].peer,
                    reported[at].reason
                );
            } break;
        }
    }
}

void WebRTCTransport::pump_inbound() {
    LocalVector<SignalerEvent> heard;
    signaler->drain(heard);
    const int64_t now = int64_t(Time::get_singleton()->get_ticks_msec());
    for (int64_t at = 0; at < int64_t(heard.size()); at++) {
        switch (heard[at].kind) {
            case SignalerEvent::RECEIVED: {
                session_core->deliver(
                    heard[at].from_peer,
                    heard[at].from_address,
                    heard[at].kind_text,
                    heard[at].payload,
                    now
                );
            } break;
            case SignalerEvent::READY: {
                signaling_ready = true;
                report(
                    StringName("handshake"),
                    "Exchanging connection info...",
                    0.65
                );
            } break;
            case SignalerEvent::LOST:
            case SignalerEvent::UNREACHABLE: {
                signaling_ready = false;
                if (!hosting && session_core != nullptr
                    && !session_core->is_connected_to(1)) {
                    connect_started_msec = 0;
                    NETW_WARN(
                        sys::TRANSPORT,
                        "WebRTC signaling went away before the host answered"
                    );
                }
            } break;
        }
    }
}

void WebRTCTransport::watch_signaling(int64_t p_now_msec) {
    if (hosting || session_core == nullptr || connect_started_msec <= 0) {
        return;
    }
    if (session_core->is_connected_to(1)) {
        return;
    }
    const double elapsed = double(p_now_msec - connect_started_msec) / 1000.0;
    if (elapsed >= timeout_hint() - 0.1 && !signaling_ready) {
        connect_started_msec = 0;
        NETW_WARN(
            sys::TRANSPORT,
            "WebRTC reached no signaling inside the connect budget"
        );
    }
}

void WebRTCTransport::poll(double p_delta) {
    const int64_t now_msec = int64_t(Time::get_singleton()->get_ticks_msec());
    const int64_t now_usec = int64_t(Time::get_singleton()->get_ticks_usec());
    const int64_t frame
        = int64_t(Engine::get_singleton()->get_process_frames());

    if (credentials != nullptr) {
        drive_credential_fetch(now_msec);
        return;
    }
    if (board_backed) {
        rooms.poll(p_delta, now_usec, frame);
        publish_listing();
    }
    if (session_core == nullptr || signaler == nullptr) {
        return;
    }
    session_core->poll(now_msec);
    pump_signals();
    pump_outcomes();
    signaler->poll(p_delta, now_usec, frame);
    pump_inbound();
    watch_signaling(now_msec);
}

String WebRTCTransport::join_address() const {
    return signaler != nullptr ? signaler->room_id() : String();
}

Dictionary WebRTCTransport::diagnostics(int64_t p_peer_id) const {
    if (session_core == nullptr) {
        return Transport::diagnostics(p_peer_id);
    }
    return session_core->connection_diagnostics(p_peer_id);
}

bool WebRTCTransport::can_browse() const {
    return !trackers.is_empty() && may_reach_public_trackers();
}

void WebRTCTransport::browse() {
    board_backed = can_browse();
    if (!board_backed) {
        return;
    }
    rooms.set_trackers(trackers);
    rooms.browse();
}

void WebRTCTransport::publish_listing() {
    LocalVector<TargetRow> listed;
    if (!rooms.take_listing(listed)) {
        return;
    }
    PackedStringArray addresses;
    PackedStringArray names;
    Array infos;
    for (uint32_t at = 0; at < listed.size(); at++) {
        addresses.push_back(listed[at].address);
        names.push_back(listed[at].display_name);
        infos.push_back(listed[at].advertised);
    }
    publish_targets(addresses, names, infos);
}

void WebRTCTransport::close() {
    rooms.close();
    if (signaler != nullptr) {
        unregister_local_room(signaler->room_id());
    }
    teardown();
    hosting = false;
    signaling_ready = false;
    connect_started_msec = 0;
}

} // namespace netw::connect

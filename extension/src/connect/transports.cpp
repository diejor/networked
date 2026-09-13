#include "netw/connect/transports.hpp"

#include "godot/ip.hpp"
#include "godot/multiplayer.hpp"
#include "godot/net_peers.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/server_info.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/webrtc_transport.hpp"

using namespace godot;

namespace netw::connect {

int64_t setting_int(
    const Dictionary &p_settings,
    const StringName &p_key,
    int64_t p_fallback
) {
    return int64_t(p_settings.get(p_key, p_fallback));
}

String setting_string(
    const Dictionary &p_settings,
    const StringName &p_key,
    const String &p_fallback
) {
    return String(p_settings.get(p_key, p_fallback));
}

void split_host_port(
    const String &p_address,
    int64_t p_default_port,
    String &r_host,
    int64_t &r_port
) {
    if (p_address.is_empty()) {
        r_host = "localhost";
        r_port = p_default_port;
        return;
    }
    const int64_t at = p_address.rfind(":");
    if (at < 0) {
        r_host = p_address;
        r_port = p_default_port;
        return;
    }
    r_host = p_address.substr(0, at);
    const String port_str = p_address.substr(at + 1);
    if (port_str.is_empty() || !port_str.is_valid_int()) {
        r_port = p_default_port;
        return;
    }
    r_port = port_str.to_int();
}

String lan_address() {
    const PackedStringArray addresses = gd::local_addresses();
    for (int64_t at = 0; at < addresses.size(); at++) {
        const String address = addresses[at];
        if (address.begins_with("127.") || address.contains(":")) {
            continue;
        }
        return address;
    }
    return "127.0.0.1";
}

StringName ENetTransport::peer_class() const {
    return StringName("ENetMultiplayerPeer");
}

String ENetTransport::display_name() const {
    return "ENet";
}

bool ENetTransport::is_available() const {
    return !OS::get_singleton()->has_feature("web");
}

bool ENetTransport::can_probe() const {
    return true;
}

String ENetTransport::address_label() const {
    return String("Server IP");
}

String ENetTransport::address_placeholder() const {
    return String("localhost");
}

bool ENetTransport::accepts_empty_address() const {
    return true;
}

Dictionary ENetTransport::client_settings() const {
    Dictionary settings;
    settings["port"] = DEFAULT_PORT;
    return settings;
}

Dictionary ENetTransport::host_settings() const {
    Dictionary settings = client_settings();
    settings["max_players"] = DEFAULT_MAX_PLAYERS;
    return settings;
}

void ENetTransport::make_peer(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (p_mode == PEER_MODE_CLIENT) {
        join_peer(p_address, p_settings);
        return;
    }
    host_peer(p_settings);
}

void ENetTransport::host_peer(const Dictionary &p_settings) {
    const int64_t port = setting_int(p_settings, "port", DEFAULT_PORT);
    const int64_t max_players
        = setting_int(p_settings, "max_players", DEFAULT_MAX_PLAYERS);
    Ref<MultiplayerPeer> peer = gd::new_peer(peer_class());
    const Error err = gd::enet_create_server(peer, port, max_players);
    if (err == ERR_CANT_CREATE) {
        fail(ERR_ALREADY_IN_USE, "address already in use");
        return;
    }
    if (err != OK) {
        fail(err, "failed to create ENet server");
        return;
    }
    live = peer;
    deliver(peer);
}

void ENetTransport::join_peer(
    const String &p_address,
    const Dictionary &p_settings
) {
    const int64_t default_port = setting_int(p_settings, "port", DEFAULT_PORT);
    String host;
    int64_t port = default_port;
    split_host_port(p_address, default_port, host, port);
    Ref<MultiplayerPeer> peer = gd::new_peer(peer_class());
    const Error err = gd::enet_create_client(peer, host, port);
    if (err != OK) {
        fail(err, "failed to create ENet client");
        return;
    }
    live = peer;
    deliver(peer);
}

Ref<MultiplayerPeer> ENetTransport::make_probe_peer(const String &p_address) {
    String host;
    int64_t port = DEFAULT_PORT;
    split_host_port(p_address, DEFAULT_PORT, host, port);
    Ref<MultiplayerPeer> peer = gd::new_peer(peer_class());
    if (gd::enet_create_client(peer, host, port) != OK) {
        return Ref<MultiplayerPeer>();
    }
    return peer;
}

void ENetTransport::adopt(const Ref<MultiplayerPeer> &p_peer) {
    live = p_peer;
}

String ENetTransport::join_address() const {
    if (live.is_null() || live->get_unique_id() != 1) {
        return String();
    }
    const int64_t port = gd::enet_local_port(live);
    if (port <= 0) {
        return String();
    }
    return vformat("%s:%d", lan_address(), port);
}

void ENetTransport::close() {
    if (live.is_valid()) {
        live->close();
    }
    live = Ref<MultiplayerPeer>();
}

StringName WebSocketTransport::peer_class() const {
    return StringName("WebSocketMultiplayerPeer");
}

String WebSocketTransport::display_name() const {
    return "WebSocket";
}

bool WebSocketTransport::can_host_here() const {
    return !OS::get_singleton()->has_feature("web");
}

bool WebSocketTransport::can_probe() const {
    return true;
}

String WebSocketTransport::address_label() const {
    return String("Server URL");
}

String WebSocketTransport::address_placeholder() const {
    return String("ws://localhost:21253");
}

bool WebSocketTransport::accepts_empty_address() const {
    return true;
}

Dictionary WebSocketTransport::host_settings() const {
    Dictionary settings;
    settings["port"] = DEFAULT_PORT;
    return settings;
}

String WebSocketTransport::build_url(const String &p_address) {
    if (p_address.is_empty() || p_address == "localhost"
        || p_address == "127.0.0.1") {
        return vformat("ws://localhost:%d", DEFAULT_PORT);
    }
    if (p_address.begins_with("ws://") || p_address.begins_with("wss://")) {
        return p_address;
    }
    return "wss://" + p_address;
}

void WebSocketTransport::make_peer(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (p_mode == PEER_MODE_CLIENT) {
        join_peer(p_address, p_settings);
        return;
    }
    host_peer(p_settings);
}

void WebSocketTransport::host_peer(const Dictionary &p_settings) {
    const int64_t port = setting_int(p_settings, "port", DEFAULT_PORT);
    Ref<MultiplayerPeer> peer = gd::new_peer(peer_class());
    gd::websocket_set_outbound_buffer(peer, 1048576);
    const Error err = gd::websocket_create_server(peer, port);
    if (err != OK) {
        fail(err, "failed to create WebSocket server");
        return;
    }
    live = peer;
    bound_port = port;
    deliver(peer);
}

void WebSocketTransport::join_peer(
    const String &p_address,
    const Dictionary &p_settings
) {
    Ref<MultiplayerPeer> peer = gd::new_peer(peer_class());
    const Error err = gd::websocket_create_client(peer, build_url(p_address));
    if (err != OK) {
        fail(err, "failed to create WebSocket client");
        return;
    }
    live = peer;
    deliver(peer);
}

void WebSocketTransport::adopt(const Ref<MultiplayerPeer> &p_peer) {
    live = p_peer;
}

String WebSocketTransport::join_address() const {
    if (live.is_null() || live->get_unique_id() != 1 || bound_port <= 0) {
        return String();
    }
    return vformat("ws://%s:%d", lan_address(), bound_port);
}

void WebSocketTransport::close() {
    if (live.is_valid()) {
        live->close();
    }
    live = Ref<MultiplayerPeer>();
    bound_port = 0;
}

StringName LocalTransport::peer_class() const {
    return StringName("LocalMultiplayerPeer");
}

String LocalTransport::display_name() const {
    return "Local";
}

bool LocalTransport::can_probe() const {
    return true;
}

String LocalTransport::address_label() const {
    return String();
}

bool LocalTransport::accepts_empty_address() const {
    return true;
}

void LocalTransport::make_peer(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (p_mode == PEER_MODE_CLIENT) {
        join_peer(p_address, p_settings);
        return;
    }
    host_peer(p_settings);
}

void LocalTransport::host_peer(const Dictionary &p_settings) {
    Ref<LocalLoopbackSession> shared
        = LocalLoopbackSession::get_shared_session();
    if (!shared->has_live_server()) {
        shared->reset();
    }
    NetwMultiplayer *bound = bound_session();
    if (bound != nullptr) {
        shared->set_server_app_id(bound->session_get_app_id());
    }
    loopback = shared;
    deliver(shared->get_server_peer());
}

void LocalTransport::join_peer(
    const String &p_address,
    const Dictionary &p_settings
) {
    Ref<LocalLoopbackSession> shared
        = LocalLoopbackSession::get_shared_session();
    if (!shared->has_live_server()) {
        fail(ERR_CANT_CONNECT, "no local server is live");
        return;
    }
    loopback = shared;
    deliver(shared->create_client_peer());
}

void LocalTransport::probe(const String &p_address) {
    Ref<LocalLoopbackSession> shared
        = LocalLoopbackSession::get_shared_session();
    if (!shared->has_live_server()) {
        fail(ERR_CANT_CONNECT, "no local server is live");
        return;
    }
    NetwMultiplayer *bound = bound_session();
    Ref<NetwServerInfo> info = bound != nullptr
        ? NetwServerInfo::from_session(bound)
        : Ref<NetwServerInfo>();
    if (info.is_null()) {
        info.instantiate();
    }
    deliver_probe(info);
}

void LocalTransport::adopt(const Ref<MultiplayerPeer> &p_peer) {
    loopback = LocalLoopbackSession::get_shared_session();
}

void LocalTransport::poll(double p_delta) {
    if (loopback.is_valid()) {
        loopback->poll_frame_scoped();
    }
}

String LocalTransport::join_address() const {
    return "local";
}

void LocalTransport::close() {
    loopback = Ref<LocalLoopbackSession>();
}

namespace {

Transport *make_enet() {
    return new ENetTransport();
}

Transport *make_webrtc() {
    return new WebRTCTransport();
}

Transport *make_websocket() {
    return new WebSocketTransport();
}

Transport *make_local() {
    return new LocalTransport();
}

} // namespace

void install_native_transports() {
    TransportBook &book = TransportBook::shared();
    book.install_native(
        StringName("ENetMultiplayerPeer"),
        make_enet,
        String("ENet")
    );
    book.install_native(
        StringName("WebSocketMultiplayerPeer"),
        make_websocket,
        String("WebSocket")
    );
    book.install_native(
        StringName("LocalMultiplayerPeer"),
        make_local,
        String("Local")
    );
    book.install_native(
        StringName("WebRTCMultiplayerPeer"),
        make_webrtc,
        String("WebRTC")
    );
}

} // namespace netw::connect

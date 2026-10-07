#include "netw/api/nodes/lan_lobby_directory.hpp"

#include "godot/class_db.hpp"
#include "godot/net_peers.hpp"
#include "godot/os.hpp"
#include "godot/random.hpp"
#include "godot/time.hpp"
#include "godot/udp.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/lan_query.hpp"
#include "netw/connect/tracker_client.hpp"
#include "netw/connect/transport.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

const double LANLobbyDirectory::BROWSE_WINDOW = 0.5;

namespace {

const double LISTED_CHECK_INTERVAL = 1.0;

uint64_t random_u64() {
    Ref<RandomNumberGenerator> dice;
    dice.instantiate();
    dice->randomize();
    return (uint64_t(dice->randi()) << 32) | uint64_t(dice->randi());
}

int64_t now_usec() {
    return int64_t(Time::get_singleton()->get_ticks_usec());
}

} // namespace

void LANLobbyDirectory::set_port(int64_t p_port) {
    if (p_port < 1 || p_port > 65535) {
        NETW_WARN(
            sys::TRANSPORT,
            "LANLobbyDirectory.port was set to %d, so it was ignored. Set a "
            "port from 1 to 65535.",
            p_port
        );
        return;
    }
    port = p_port;
}

int64_t LANLobbyDirectory::get_port() const {
    return port;
}

void LANLobbyDirectory::set_port_count(int64_t p_count) {
    if (p_count < 1 || p_count > 64) {
        NETW_WARN(
            sys::TRANSPORT,
            "LANLobbyDirectory.port_count was set to %d, so it was ignored. "
            "Set a count from 1 to 64.",
            p_count
        );
        return;
    }
    port_count = p_count;
}

int64_t LANLobbyDirectory::get_port_count() const {
    return port_count;
}

void LANLobbyDirectory::set_list_interval(double p_seconds) {
    listing_interval = p_seconds;
}

double LANLobbyDirectory::get_list_interval() const {
    return listing_interval;
}

double LANLobbyDirectory::list_interval() {
    return listing_interval;
}

void LANLobbyDirectory::stop_listing() {
    close_browse();
}

StringName LANLobbyDirectory::peer_class() {
    return StringName("ENetMultiplayerPeer");
}

String LANLobbyDirectory::display_name() {
    return String("LAN");
}

LobbyDirectory::Capability LANLobbyDirectory::capabilities() {
    return CAPABILITY_BROWSE;
}

bool LANLobbyDirectory::is_available() {
    return !OS::get_singleton()->has_feature("web");
}

uint64_t LANLobbyDirectory::app_tag() const {
    return session == nullptr
        ? 0
        : uint64_t(session->session_app_tag(session->session_get_app_id()));
}

int64_t LANLobbyDirectory::gameplay_port() const {
    if (session == nullptr) {
        return 0;
    }
    const Ref<MultiplayerPeer> peer
        = session->NETW_API_VIRTUAL(get_multiplayer_peer)();
    if (connect::peer_class_of(peer) != StringName("ENetMultiplayerPeer")
        || peer->get_connection_status()
            != MultiplayerPeer::CONNECTION_CONNECTED
        || peer->get_unique_id() != 1) {
        return 0;
    }
    return gd::enet_local_port(peer);
}

void LANLobbyDirectory::keep_host_socket() {
    listed = gameplay_port() > 0 ? listed_info(session) : Ref<NetwServerInfo>();
    if (listed.is_null()) {
        close_host();
        return;
    }
    for (int64_t at = 0; host_socket.is_null() && at < port_count; at++) {
        host_socket = gd::udp_bind(port + at);
    }
    if (host_socket.is_null() && !warned_ports) {
        warned_ports = true;
        NETW_WARN(
            sys::TRANSPORT,
            "LANLobbyDirectory found no free port from %d to %d, so this game "
            "is not listed on the LAN. Set port or port_count to a free range.",
            port,
            port + port_count - 1
        );
    }
}

void LANLobbyDirectory::answer_queries() {
    gd::Datagram asked;
    while (gd::udp_take(host_socket, asked)) {
        uint64_t their_nonce = 0;
        if (!connect::lan::decode_query(asked.bytes, app_tag(), their_nonce)) {
            continue;
        }
        gd::udp_send(
            host_socket,
            asked.ip,
            asked.port,
            connect::lan::encode_reply(
                app_tag(),
                their_nonce,
                instance,
                gameplay_port(),
                listed
            )
        );
    }
}

void LANLobbyDirectory::list_lobbies() {
    if (!is_available() || session == nullptr) {
        return;
    }
    close_browse();
    browse_socket = gd::udp_bind(0);
    if (browse_socket.is_null()) {
        return;
    }
    nonce = random_u64();
    asked_usec = now_usec();
    window_left = BROWSE_WINDOW;
    const String reached = connect::under_test_runner()
        ? String("127.0.0.1")
        : query_address;
    const PackedByteArray query = connect::lan::encode_query(app_tag(), nonce);
    for (int64_t at = 0; at < port_count; at++) {
        gd::udp_send(browse_socket, reached, port + at, query);
    }
}

void LANLobbyDirectory::collect_replies() {
    gd::Datagram answered;
    while (gd::udp_take(browse_socket, answered)) {
        connect::lan::Reply reply;
        if (!connect::lan::decode_reply(
                answered.bytes,
                app_tag(),
                nonce,
                reply
            )
            || reply.instance == instance || seen.has(reply.instance)) {
            continue;
        }
        seen.push_back(reply.instance);
        reply.info->set_latency_ms((now_usec() - asked_usec) / 1000);
        const String address
            = answered.ip + ":" + String::num_int64(reply.gameplay_port);
        const String motd = reply.info->get_motd();
        addresses.push_back(address);
        names.push_back(motd.is_empty() ? address : motd);
        infos.push_back(reply.info);
    }
}

void LANLobbyDirectory::close_host() {
    gd::udp_close(host_socket);
    host_socket = Ref<RefCounted>();
}

void LANLobbyDirectory::close_browse() {
    gd::udp_close(browse_socket);
    browse_socket = Ref<RefCounted>();
    seen.clear();
    addresses = PackedStringArray();
    names = PackedStringArray();
    infos = TypedArray<NetwServerInfo>();
}

void LANLobbyDirectory::service_entered(NetwMultiplayer *p_api) {
    session = p_api;
    instance = random_u64();
    listed_check = 0.0;
    set_process_internal(is_available());
}

void LANLobbyDirectory::service_exiting(NetwMultiplayer *) {
    close_host();
    close_browse();
    session = nullptr;
    set_process_internal(false);
}

void LANLobbyDirectory::_notification(int p_what) {
    if (p_what == NOTIFICATION_INTERNAL_PROCESS) {
        tick(get_process_delta_time());
    }
}

void LANLobbyDirectory::tick(double p_delta) {
    listed_check -= p_delta;
    if (listed_check <= 0.0) {
        listed_check = LISTED_CHECK_INTERVAL;
        keep_host_socket();
    }
    answer_queries();
    if (browse_socket.is_null()) {
        return;
    }
    collect_replies();
    window_left -= p_delta;
    if (window_left <= 0.0) {
        const PackedStringArray found_addresses = addresses;
        const PackedStringArray found_names = names;
        const TypedArray<NetwServerInfo> found_infos = infos;
        close_browse();
        publish_lobbies(found_addresses, found_names, found_infos);
    }
}

void LANLobbyDirectory::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_port", "port"),
        &LANLobbyDirectory::set_port
    );
    ClassDB::bind_method(D_METHOD("get_port"), &LANLobbyDirectory::get_port);
    ClassDB::bind_method(
        D_METHOD("set_port_count", "port_count"),
        &LANLobbyDirectory::set_port_count
    );
    ClassDB::bind_method(
        D_METHOD("get_port_count"),
        &LANLobbyDirectory::get_port_count
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "port", PROPERTY_HINT_RANGE, "1,65535"),
        "set_port",
        "get_port"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "port_count", PROPERTY_HINT_RANGE, "1,64"),
        "set_port_count",
        "get_port_count"
    );
    ClassDB::bind_method(
        D_METHOD("set_list_interval", "seconds"),
        &LANLobbyDirectory::set_list_interval
    );
    ClassDB::bind_method(
        D_METHOD("get_list_interval"),
        &LANLobbyDirectory::get_list_interval
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

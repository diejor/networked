#include "netw/netw_multiplayer.hpp"
#include "godot/class_db.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;
using namespace netw;

namespace netw {

void NetwMultiplayerCore::_bind_methods() {
    ClassDB::bind_method(D_METHOD("poll"), &NetwMultiplayerCore::poll);
    ClassDB::bind_method(
        D_METHOD("set_multiplayer_peer", "peer"),
        &NetwMultiplayerCore::set_multiplayer_peer
    );
    ClassDB::bind_method(
        D_METHOD("get_multiplayer_peer"),
        &NetwMultiplayerCore::get_multiplayer_peer
    );
    ClassDB::bind_method(
        D_METHOD("has_multiplayer_peer"),
        &NetwMultiplayerCore::has_multiplayer_peer
    );
    ClassDB::bind_method(
        D_METHOD("is_server"),
        &NetwMultiplayerCore::is_server
    );
    ClassDB::bind_method(
        D_METHOD("get_unique_id"),
        &NetwMultiplayerCore::get_unique_id
    );
    ClassDB::bind_method(
        D_METHOD("get_peer_ids"),
        &NetwMultiplayerCore::get_peer_ids
    );
    ClassDB::bind_method(
        D_METHOD("set_peer_ids", "peer_ids"),
        &NetwMultiplayerCore::set_peer_ids
    );
}

NetwMultiplayerCore::NetwMultiplayerCore() {
}

NetwMultiplayerCore::~NetwMultiplayerCore() {
}

Error NetwMultiplayerCore::poll() {
    NETW_ZONE_SYS(profile::SUBSYSTEM_SESSION);
    NETW_ZONE_COLOR(colors::SESSION);
    if (peer.is_valid()) {
        peer->poll();
    }
    return OK;
}

void NetwMultiplayerCore::set_multiplayer_peer(
    const Ref<MultiplayerPeer> &p_peer
) {
    peer = p_peer;
}

Ref<MultiplayerPeer> NetwMultiplayerCore::get_multiplayer_peer() const {
    return peer;
}

bool NetwMultiplayerCore::has_multiplayer_peer() const {
    return peer.is_valid();
}

bool NetwMultiplayerCore::is_server() const {
    return get_unique_id() == 1;
}

int32_t NetwMultiplayerCore::get_unique_id() const {
    if (!peer.is_valid()
        || peer->get_connection_status()
            == MultiplayerPeer::CONNECTION_DISCONNECTED) {
        return 1;
    }
    return peer->get_unique_id();
}

PackedInt32Array NetwMultiplayerCore::get_peer_ids() const {
    return peer_ids;
}

void NetwMultiplayerCore::set_peer_ids(
    const PackedInt32Array &p_peer_ids
) {
    peer_ids = p_peer_ids;
}

} // namespace netw

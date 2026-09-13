#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwServerPredicate {

using namespace godot;
using netw::LocalMultiplayerPeer;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

Ref<LocalMultiplayerPeer> a_client_peer(int p_id) {
    Ref<LocalMultiplayerPeer> server;
    server.instantiate();
    Ref<LocalMultiplayerPeer> client;
    client.instantiate();
    server->create_server();
    client->create_client(p_id);
    server->force_connect_peer(p_id, client.ptr());
    client->force_connect_peer(1, server.ptr());
    return client;
}

TEST_CASE(
    "[Networked][Session][Authority] SP1 a session holding no peer authors as "
    "its own server, so an offline rig writes state without forging a role"
) {
    Ref<NetwMultiplayer> session = make_session();

    NETW_CHECK_EQ(int(session->is_server()), 1);
    NETW_CHECK_EQ(int(session->get_unique_id()), 1);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Authority] SP2 a connected client is not the server, "
    "and the same session is the server again once its peer is gone, because "
    "the predicate reads the peer's own connection status rather than a role "
    "the session remembers"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<LocalMultiplayerPeer> client = a_client_peer(7);
    session->set("multiplayer_peer", client);

    NETW_CHECK_EQ(int(session->get_unique_id()), 7);
    NETW_CHECK_EQ(int(session->is_server()), 0);

    client->close();

    NETW_CHECK_EQ(
        int(client->get_connection_status()),
        int(MultiplayerPeer::CONNECTION_DISCONNECTED)
    );
    NETW_CHECK_EQ(int(session->get_unique_id()), 1);
    NETW_CHECK_EQ(int(session->is_server()), 1);

    session->embed_dispose();
}

} // namespace TestNetwServerPredicate

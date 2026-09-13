#pragma once

#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "modules/multiplayer/scene_multiplayer.h"
#include "netw/api/loopback.hpp"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace TestNetworkedHarness {

using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;

struct SessionPair {
    godot::Ref<LocalLoopbackSession> session;
    godot::Ref<godot::SceneMultiplayer> server;
    godot::Ref<godot::SceneMultiplayer> client;
    int client_id = 0;
};

SessionPair make_session_pair() {
    SessionPair pair;
    pair.session.instantiate();

    godot::Ref<LocalMultiplayerPeer> server_peer
        = pair.session->get_server_peer();
    godot::Ref<LocalMultiplayerPeer> client_peer
        = pair.session->create_client_peer();
    pair.client_id = client_peer->get_unique_id();

    pair.server.instantiate();
    pair.client.instantiate();
    pair.server->set_root_path(godot::NodePath("/"));
    pair.client->set_root_path(godot::NodePath("/"));
    pair.server->set_multiplayer_peer(server_peer);
    pair.client->set_multiplayer_peer(client_peer);
    return pair;
}

TEST_CASE(
    "[Networked][Transport] Two SceneMultiplayers admit each other over the "
    "loopback pair"
) {
    SessionPair pair = make_session_pair();

    SIGNAL_WATCH(pair.server.ptr(), "peer_connected");
    SIGNAL_WATCH(pair.client.ptr(), "connected_to_server");

    CHECK_EQ(pair.server->poll(), godot::Error::OK);
    CHECK_EQ(pair.client->poll(), godot::Error::OK);

    SIGNAL_CHECK("peer_connected", godot::Array({{pair.client_id}}));
    SIGNAL_CHECK("connected_to_server", godot::Array({godot::Array()}));
    CHECK(pair.server->get_connected_peers().has(pair.client_id));
    CHECK(pair.client->get_connected_peers().has(1));

    SIGNAL_UNWATCH(pair.server.ptr(), "peer_connected");
    SIGNAL_UNWATCH(pair.client.ptr(), "connected_to_server");
}

TEST_CASE("[Networked][Transport] send_bytes crosses the loopback pair") {
    SessionPair pair = make_session_pair();

    CHECK_EQ(pair.server->poll(), godot::Error::OK);
    CHECK_EQ(pair.client->poll(), godot::Error::OK);

    SIGNAL_WATCH(pair.server.ptr(), "peer_packet");

    godot::PackedByteArray payload;
    payload.push_back(0xAB);
    payload.push_back(0xCD);
    CHECK_EQ(
        pair.client->send_bytes(
            payload,
            1,
            godot::MultiplayerPeer::TRANSFER_MODE_RELIABLE,
            0
        ),
        godot::Error::OK
    );
    CHECK_EQ(pair.server->poll(), godot::Error::OK);

    SIGNAL_CHECK("peer_packet", godot::Array({{pair.client_id, payload}}));

    SIGNAL_UNWATCH(pair.server.ptr(), "peer_packet");
}

TEST_CASE(
    "[Networked][Transport] A conditioned link delays send_bytes across the "
    "loopback pair"
) {
    SessionPair pair = make_session_pair();
    CHECK_EQ(pair.server->poll(), godot::Error::OK);
    CHECK_EQ(pair.client->poll(), godot::Error::OK);

    godot::Ref<netw::LocalLinkConditions> slow
        = netw::LocalLinkConditions::create(1);
    slow->set_latency_ms(3.0 * 1000.0 / 60.0);
    pair.session->set_link_conditions(
        pair.session->get_server_peer().ptr(),
        slow
    );

    SIGNAL_WATCH(pair.server.ptr(), "peer_packet");

    godot::PackedByteArray payload;
    payload.push_back(0x01);
    CHECK_EQ(
        pair.client->send_bytes(
            payload,
            1,
            godot::MultiplayerPeer::TRANSFER_MODE_RELIABLE,
            0
        ),
        godot::Error::OK
    );

    pair.session->poll();
    CHECK_EQ(pair.server->poll(), godot::Error::OK);
    SIGNAL_CHECK_FALSE("peer_packet");

    pair.session->poll();
    pair.session->poll();
    CHECK_EQ(pair.server->poll(), godot::Error::OK);
    SIGNAL_CHECK("peer_packet", godot::Array({{pair.client_id, payload}}));

    SIGNAL_UNWATCH(pair.server.ptr(), "peer_packet");
}

TEST_CASE(
    "[Networked][Transport][SceneTree] A scoped SceneMultiplayer installs "
    "under the "
    "fixture tree"
) {
    godot::SceneTree *tree = godot::SceneTree::get_singleton();
    REQUIRE(tree != nullptr);

    godot::Node *anchor = memnew(godot::Node);
    anchor->set_name("NetwHarnessAnchor");
    tree->get_root()->add_child(anchor);

    godot::Ref<godot::SceneMultiplayer> api;
    api.instantiate();
    tree->set_multiplayer(api, anchor->get_path());
    godot::Ref<godot::MultiplayerAPI> scoped
        = tree->get_multiplayer(anchor->get_path());
    godot::Ref<godot::MultiplayerAPI> root_api = tree->get_multiplayer();
    CHECK(scoped.ptr() == api.ptr());
    CHECK(root_api.ptr() != api.ptr());

    tree->set_multiplayer(
        godot::Ref<godot::MultiplayerAPI>(),
        anchor->get_path()
    );
    tree->get_root()->remove_child(anchor);
    memdelete(anchor);
}

} // namespace TestNetworkedHarness

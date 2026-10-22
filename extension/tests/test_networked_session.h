#pragma once

#include "support/fault_log.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "core/object/callable_mp.h"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "godot/utility.hpp"
#include "modules/multiplayer/scene_multiplayer.h"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/carrier_frame.hpp"
#include "netw/scene_core.hpp"
#include "netw/session/frames.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

namespace TestNetworkedSession {

using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;
using netw::NetwCarrierFrame;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;

struct SessionSide {
    godot::Ref<LocalMultiplayerPeer> peer;
    godot::Ref<godot::SceneMultiplayer> inner;
    godot::Ref<NetwMultiplayer> core;
};

struct NativeSessionPair {
    godot::Ref<LocalLoopbackSession> session;
    SessionSide server;
    SessionSide client;
    int client_id = 0;
};

SessionSide make_side(const godot::Ref<LocalMultiplayerPeer> &p_peer) {
    SessionSide side;
    side.peer = p_peer;
    side.inner.instantiate();
    side.inner->set_root_path(godot::NodePath("/"));
    side.inner->set_multiplayer_peer(p_peer);
    side.core.instantiate();
    side.core->session_set_inner(side.inner);
    side.core->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
    return side;
}

NativeSessionPair make_native_pair() {
    NativeSessionPair pair;
    pair.session.instantiate();
    pair.server = make_side(pair.session->get_server_peer());
    const godot::Ref<LocalMultiplayerPeer> client_peer
        = pair.session->create_client_peer();
    pair.client_id = client_peer->get_unique_id();
    pair.client = make_side(client_peer);
    return pair;
}

void adopt_transport_roster(SessionSide &p_side) {
    p_side.core->set_peer_ids(netw::gd::api_peer_ids(p_side.inner));
}

void settle_pair(NativeSessionPair &p_pair) {
    p_pair.server.inner->poll();
    p_pair.client.inner->poll();
    p_pair.server.core->NETW_API_VIRTUAL(poll)();
    p_pair.client.core->NETW_API_VIRTUAL(poll)();
    adopt_transport_roster(p_pair.server);
    adopt_transport_roster(p_pair.client);
}

godot::PackedByteArray two_bytes(uint8_t p_first, uint8_t p_second) {
    godot::PackedByteArray bytes;
    bytes.push_back(p_first);
    bytes.push_back(p_second);
    return bytes;
}

godot::PackedByteArray clock_reply(SessionSide &p_receiver) {
    netw::session::ClockRate rate;
    rate.tickrate = p_receiver.core->clock_engine().get_tickrate();
    return netw::wire::frame_pack(
        0,
        0,
        netw::wire::builtin_channel("CLOCK_HANDSHAKE_REPLY"),
        netw::session::frame_write(rate),
        godot::String()
    );
}

struct InboundSink {
    int64_t sender = 0;
    int64_t count = 0;
    godot::PackedByteArray packet;
};

InboundSink inbound_sink;

void note_inbound_packet(int p_id, const godot::PackedByteArray &p_packet) {
    inbound_sink.sender = p_id;
    inbound_sink.count += 1;
    inbound_sink.packet = p_packet;
}

void arm_inbound_sink(SessionSide &p_side) {
    inbound_sink = InboundSink();
    p_side.inner->connect(
        "peer_packet",
        callable_mp_static(&note_inbound_packet)
    );
}

void disarm_inbound_sink(SessionSide &p_side) {
    p_side.inner->disconnect(
        "peer_packet",
        callable_mp_static(&note_inbound_packet)
    );
}

TEST_CASE(
    "[Networked][Session] Two NetwMultiplayers admit each other over the "
    "loopback pair with no GDScript in the picture"
) {
    NativeSessionPair pair = make_native_pair();

    settle_pair(pair);

    NETW_CHECK_EQ(pair.server.core->NETW_API_VIRTUAL(get_unique_id)(), 1);
    NETW_CHECK_EQ(
        pair.client.core->NETW_API_VIRTUAL(get_unique_id)(),
        pair.client_id
    );
    NETW_CHECK_EQ(pair.server.core->is_server(), true);
    NETW_CHECK_EQ(pair.client.core->is_server(), false);

    const godot::PackedInt32Array server_view
        = pair.server.core->NETW_API_VIRTUAL(get_peer_ids)();
    const godot::PackedInt32Array client_view
        = pair.client.core->NETW_API_VIRTUAL(get_peer_ids)();

    NETW_CHECK_EQ(server_view.size(), 1);
    NETW_CHECK_GE(server_view.find(pair.client_id), 0);
    NETW_CHECK_EQ(client_view.size(), 1);
    NETW_CHECK_GE(client_view.find(1), 0);
}

TEST_CASE(
    "[Networked][Session] A seated transport owns the roster, so a core "
    "answers the transport's own peers and a cache written before the "
    "transport was seated cannot show through"
) {
    NativeSessionPair pair = make_native_pair();

    pair.server.inner->poll();
    pair.client.inner->poll();
    pair.server.core->NETW_API_VIRTUAL(poll)();
    pair.client.core->NETW_API_VIRTUAL(poll)();

    NETW_CHECK_EQ(netw::gd::api_peer_ids(pair.server.inner).size(), 1);
    NETW_CHECK_EQ(pair.server.core->NETW_API_VIRTUAL(get_peer_ids)().size(), 1);

    godot::PackedInt32Array stale;
    stale.push_back(11);
    stale.push_back(12);
    pair.server.core->set_peer_ids(stale);

    NETW_CHECK_EQ(pair.server.core->NETW_API_VIRTUAL(get_peer_ids)().size(), 1);
}

TEST_CASE(
    "[Networked][Session] A datagram framed by one core is read back as a "
    "carrier header by the other core"
) {
    NativeSessionPair pair = make_native_pair();
    settle_pair(pair);
    arm_inbound_sink(pair.client);

    const godot::PackedByteArray payload = clock_reply(pair.client);
    pair.server.core->send_datagram(pair.client_id, payload, true);

    pair.client.inner->poll();

    NETW_CHECK_EQ(inbound_sink.count, 1);
    NETW_CHECK_EQ(inbound_sink.sender, 1);

    const int64_t counted_packets = pair.client.core->get_received_packets();
    const int64_t counted_bytes = pair.client.core->get_received_bytes();

    const NetwCarrierFrame header = pair.client.core->receive_header(
        inbound_sink.sender,
        inbound_sink.packet
    );
    NETW_CHECK_EQ(header.kind, int64_t(NetwCarrierFrame::RELIABLE));
    NETW_CHECK_EQ(
        inbound_sink.packet.size() - header.payload_offset,
        payload.size()
    );
    for (int64_t at = 0; at < payload.size(); ++at) {
        NETW_CHECK_EQ(
            inbound_sink.packet[header.payload_offset + at],
            payload[at]
        );
    }
    NETW_CHECK_EQ(counted_packets, 1);
    NETW_CHECK_EQ(counted_bytes, payload.size());

    disarm_inbound_sink(pair.client);
}

TEST_CASE(
    "[Networked][Session] Only an unreliable datagram spends a sequence, and "
    "one addressed to a peer the sender's transport does not report is "
    "refused before it spends one"
) {
    NativeSessionPair pair = make_native_pair();
    settle_pair(pair);

    NETW_CHECK_EQ(
        pair.server.core
            ->send_datagram(pair.client_id, two_bytes(0x01, 0x02), true),
        -1
    );
    const int64_t spent = pair.server.core->send_datagram(
        pair.client_id,
        two_bytes(0x03, 0x04),
        false
    );
    NETW_CHECK_GE(spent, 0);
    NETW_CHECK_EQ(
        pair.server.core
            ->send_datagram(pair.client_id + 41, two_bytes(0x05, 0x06), false),
        -1
    );
    NETW_CHECK_EQ(
        pair.server.core
            ->send_datagram(pair.client_id, two_bytes(0x07, 0x08), false),
        spent + 1
    );
}

TEST_CASE(
    "[Networked][Session][SceneTree] A GDScript-free session pair installs as "
    "two scoped multiplayer APIs under the fixture tree"
) {
    godot::SceneTree *tree = godot::SceneTree::get_singleton();
    REQUIRE(tree != nullptr);

    godot::Node *server_anchor = memnew(godot::Node);
    server_anchor->set_name("NetwSessionServer");
    godot::Node *client_anchor = memnew(godot::Node);
    client_anchor->set_name("NetwSessionClient");
    tree->get_root()->add_child(server_anchor);
    tree->get_root()->add_child(client_anchor);

    NativeSessionPair pair = make_native_pair();
    tree->set_multiplayer(pair.server.core, server_anchor->get_path());
    tree->set_multiplayer(pair.client.core, client_anchor->get_path());

    NETW_CHECK_EQ(
        tree->get_multiplayer(server_anchor->get_path()).ptr()
            == pair.server.core.ptr(),
        true
    );
    NETW_CHECK_EQ(
        tree->get_multiplayer(client_anchor->get_path()).ptr()
            == pair.client.core.ptr(),
        true
    );

    settle_pair(pair);
    arm_inbound_sink(pair.client);

    NETW_CHECK_EQ(pair.server.core->NETW_API_VIRTUAL(get_unique_id)(), 1);
    NETW_CHECK_EQ(
        pair.client.core->NETW_API_VIRTUAL(get_unique_id)(),
        pair.client_id
    );
    NETW_CHECK_GE(
        pair.server.core->NETW_API_VIRTUAL(get_peer_ids)().find(pair.client_id),
        0
    );

    const godot::PackedByteArray payload = clock_reply(pair.client);
    pair.server.core->send_datagram(pair.client_id, payload, true);
    pair.client.inner->poll();
    NETW_CHECK_EQ(inbound_sink.count, 1);
    disarm_inbound_sink(pair.client);

    tree->set_multiplayer(
        godot::Ref<godot::MultiplayerAPI>(),
        server_anchor->get_path()
    );
    tree->set_multiplayer(
        godot::Ref<godot::MultiplayerAPI>(),
        client_anchor->get_path()
    );
    tree->get_root()->remove_child(server_anchor);
    tree->get_root()->remove_child(client_anchor);
    memdelete(server_anchor);
    memdelete(client_anchor);
}

TEST_CASE(
    "[Networked][Session][SceneTree] A packed in-memory template seats a "
    "native entity with no GDScript and no res:// scene file"
) {
    godot::SceneTree *tree = godot::SceneTree::get_singleton();
    REQUIRE(tree != nullptr);

    godot::Node *fixture_parent = memnew(godot::Node);
    fixture_parent->set_name("NetwSpawnFixture");
    tree->get_root()->add_child(fixture_parent);

    godot::Node *root_template = memnew(godot::Node);
    root_template->set_name("Npc");
    root_template->set("process_priority", 7);
    godot::Node *child_template = memnew(godot::Node);
    child_template->set_name("Marker");
    root_template->add_child(child_template);
    child_template->set_owner(root_template);

    godot::Ref<godot::PackedScene> packed;
    packed.instantiate();
    NETW_CHECK_EQ(packed->pack(root_template), godot::Error::OK);

    {
        godot::Node *copy = packed->instantiate();
        REQUIRE(copy != nullptr);
        NETW_CHECK_EQ(copy->get_name() == godot::StringName("Npc"), true);
        NETW_CHECK_EQ(copy->get_child_count(), 1);
        NETW_CHECK_EQ(int64_t(copy->get("process_priority")), 7);

        NetwMultiplayer::wrapper_ensure(copy);
        fixture_parent->add_child(copy);

        const godot::Ref<NetwEntity> entity = NetwEntity::of(copy);
        REQUIRE(entity.is_valid());
        NETW_CHECK_EQ(entity->get_owner(), copy);
        NETW_CHECK_EQ(entity->get_peer_id(), 0);
        REQUIRE(entity->get_route() > 0);
        const godot::StringName default_name(
            godot::String("Npc@")
            + godot::String::num_int64(entity->get_route())
        );
        NETW_CHECK_EQ(entity->get_name() == default_name, true);
        NETW_CHECK_EQ(entity->get_is_player(), false);
        NETW_CHECK_EQ(entity->is_on_server(), true);
        NETW_CHECK_EQ(copy->get_parent(), fixture_parent);

        fixture_parent->remove_child(copy);
        memdelete(copy);
    }

    {
        godot::Node *copy = packed->instantiate();
        REQUIRE(copy != nullptr);

        NetwMultiplayer::wrapper_stamp_identity(
            copy,
            godot::StringName("courier"),
            7
        );
        fixture_parent->add_child(copy);

        NETW_CHECK_EQ(copy->get_name() == godot::StringName("courier"), true);

        const godot::Ref<NetwEntity> entity = NetwEntity::of(copy);
        REQUIRE(entity.is_valid());
        NETW_CHECK_EQ(entity->get_name() == godot::StringName("courier"), true);
        NETW_CHECK_EQ(entity->get_peer_id(), 7);
        NETW_CHECK_EQ(entity->get_is_player(), true);
        NETW_CHECK_EQ(copy->get_parent(), fixture_parent);

        fixture_parent->remove_child(copy);
        memdelete(copy);
    }

    tree->get_root()->remove_child(fixture_parent);
    memdelete(fixture_parent);
    memdelete(root_template);
}

} // namespace TestNetworkedSession

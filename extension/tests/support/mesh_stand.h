#pragma once

#include "netw_test.h"

#include <cstring>

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"

#if defined(NETW_TIER_HOSTED)
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/script.hpp>
#endif

namespace netw_test {

#if defined(NETW_TIER_HOSTED)

struct CapturedPacket {
    godot::PackedByteArray bytes;
    int sender = 0;
    int destination = 0;
    int channel = 0;
    int mode = 0;
};

inline godot::Error mesh_put(
    const godot::Ref<netw::LocalMultiplayerPeer> &p_peer,
    const godot::PackedByteArray &p_bytes
) {
    return p_peer->put_packet(p_bytes);
}

inline godot::PackedByteArray mesh_take(
    const godot::Ref<netw::LocalMultiplayerPeer> &p_peer
) {
    return p_peer->get_packet();
}

class MeshStand {
    godot::Ref<netw::LocalLoopbackSession> link;
    godot::Vector<int> ids;
    godot::Vector<godot::Ref<netw::LocalMultiplayerPeer>> peers;
    godot::Vector<godot::Ref<netw::NetwMultiplayer>> sessions;
    godot::Vector<godot::NodePath> mounted_paths;
    int coordinator = 1;
    bool relays_through_hub = false;
    double tick_period_ms = 1000.0 / 30.0;

    int index_of(int p_id) const {
        for (int at = 0; at < ids.size(); ++at) {
            if (ids[at] == p_id) {
                return at;
            }
        }
        return -1;
    }

    godot::Ref<netw::NetwMultiplayer> make_session(
        const godot::Ref<netw::LocalMultiplayerPeer> &p_peer
    ) {
        godot::Ref<godot::SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(godot::NodePath("/"));
        inner->set_server_relay_enabled(relays_through_hub);

        godot::Ref<netw::NetwMultiplayer> session
            = netw::NetwMultiplayer::make(inner, godot::Ref<godot::Script>());
        REQUIRE_MESSAGE(session.is_valid(), "the session did not instantiate");
        if (session.is_valid()) {
            session->session_set_authority_peer(coordinator);
            session->set("multiplayer_peer", p_peer);
        }
        return session;
    }

    int seat(int p_id, bool p_holds_authority) {
        REQUIRE_MESSAGE(index_of(p_id) < 0, "that peer id is already seated");
        godot::Ref<netw::LocalMultiplayerPeer> peer;
        peer.instantiate();
        peer->set_loopback_session(link.ptr());
        if (p_holds_authority) {
            peer->create_server(p_id);
        } else {
            peer->create_client(p_id);
        }
        peer->NETW_PEER_VIRTUAL(poll)();
        ids.push_back(p_id);
        peers.push_back(peer);
        sessions.push_back(make_session(peer));
        return ids.size() - 1;
    }

public:
    MeshStand() {
        link.instantiate();
    }

    ~MeshStand() {
        godot::SceneTree *tree = netw::gd::scene_tree();
        if (tree == nullptr) {
            return;
        }
        for (const godot::NodePath &path : mounted_paths) {
            tree->set_multiplayer(godot::Ref<godot::MultiplayerAPI>(), path);
        }
    }

    MeshStand(const MeshStand &) = delete;
    MeshStand &operator=(const MeshStand &) = delete;

    void mount(int p_id, godot::Node *p_at) {
        netw::NetwMultiplayer *session = session_of(p_id);
        REQUIRE_MESSAGE(session != nullptr, "a mount needs a seated peer");
        REQUIRE_MESSAGE(p_at != nullptr, "a mount needs a node to sit under");
        godot::SceneTree *tree = netw::gd::scene_tree();
        REQUIRE_MESSAGE(tree != nullptr, "a mount needs a tree to register in");
        if (session == nullptr || p_at == nullptr || tree == nullptr) {
            return;
        }
        const godot::NodePath path = p_at->get_path();
        session->session_get_inner()->set_root_path(path);
        tree->set_multiplayer(
            godot::Ref<godot::MultiplayerAPI>(
                godot::Object::cast_to<godot::MultiplayerAPI>(session)
            ),
            path
        );
        mounted_paths.push_back(path);
        REQUIRE_MESSAGE(
            session->session_root() == p_at,
            "the session did not take that node as its root"
        );
    }

    void declare_coordinator(int p_id) {
        REQUIRE_MESSAGE(
            ids.is_empty(),
            "declare the coordinator before any seat"
        );
        coordinator = p_id;
    }

    void relay_through_hub() {
        REQUIRE_MESSAGE(ids.is_empty(), "choose the relay before any seat");
        relays_through_hub = true;
    }

    void seat_coordinator(int p_id) {
        declare_coordinator(p_id);
        seat(p_id, true);
    }

    void seat_transport_server(int p_id) {
        seat(p_id, true);
    }

    void seat_member(int p_id) {
        seat(p_id, false);
    }

    void wire(int p_a, int p_b) {
        netw::LocalMultiplayerPeer *a = peer_of(p_a);
        netw::LocalMultiplayerPeer *b = peer_of(p_b);
        REQUIRE_MESSAGE(a != nullptr, "wire needs a seated first peer");
        REQUIRE_MESSAGE(b != nullptr, "wire needs a seated second peer");
        a->force_connect_peer(p_b, b);
        b->force_connect_peer(p_a, a);
    }

    netw::LocalMultiplayerPeer *peer_of(int p_id) const {
        const int at = index_of(p_id);
        return at < 0 ? nullptr : peers[at].ptr();
    }

    netw::NetwMultiplayer *session_of(int p_id) const {
        const int at = index_of(p_id);
        return at < 0 ? nullptr : sessions[at].ptr();
    }

    int coordinator_id() const {
        return coordinator;
    }

    void send(int p_from, int p_to, const godot::PackedByteArray &p_bytes) {
        netw::LocalMultiplayerPeer *from = peer_of(p_from);
        REQUIRE_MESSAGE(from != nullptr, "a send needs a seated sender");
        if (from == nullptr) {
            return;
        }
        from->NETW_PEER_VIRTUAL(set_target_peer)(p_to);
        mesh_put(godot::Ref<netw::LocalMultiplayerPeer>(from), p_bytes);
    }

    void send_on(
        int p_from,
        int p_to,
        int p_channel,
        netw::MultiplayerPeerBase::TransferMode p_mode,
        const godot::PackedByteArray &p_bytes
    ) {
        netw::LocalMultiplayerPeer *from = peer_of(p_from);
        REQUIRE_MESSAGE(from != nullptr, "a send needs a seated sender");
        if (from == nullptr) {
            return;
        }
        from->NETW_PEER_VIRTUAL(set_transfer_channel)(p_channel);
        from->NETW_PEER_VIRTUAL(set_transfer_mode)(p_mode);
        send(p_from, p_to, p_bytes);
    }

    godot::Vector<CapturedPacket> capture_at(int p_id) {
        godot::Vector<CapturedPacket> taken;
        netw::LocalMultiplayerPeer *peer = peer_of(p_id);
        if (peer == nullptr) {
            return taken;
        }
        const godot::Ref<netw::LocalMultiplayerPeer> held(peer);
        while (peer->NETW_PEER_VIRTUAL(get_available_packet_count)() > 0) {
            CapturedPacket record;
            record.destination = p_id;
            record.bytes = mesh_take(held);
            record.sender = peer->NETW_PEER_VIRTUAL(get_packet_peer)();
            record.channel = peer->NETW_PEER_VIRTUAL(get_packet_channel)();
            record.mode = int(peer->NETW_PEER_VIRTUAL(get_packet_mode)());
            taken.push_back(record);
        }
        return taken;
    }

    void drain_all() {
        for (int at = 0; at < ids.size(); ++at) {
            capture_at(ids[at]);
        }
    }

    void retire(int p_id) {
        const int at = index_of(p_id);
        if (at < 0) {
            return;
        }
        peers[at]->NETW_PEER_VIRTUAL(close)();
        ids.remove_at(at);
        peers.remove_at(at);
        sessions.remove_at(at);
    }

    void pump(int p_times = 1) {
        for (int round = 0; round < p_times; ++round) {
            link->poll();
            for (const godot::Ref<netw::LocalMultiplayerPeer> &peer : peers) {
                if (peer.is_valid()) {
                    peer->NETW_PEER_VIRTUAL(poll)();
                }
            }
            for (const godot::Ref<netw::NetwMultiplayer> &session : sessions) {
                if (session.is_valid()) {
                    session->poll();
                }
            }
        }
    }

    godot::Ref<netw::NetwPromise> prepare_join(
        int p_id,
        const godot::StringName &p_username,
        const godot::Array &p_args = godot::Array()
    ) {
        netw::NetwMultiplayer *joining = session_of(p_id);
        REQUIRE_MESSAGE(joining != nullptr, "a join needs a seated peer");
        if (joining == nullptr) {
            return godot::Ref<netw::NetwPromise>();
        }
        const godot::Ref<netw::NetwPromise> prepared
            = joining->session_prepare_join(p_username, p_args);
        REQUIRE_MESSAGE(prepared.is_valid(), "the join prepared no promise");
        return prepared;
    }

    godot::Ref<netw::NetwPlayer> submit_join(int p_id, int p_pumps = 8) {
        netw::NetwMultiplayer *joining = session_of(p_id);
        REQUIRE_MESSAGE(joining != nullptr, "a join needs a seated peer");
        netw::NetwMultiplayer *host = session_of(coordinator);
        REQUIRE_MESSAGE(host != nullptr, "a join needs a seated coordinator");
        if (joining == nullptr || host == nullptr) {
            return godot::Ref<netw::NetwPlayer>();
        }
        joining->session_submit_prepared_join();
        for (int round = 0; round < p_pumps; ++round) {
            if (host->player_of(p_id).is_valid()
                && joining->player_local().is_valid()) {
                break;
            }
            pump(1);
        }
        return joining->player_local();
    }

    godot::Ref<netw::NetwPlayer> join_awaiting(
        int p_id,
        const godot::StringName &p_username,
        const godot::Array &p_args = godot::Array(),
        int p_pumps = 8
    ) {
        const godot::Ref<netw::NetwPromise> prepared
            = prepare_join(p_id, p_username, p_args);
        if (prepared.is_null()) {
            return godot::Ref<netw::NetwPlayer>();
        }
        for (int round = 0; round < p_pumps && !prepared->get_is_settled();
             ++round) {
            pump(1);
        }
        REQUIRE_MESSAGE(
            prepared->get_is_settled(),
            "the join preparation did not settle"
        );
        NETW_CHECK_EQ(int(prepared->get_code()), int(godot::OK));
        return submit_join(p_id, p_pumps);
    }

    godot::Ref<netw::NetwPlayer> join(
        int p_id,
        const godot::StringName &p_username,
        const godot::Array &p_args = godot::Array(),
        int p_pumps = 8
    ) {
        const godot::Ref<netw::NetwPromise> prepared
            = prepare_join(p_id, p_username, p_args);
        if (prepared.is_null()) {
            return godot::Ref<netw::NetwPlayer>();
        }
        REQUIRE_MESSAGE(
            prepared->get_is_settled(),
            "the join preparation did not settle"
        );
        NETW_CHECK_EQ(int(prepared->get_code()), int(godot::OK));
        const godot::Ref<netw::NetwPlayer> seated
            = submit_join(p_id, p_pumps);
        netw::NetwMultiplayer *host = session_of(coordinator);
        const bool host_seated_it
            = host != nullptr && host->player_of(p_id).is_valid();
        REQUIRE_MESSAGE(
            host_seated_it,
            "the coordinator seated no player for that peer"
        );
        return seated;
    }

    void step_ticks(int p_ticks) {
        for (int step = 0; step < p_ticks; ++step) {
            for (const godot::Ref<netw::NetwMultiplayer> &session : sessions) {
                if (session.is_valid()) {
                    session->clock_engine().force_step(1);
                }
            }
            link->advance_time(tick_period_ms);
            pump(1);
        }
    }
};

#endif

} // namespace netw_test

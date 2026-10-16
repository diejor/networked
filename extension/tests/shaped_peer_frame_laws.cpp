#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/netw_test.h"

#if defined(DEBUG_ENABLED) && defined(NETW_TIER_HOSTED)

#include <array>
#include <memory>
#include <vector>

#include "godot/net_peers.hpp"
#include "godot/time.hpp"
#include "netw/link/shaped_peer.hpp"
#include "support/link.h"
#include "support/netw_call_log.h"

using namespace godot;

namespace TestShapedPeer {

enum Row { LATENCY, LOSS, DUPLICATE, DISCONNECT, KICK, CLOSE, ROW_COUNT };

struct Receipt {
    int value = 0;
    int channel = 0;
    int peer = 0;
    MultiplayerPeer::TransferMode mode;
    double elapsed_ms = 0.0;
};

struct Evidence {
    bool connected = false;
    bool done = false;
    bool send_ok = true;
    bool close_disconnected = false;
    bool client_disconnected = false;
    int64_t errors = 0;
    int client_id = 0;
    int connects = 0;
    int disconnects = 0;
    std::vector<Receipt> at_host;
    std::vector<Receipt> at_client;
};

std::array<Evidence, ROW_COUNT> &evidence() {
    static std::array<Evidence, ROW_COUNT> rows;
    return rows;
}

class PeerScenario final : public netw_test::FrameScenario {
    int row = 0;
    int phase = 0;
    double opened = 0.0;
    double sent = 0.0;
    Ref<MultiplayerPeer> server_base;
    Ref<MultiplayerPeer> client_base;
    Ref<netw::link::ShapedPeer> server;
    Ref<netw::link::ShapedPeer> client;
    std::unique_ptr<netw_test::CallLog> events;
    std::unique_ptr<netw_test::FaultWatch> faults;
    bool disconnected = false;

    static double now() {
        return double(Time::get_singleton()->get_ticks_usec()) / 1000.0;
    }

    bool open() {
        int port = 0;
        for (int candidate = 30600; candidate < 30700; ++candidate) {
            server_base = netw::gd::new_peer("ENetMultiplayerPeer");
            if (netw::gd::enet_create_server(server_base, candidate, 8) == OK) {
                port = candidate;
                break;
            }
        }
        if (port == 0) {
            return false;
        }
        client_base = netw::gd::new_peer("ENetMultiplayerPeer");
        if (netw::gd::enet_create_client(client_base, "127.0.0.1", port)
            != OK) {
            return false;
        }
        Ref<netw::NetwLinkConditions> host_conditions = netw_test::link(31);
        Ref<netw::NetwLinkConditions> client_conditions = netw_test::link(32);
        if (row == LATENCY) {
            host_conditions->set_latency_ms(40.0);
            client_conditions->set_latency_ms(40.0);
        } else if (row == LOSS) {
            client_conditions->set_loss_chance(1.0);
            client_conditions->set_latency_ms(10.0);
        } else if (row == DUPLICATE) {
            client_conditions->set_duplicate_chance(1.0);
        } else if (row == DISCONNECT || row == KICK) {
            host_conditions->set_latency_ms(100.0);
        } else {
            host_conditions->set_latency_ms(100.0);
            client_conditions->set_latency_ms(100.0);
        }
        server.instantiate();
        client.instantiate();
        server->initialize(server_base, host_conditions);
        client->initialize(client_base, client_conditions);
        events = std::make_unique<netw_test::CallLog>();
        disconnected = false;
        server->connect("peer_connected", events->callable("connected"));
        server->connect("peer_disconnected", events->callable("disconnected"));
        opened = now();
        return true;
    }

    void send(
        const Ref<netw::link::ShapedPeer> &p_peer,
        int p_target,
        int p_value,
        MultiplayerPeer::TransferMode p_mode,
        int p_channel
    ) {
        p_peer->set_target_peer(p_target);
        p_peer->set_transfer_channel(p_channel);
        p_peer->set_transfer_mode(p_mode);
        PackedByteArray bytes;
        bytes.push_back(uint8_t(p_value));
        if (p_peer->put_packet(bytes) != OK) {
            evidence()[row].send_ok = false;
        }
    }

    void read(
        const Ref<netw::link::ShapedPeer> &p_peer,
        std::vector<Receipt> &p_receipts
    ) {
        while (p_peer->get_available_packet_count() > 0) {
            Receipt receipt;
            receipt.peer = p_peer->get_packet_peer();
            receipt.channel = p_peer->get_packet_channel();
            receipt.mode = p_peer->get_packet_mode();
            receipt.elapsed_ms = now() - sent;
            const PackedByteArray bytes = p_peer->get_packet();
            receipt.value = bytes.is_empty() ? -1 : int(bytes[0]);
            p_receipts.push_back(receipt);
        }
    }

    void finish() {
        if (events) {
            evidence()[row].connects = events->count("connected");
            evidence()[row].disconnects = events->count("disconnected");
        }
        evidence()[row].client_disconnected = client_base.is_valid()
            && client_base->get_connection_status()
                == MultiplayerPeer::CONNECTION_DISCONNECTED;
        events.reset();
        if (client.is_valid()) {
            client->close();
        }
        if (server.is_valid()) {
            server->close();
        }
        client.unref();
        server.unref();
        client_base.unref();
        server_base.unref();
        if (faults) {
            evidence()[row].errors = faults->errors();
            faults.reset();
        }
        evidence()[row].done = true;
        ++row;
        phase = 0;
    }

public:
    bool advance() override {
        if (row == ROW_COUNT) {
            return false;
        }
        if (phase == 0) {
            if (!open()) {
                finish();
            } else {
                faults = std::make_unique<netw_test::FaultWatch>();
                phase = 1;
            }
            return true;
        }
        server->poll();
        client->poll();
        Evidence &out = evidence()[row];
        if (phase == 1) {
            if (client->get_connection_status()
                    == MultiplayerPeer::CONNECTION_CONNECTED
                && events->count("connected") > 0) {
                out.connected = true;
                out.client_id = client->get_unique_id();
                sent = now();
                const auto mode = row == LOSS
                    ? MultiplayerPeer::TRANSFER_MODE_RELIABLE
                    : MultiplayerPeer::TRANSFER_MODE_UNRELIABLE;
                const int count = row == LOSS ? 3 : 1;
                for (int i = 0; i < count; ++i) {
                    send(client, 1, i, mode, 2 + i % 2);
                    send(server, out.client_id, i, mode, 2 + i % 2);
                }
                if (row == LOSS) {
                    send(
                        client,
                        1,
                        99,
                        MultiplayerPeer::TRANSFER_MODE_UNRELIABLE,
                        4
                    );
                    send(
                        server,
                        out.client_id,
                        99,
                        MultiplayerPeer::TRANSFER_MODE_UNRELIABLE,
                        4
                    );
                }
                if (row == CLOSE) {
                    server->close();
                    client->close();
                    out.close_disconnected = server->get_connection_status()
                            == MultiplayerPeer::CONNECTION_DISCONNECTED
                        && client->get_connection_status()
                            == MultiplayerPeer::CONNECTION_DISCONNECTED
                        && server_base->get_connection_status()
                            == MultiplayerPeer::CONNECTION_DISCONNECTED
                        && client_base->get_connection_status()
                            == MultiplayerPeer::CONNECTION_DISCONNECTED;
                }
                phase = 2;
            } else if (now() - opened > 3000.0) {
                finish();
            }
            return true;
        }
        read(server, out.at_host);
        read(client, out.at_client);
        if ((row == DISCONNECT || row == KICK) && !disconnected
            && now() - sent > 30.0) {
            disconnected = true;
            if (row == DISCONNECT) {
                client->close();
            } else {
                server->disconnect_peer(out.client_id, true);
            }
        }
        if (now() - sent > 350.0) {
            finish();
        }
        return true;
    }
};

NETW_FRAME_SCENARIO(PeerScenario, shaped_peer_scenario);

TEST_CASE("[Networked][Link][Frame] Latency applies in both directions") {
    const Evidence &out = evidence()[LATENCY];
    NETW_CHECK_EQ(out.errors, 0);
    CHECK(out.done);
    CHECK(out.connected);
    CHECK(out.send_ok);
    NETW_CHECK_EQ(out.connects, 1);
    NETW_CHECK_EQ(out.at_host.size(), 1);
    NETW_CHECK_EQ(out.at_client.size(), 1);
    for (const Receipt &receipt : out.at_host) {
        NETW_CHECK_GE(receipt.elapsed_ms, 80.0);
        NETW_CHECK_EQ(receipt.peer, out.client_id);
        NETW_CHECK_EQ(receipt.channel, 2);
        CHECK(bool(receipt.mode == MultiplayerPeer::TRANSFER_MODE_UNRELIABLE));
    }
    for (const Receipt &receipt : out.at_client) {
        NETW_CHECK_GE(receipt.elapsed_ms, 80.0);
        NETW_CHECK_EQ(receipt.peer, 1);
    }
}

TEST_CASE(
    "[Networked][Link][Frame] Loss drops unreliable and delays reliable"
) {
    const Evidence &out = evidence()[LOSS];
    NETW_CHECK_EQ(out.errors, 0);
    CHECK(out.done);
    CHECK(out.connected);
    CHECK(out.send_ok);
    for (const auto *receipts : {&out.at_host, &out.at_client}) {
        NETW_CHECK_EQ(receipts->size(), 3);
        int next[2] = {0, 1};
        for (const Receipt &receipt : *receipts) {
            CHECK(
                bool(receipt.mode == MultiplayerPeer::TRANSFER_MODE_RELIABLE)
            );
            CHECK(bool(receipt.channel == 2 || receipt.channel == 3));
            if (receipt.channel == 2 || receipt.channel == 3) {
                NETW_CHECK_EQ(receipt.value, next[receipt.channel - 2]);
                next[receipt.channel - 2] += 2;
            }
            NETW_CHECK_GE(receipt.elapsed_ms, 40.0);
        }
    }
}

TEST_CASE("[Networked][Link][Frame] Duplication applies outbound and inbound") {
    const Evidence &out = evidence()[DUPLICATE];
    NETW_CHECK_EQ(out.errors, 0);
    CHECK(out.done);
    CHECK(out.connected);
    CHECK(out.send_ok);
    NETW_CHECK_EQ(out.at_host.size(), 2);
    NETW_CHECK_EQ(out.at_client.size(), 2);
    for (const auto *receipts : {&out.at_host, &out.at_client}) {
        for (const Receipt &receipt : *receipts) {
            NETW_CHECK_EQ(receipt.value, 0);
        }
    }
}

TEST_CASE("[Networked][Link][Frame] Disconnect purges in-flight packets") {
    for (int row : {DISCONNECT, KICK}) {
        const Evidence &out = evidence()[row];
        NETW_CHECK_EQ(out.errors, 0);
        CHECK(out.done);
        CHECK(out.connected);
        CHECK(out.send_ok);
        CHECK(out.client_disconnected);
        NETW_CHECK_EQ(out.at_host.size(), 0);
        NETW_CHECK_EQ(out.at_client.size(), 0);
    }
    NETW_CHECK_EQ(evidence()[DISCONNECT].disconnects, 1);
}

TEST_CASE("[Networked][Link][Frame] Close clears packets and disconnects") {
    const Evidence &out = evidence()[CLOSE];
    NETW_CHECK_EQ(out.errors, 0);
    CHECK(out.done);
    CHECK(out.connected);
    CHECK(out.send_ok);
    CHECK(out.close_disconnected);
    NETW_CHECK_EQ(out.at_host.size(), 0);
    NETW_CHECK_EQ(out.at_client.size(), 0);
}

} // namespace TestShapedPeer

#endif

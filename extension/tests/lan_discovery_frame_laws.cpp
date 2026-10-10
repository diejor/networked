#include "support/frame_drive.h"
#include "support/lan_directory_probe.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include <memory>

#include "godot/class_db.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lan_lobby_directory.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int GAMEPLAY_PORT_START = 30300;
constexpr int DISCOVERY_PORT = 30250;
constexpr int64_t STEP_BUDGET_MSEC = 5000;
constexpr double FRAME = 1.0 / 60.0;

struct DiscoveryEvidence {
    bool driven = false;
    PackedInt32Array hosted_ports;
    PackedStringArray found;
    int rows = 0;
    bool joined = false;
};

DiscoveryEvidence &discovery_evidence() {
    static DiscoveryEvidence evidence;
    return evidence;
}

struct Host {
    Ref<netw::NetwMultiplayer> session;
    netw::LANLobbyDirectory *directory = nullptr;
    int port = 0;
};

class LanDiscoveryScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t step_opened = 0;
    int next_port = GAMEPLAY_PORT_START;
    Host hosts[2];
    Ref<netw::NetwMultiplayer> browser;
    netw::LANLobbyDirectory *finder = nullptr;
    std::unique_ptr<netw_test::CallLog> listed;
    Ref<netw::NetwMultiplayer> joiner;

    static int64_t now_msec() {
        return int64_t(Time::get_singleton()->get_ticks_msec());
    }

    bool over_budget() const {
        return now_msec() - step_opened > STEP_BUDGET_MSEC;
    }

    void enter(int p_step) {
        step = p_step;
        step_opened = now_msec();
    }

    static Ref<MultiplayerPeer> enet_peer() {
        return Ref<MultiplayerPeer>(
            ClassDB::instantiate(StringName("ENetMultiplayerPeer"))
        );
    }

    static netw::LANLobbyDirectory *directory_for(
        const Ref<netw::NetwMultiplayer> &p_session
    ) {
        netw::LANLobbyDirectory *made = memnew(netw::LANLobbyDirectory);
        made->set_port(DISCOVERY_PORT);
        netw::LANLobbyDirectoryProbe::query_loopback(made);
        made->service_entered(p_session.ptr());
        return made;
    }

    bool open_host(Host &r_host) {
        for (; next_port < GAMEPLAY_PORT_START + 100; next_port++) {
            const Ref<MultiplayerPeer> peer = enet_peer();
            if (peer.is_null()) {
                return false;
            }
            if (Error(int(peer->call(StringName("create_server"), next_port)))
                != OK) {
                continue;
            }
            r_host.port = next_port++;
            r_host.session.instantiate();
            r_host.session->session_prepare_join(StringName("host"), Array());
            r_host.session->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
            r_host.directory = directory_for(r_host.session);
            return true;
        }
        return false;
    }

    void pump() {
        for (Host &host : hosts) {
            if (host.session.is_valid()) {
                host.session->NETW_API_VIRTUAL(poll)();
                netw::LANLobbyDirectoryProbe::tick(host.directory, FRAME);
            }
        }
        if (finder != nullptr) {
            browser->connect_plane().on_poll(FRAME);
            netw::LANLobbyDirectoryProbe::tick(finder, FRAME);
        }
        if (joiner.is_valid()) {
            joiner->NETW_API_VIRTUAL(poll)();
        }
    }

    bool joined_host_zero() const {
        const PackedInt32Array seated
            = hosts[0].session->NETW_API_VIRTUAL(get_peer_ids)();
        return joiner->NETW_API_VIRTUAL(get_unique_id)() > 1
            && seated.has(joiner->NETW_API_VIRTUAL(get_unique_id)());
    }

    void finish() {
        DiscoveryEvidence &evidence = discovery_evidence();
        for (Host &host : hosts) {
            evidence.hosted_ports.push_back(host.port);
            if (host.directory != nullptr) {
                host.directory->service_exiting(host.session.ptr());
                memdelete(host.directory);
                host.directory = nullptr;
            }
            if (host.session.is_valid()) {
                host.session->NETW_API_VIRTUAL(set_multiplayer_peer)(
                    Ref<MultiplayerPeer>()
                );
                host.session.unref();
            }
        }
        if (finder != nullptr) {
            evidence.rows = int(browser->endpoint_list().size());
            browser->service_unregister(finder, nullptr);
            finder->service_exiting(browser.ptr());
            memdelete(finder);
            finder = nullptr;
        }
        listed.reset();
        if (joiner.is_valid()) {
            joiner->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
            joiner.unref();
        }
        browser.unref();
        evidence.driven = true;
    }

public:
    bool advance() override {
        DiscoveryEvidence &evidence = discovery_evidence();
        pump();
        switch (step) {
            case 0: {
                if (!open_host(hosts[0]) || !open_host(hosts[1])) {
                    finish();
                    return false;
                }
                enter(1);
                return true;
            }
            case 1: {
                if (hosts[0].session->is_online()
                    && hosts[1].session->is_online()) {
                    browser.instantiate();
                    finder = directory_for(browser);
                    browser->service_register(finder, nullptr);
                    listed = std::make_unique<netw_test::CallLog>();
                    finder->connect(
                        StringName("lobby_list_published"),
                        listed->callable("listed")
                    );
                    enter(2);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                if (over_budget()) {
                    enter(99);
                } else if (now_msec() - step_opened > 1200) {
                    browser->endpoint_set_browsing(true);
                    enter(3);
                }
                return true;
            }
            case 3: {
                if (listed->count(StringName("listed")) > 0) {
                    evidence.found = listed->args(StringName("listed"), 0)[0];
                    const String wanted
                        = String("127.0.0.1:") + itos(hosts[0].port);
                    const Ref<MultiplayerPeer> peer = enet_peer();
                    if (evidence.found.has(wanted)
                        && Error(
                               int(peer->call(
                                   StringName("create_client"),
                                   String("127.0.0.1"),
                                   hosts[0].port
                               ))
                           ) == OK) {
                        joiner.instantiate();
                        joiner->session_prepare_join(
                            StringName("joiner"),
                            Array()
                        );
                        joiner->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
                        enter(4);
                    } else {
                        enter(99);
                    }
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 4: {
                if (joined_host_zero()) {
                    evidence.joined = true;
                    enter(99);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            default: {
                finish();
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(LanDiscoveryScenario, lan_discovery_scenario);

} // namespace

TEST_CASE(
    "[Networked][Connect][Frame] two ENet hosts on one machine are found by "
    "a session browsing over LAN discovery at their own gameplay ports, and a "
    "peer joins one of them by the address it found"
) {
    const DiscoveryEvidence &evidence = discovery_evidence();
    REQUIRE(evidence.driven);
    NETW_REQUIRE_EQ(int(evidence.hosted_ports.size()), 2);
    NETW_CHECK_EQ(int(evidence.found.size()), 2);
    NETW_CHECK_EQ(evidence.rows, 2);
    for (int at = 0; at < evidence.hosted_ports.size(); at++) {
        CHECK(evidence.found.has(
            String("127.0.0.1:") + itos(evidence.hosted_ports[at])
        ));
    }
    CHECK(evidence.joined);
}

} // namespace NetwTests

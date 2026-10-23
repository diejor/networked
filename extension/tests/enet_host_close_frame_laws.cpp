#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int PORT_RANGE_START = 31400;
constexpr int PORT_RANGE_SIZE = 100;
constexpr int64_t SEAT_BUDGET_MSEC = 5000;
constexpr int GIVE_UP_FRAMES = 90;
constexpr int FEW_FRAMES = 4;
constexpr int CLIENTS = 2;

struct ClientRun {
    int exit_reason = -1;
    int host_left_reason = -1;
    int offline_frames = -1;
};

struct HostCloseEvidence {
    bool driven = false;
    bool seated = false;
    ClientRun clients[CLIENTS];
};

HostCloseEvidence &host_close_evidence() {
    static HostCloseEvidence evidence;
    return evidence;
}

void hear_exiting(int64_t p_reason, const String &, int64_t p_client) {
    ClientRun &run = host_close_evidence().clients[p_client];
    if (run.exit_reason < 0) {
        run.exit_reason = int(p_reason);
    }
}

void hear_player_left(
    const Ref<netw::NetwPlayer> &p_player,
    int64_t p_reason,
    int64_t p_client
) {
    if (p_player.is_valid() && p_player->get_peer_id() == 1) {
        host_close_evidence().clients[p_client].host_left_reason
            = int(p_reason);
    }
}

class EnetHostCloseScenario final : public netw_test::FrameScenario {
    int step = 0;
    int frames = 0;
    int64_t opened = 0;
    Ref<netw::NetwMultiplayer> host;
    Ref<MultiplayerPeer> host_link;
    Ref<netw::NetwMultiplayer> clients[CLIENTS];

    static int64_t now_msec() {
        return int64_t(Time::get_singleton()->get_ticks_msec());
    }

    static Ref<MultiplayerPeer> enet_peer() {
        return Ref<MultiplayerPeer>(
            ClassDB::instantiate(StringName("ENetMultiplayerPeer"))
        );
    }

    bool open() {
        for (int port = PORT_RANGE_START;
             port < PORT_RANGE_START + PORT_RANGE_SIZE;
             port++) {
            host_link = enet_peer();
            if (Error(int(host_link->call(StringName("create_server"), port)))
                != OK) {
                continue;
            }
            host.instantiate();
            host->session_prepare_join(StringName("host"), Array());
            host->NETW_API_VIRTUAL(set_multiplayer_peer)(host_link);
            for (int at = 0; at < CLIENTS; at++) {
                const Ref<MultiplayerPeer> link = enet_peer();
                link->call(
                    StringName("create_client"),
                    String("127.0.0.1"),
                    port
                );
                clients[at].instantiate();
                clients[at]->session_prepare_join(
                    StringName(vformat("client%d", at)),
                    Array()
                );
                clients[at]->NETW_API_VIRTUAL(set_multiplayer_peer)(link);
                clients[at]->get_session()->connect(
                    StringName("exiting"),
                    callable_mp_static(&hear_exiting).bind(int64_t(at))
                );
                clients[at]->get_session()->connect(
                    StringName("player_left"),
                    callable_mp_static(&hear_player_left).bind(int64_t(at))
                );
            }
            return true;
        }
        return false;
    }

    bool seated() const {
        bool all = host->player_local().is_valid();
        for (const Ref<netw::NetwMultiplayer> &client : clients) {
            all = all && client->player_local().is_valid()
                && client->get_session()->player_of(1).is_valid();
        }
        return all;
    }

    void pump() {
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(poll)();
        }
        for (const Ref<netw::NetwMultiplayer> &client : clients) {
            if (client.is_valid()) {
                client->NETW_API_VIRTUAL(poll)();
            }
        }
    }

    void quit_host() {
        host->session_leave_on_quit();
        host_link->close();
        host->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        host.unref();
        host_link.unref();
    }

    bool clients_offline() {
        bool all = true;
        for (int at = 0; at < CLIENTS; at++) {
            ClientRun &run = host_close_evidence().clients[at];
            const bool offline = clients[at]->session_get_state()
                == netw::NetwMultiplayer::SESSION_STATE_OFFLINE;
            if (offline && run.offline_frames < 0) {
                run.offline_frames = frames;
            }
            all = all && run.offline_frames >= 0;
        }
        return all;
    }

    bool finish() {
        for (Ref<netw::NetwMultiplayer> &client : clients) {
            if (client.is_valid()) {
                client->NETW_API_VIRTUAL(set_multiplayer_peer)(
                    Ref<MultiplayerPeer>()
                );
            }
            client.unref();
        }
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
        }
        host.unref();
        host_link.unref();
        host_close_evidence().driven = true;
        return false;
    }

public:
    bool advance() override {
        if (step == 0) {
            if (!open()) {
                return finish();
            }
            opened = now_msec();
            step = 1;
            return true;
        }
        pump();
        if (step == 1) {
            if (seated()) {
                host_close_evidence().seated = true;
                quit_host();
                step = 2;
            } else if (now_msec() - opened > SEAT_BUDGET_MSEC) {
                return finish();
            }
            return true;
        }
        frames++;
        if (clients_offline() || frames >= GIVE_UP_FRAMES) {
            return finish();
        }
        return true;
    }
};

NETW_FRAME_SCENARIO(EnetHostCloseScenario, enet_host_close_scenario);

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] HC1 a host that closes its window ends "
    "every client's session within a few frames, with SERVER_SHUTDOWN and "
    "the host reported as LEAVE_QUIT"
) {
    const HostCloseEvidence &evidence = host_close_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int at = 0; at < CLIENTS; at++) {
        const ClientRun &run = evidence.clients[at];
        NETW_FORMAT_INT(client_text, int64_t(at));
        CAPTURE(client_text);
        NETW_CHECK_GE(run.offline_frames, 0);
        NETW_CHECK_LE(run.offline_frames, FEW_FRAMES);
        NETW_CHECK_EQ(run.exit_reason, int(netw::NetwSession::SERVER_SHUTDOWN));
        NETW_CHECK_EQ(run.host_left_reason, int(netw::NetwPlayer::LEAVE_QUIT));
    }
}

} // namespace NetwTests

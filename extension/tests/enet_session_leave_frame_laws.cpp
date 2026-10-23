#include "support/frame_drive.h"
#include "support/netw_test.h"

#include <vector>

#include "godot/class_db.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/session.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int PORT_RANGE_START = 31100;
constexpr int PORT_RANGE_SIZE = 100;
constexpr int64_t STEP_BUDGET_MSEC = 5000;
constexpr int64_t DEAD_SERVER_BUDGET_MSEC = 12000;
constexpr int STARVED_FRAMES = 30;
constexpr int GIVE_UP_FRAMES = 120;

enum Arm {
    ARM_QUIT,
    ARM_STEP_AWAY,
    ARM_QUIT_STARVED,
    ARM_STEP_AWAY_STARVED,
    ARM_KICK,
    ARM_WINDOW_CLOSE,
    ARM_KILLED,
    ARM_DEAD_SERVER,
    ARM_COUNT,
};

enum Seat {
    SEAT_HOST,
    SEAT_WATCHER,
};

struct Heard {
    int seat = 0;
    int peer = 0;
    int reason = -1;
};

struct Run {
    int peer = 0;
    int host_reason = -1;
    int watcher_reason = -1;
    int offline_frames = -1;
    int watcher_frames = -1;
};

struct LeaveEvidence {
    bool driven = false;
    int port = 0;
    Run runs[ARM_COUNT];
    int host_leave_reason = -1;
    std::vector<Heard> heard;
};

LeaveEvidence &leave_evidence() {
    static LeaveEvidence evidence;
    return evidence;
}

void hear_player_left(
    const Ref<netw::NetwPlayer> &p_player,
    int64_t p_reason,
    int64_t p_seat
) {
    if (p_player.is_valid()) {
        leave_evidence().heard.push_back(
            {int(p_seat), int(p_player->get_peer_id()), int(p_reason)}
        );
    }
}

int heard_reason(int p_seat, int p_peer) {
    for (const Heard &row : leave_evidence().heard) {
        if (row.seat == p_seat && row.peer == p_peer) {
            return row.reason;
        }
    }
    return -1;
}

class EnetLeaveScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t step_opened = 0;
    int arm = 0;
    int frames = 0;
    bool host_paused = false;
    bool leaver_paused = false;
    Ref<netw::NetwMultiplayer> host;
    Ref<netw::NetwMultiplayer> watcher;
    Ref<netw::NetwMultiplayer> leaver;
    Ref<MultiplayerPeer> leaver_link;

    static int64_t now_msec() {
        return int64_t(Time::get_singleton()->get_ticks_msec());
    }

    bool over_budget() const {
        const int64_t budget = arm == ARM_DEAD_SERVER ? DEAD_SERVER_BUDGET_MSEC
                                                      : STEP_BUDGET_MSEC;
        return now_msec() - step_opened > budget;
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

    static void listen(
        const Ref<netw::NetwMultiplayer> &p_session,
        Seat p_seat
    ) {
        p_session->get_session()->connect(
            StringName("player_left"),
            callable_mp_static(&hear_player_left).bind(int64_t(p_seat))
        );
    }

    int open_host() {
        for (int candidate = PORT_RANGE_START;
             candidate < PORT_RANGE_START + PORT_RANGE_SIZE;
             candidate++) {
            const Ref<MultiplayerPeer> peer = enet_peer();
            if (peer.is_null()) {
                return 0;
            }
            if (Error(int(peer->call(StringName("create_server"), candidate)))
                != OK) {
                continue;
            }
            host.instantiate();
            host->session_prepare_join(StringName("host"), Array());
            host->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
            listen(host, SEAT_HOST);
            return candidate;
        }
        return 0;
    }

    Ref<netw::NetwMultiplayer> open_client(
        const StringName &p_name,
        int p_port,
        Ref<MultiplayerPeer> &r_link
    ) {
        r_link = enet_peer();
        if (r_link.is_null()
            || Error(
                   int(r_link->call(
                       StringName("create_client"),
                       String("127.0.0.1"),
                       p_port
                   ))
               ) != OK) {
            return Ref<netw::NetwMultiplayer>();
        }
        Ref<netw::NetwMultiplayer> joined;
        joined.instantiate();
        joined->session_prepare_join(p_name, Array());
        joined->NETW_API_VIRTUAL(set_multiplayer_peer)(r_link);
        return joined;
    }

    void pump() {
        if (host.is_valid() && !host_paused) {
            host->NETW_API_VIRTUAL(poll)();
        }
        if (watcher.is_valid()) {
            watcher->NETW_API_VIRTUAL(poll)();
        }
        if (leaver.is_valid() && !leaver_paused) {
            leaver->NETW_API_VIRTUAL(poll)();
        }
    }

    static void close(const Ref<netw::NetwMultiplayer> &p_session) {
        if (p_session.is_valid()) {
            p_session->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
        }
    }

    static bool seated(const Ref<netw::NetwMultiplayer> &p_session) {
        return p_session.is_valid() && p_session->player_local().is_valid();
    }

    bool leaver_offline() const {
        return leaver.is_valid()
            && leaver->session_get_state()
            == netw::NetwMultiplayer::SESSION_STATE_OFFLINE;
    }

    void depart(Run &r_run) {
        switch (arm) {
            case ARM_QUIT:
                leaver->session_leave();
                break;
            case ARM_STEP_AWAY:
                leaver->session_step_away();
                break;
            case ARM_QUIT_STARVED:
                host_paused = true;
                leaver->session_leave();
                break;
            case ARM_STEP_AWAY_STARVED:
                host_paused = true;
                leaver->session_step_away();
                break;
            case ARM_KICK:
                host->get_session()->kick(
                    host->get_session()->player_of(r_run.peer),
                    String("gate")
                );
                break;
            case ARM_WINDOW_CLOSE:
                leaver->session_leave_on_quit();
                leaver_link->close();
                leaver_paused = true;
                break;
            case ARM_KILLED:
                leaver_link->close();
                leaver_paused = true;
                break;
            case ARM_DEAD_SERVER:
                host_paused = true;
                leaver->session_leave();
                break;
            default:
                break;
        }
    }

    bool settled(Run &r_run) {
        if (r_run.offline_frames < 0 && leaver_offline()) {
            r_run.offline_frames = frames;
        }
        if ((arm == ARM_QUIT_STARVED || arm == ARM_STEP_AWAY_STARVED)
            && frames >= STARVED_FRAMES) {
            host_paused = false;
        }
        if (arm == ARM_DEAD_SERVER && r_run.offline_frames >= 0) {
            host_paused = false;
        }
        r_run.host_reason = heard_reason(SEAT_HOST, r_run.peer);
        r_run.watcher_reason = heard_reason(SEAT_WATCHER, r_run.peer);
        if (r_run.watcher_frames < 0 && r_run.watcher_reason >= 0) {
            r_run.watcher_frames = frames;
        }
        return r_run.host_reason >= 0 && r_run.watcher_reason >= 0
            && (r_run.offline_frames >= 0 || arm == ARM_KILLED
                || arm == ARM_WINDOW_CLOSE);
    }

    void drop_leaver() {
        close(leaver);
        leaver.unref();
        leaver_link.unref();
        host_paused = false;
        leaver_paused = false;
    }

public:
    bool advance() override {
        LeaveEvidence &evidence = leave_evidence();
        pump();

        switch (step) {
            case 0: {
                evidence.port = open_host();
                if (evidence.port == 0) {
                    evidence.driven = true;
                    return false;
                }
                enter(1);
                return true;
            }
            case 1: {
                if (seated(host)) {
                    Ref<MultiplayerPeer> link;
                    watcher = open_client(
                        StringName("watcher"),
                        evidence.port,
                        link
                    );
                    if (watcher.is_null()) {
                        enter(99);
                        return true;
                    }
                    listen(watcher, SEAT_WATCHER);
                    enter(2);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                if (seated(watcher)) {
                    enter(3);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 3: {
                if (arm >= ARM_COUNT) {
                    host->session_leave();
                    enter(6);
                    return true;
                }
                leaver = open_client(
                    StringName("leaver"),
                    evidence.port,
                    leaver_link
                );
                enter(leaver.is_valid() ? 4 : 99);
                return true;
            }
            case 4: {
                const int peer = leaver->NETW_API_VIRTUAL(get_unique_id)();
                const bool known = seated(leaver)
                    && host->get_session()->player_of(peer).is_valid()
                    && watcher->get_session()->player_of(peer).is_valid();
                if (known) {
                    Run &run = evidence.runs[arm];
                    run.peer = peer;
                    frames = 0;
                    depart(run);
                    enter(5);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 5: {
                frames++;
                if (settled(evidence.runs[arm]) || over_budget()) {
                    drop_leaver();
                    arm++;
                    enter(3);
                }
                return true;
            }
            case 6: {
                evidence.host_leave_reason = heard_reason(SEAT_WATCHER, 1);
                if (evidence.host_leave_reason >= 0 || over_budget()) {
                    enter(99);
                }
                return true;
            }
            default: {
                drop_leaver();
                close(watcher);
                close(host);
                watcher.unref();
                host.unref();
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(EnetLeaveScenario, enet_leave_scenario);

const Run &run_of(Arm p_arm) {
    return leave_evidence().runs[p_arm];
}

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] SL1 a player who leaves or steps away is "
    "reported with that reason on the server and on every other client"
) {
    REQUIRE(leave_evidence().driven);
    const struct {
        Arm arm;
        netw::NetwPlayer::LeaveReason reason;
    } rows[] = {
        {ARM_QUIT, netw::NetwPlayer::LEAVE_QUIT},
        {ARM_STEP_AWAY, netw::NetwPlayer::LEAVE_STEPPED_AWAY},
        {ARM_QUIT_STARVED, netw::NetwPlayer::LEAVE_QUIT},
        {ARM_STEP_AWAY_STARVED, netw::NetwPlayer::LEAVE_STEPPED_AWAY},
        {ARM_KICK, netw::NetwPlayer::LEAVE_KICKED},
        {ARM_KILLED, netw::NetwPlayer::LEAVE_DROPPED},
    };
    for (const auto &row : rows) {
        NETW_FORMAT_INT(arm_text, int64_t(row.arm));
        CAPTURE(arm_text);
        NETW_CHECK_EQ(run_of(row.arm).host_reason, int(row.reason));
        NETW_CHECK_EQ(run_of(row.arm).watcher_reason, int(row.reason));
    }
}

TEST_CASE(
    "[Networked][Session][Frame] SL2 the server closes a leaving client's "
    "connection, so leave settles before the client would give up"
) {
    REQUIRE(leave_evidence().driven);
    for (const Arm arm : {ARM_QUIT, ARM_STEP_AWAY}) {
        NETW_FORMAT_INT(arm_text, int64_t(arm));
        CAPTURE(arm_text);
        NETW_CHECK_GE(run_of(arm).offline_frames, 0);
        NETW_CHECK_LT(run_of(arm).offline_frames, GIVE_UP_FRAMES / 2);
    }
}

TEST_CASE(
    "[Networked][Session][Frame] SL3 a client leaving a server that never "
    "answers goes offline after the give-up, long before the connection "
    "times out"
) {
    REQUIRE(leave_evidence().driven);
    const Run &run = run_of(ARM_DEAD_SERVER);
    NETW_CHECK_GE(run.offline_frames, GIVE_UP_FRAMES - 2);
    NETW_CHECK_LE(run.offline_frames, GIVE_UP_FRAMES + 10);
    NETW_CHECK_EQ(run.host_reason, int(netw::NetwPlayer::LEAVE_QUIT));
}

TEST_CASE(
    "[Networked][Session][Frame] SL4 a client that closes its window reads "
    "LEAVE_QUIT on the server, and other clients read it without waiting "
    "for the connection to time out"
) {
    REQUIRE(leave_evidence().driven);
    const Run &run = run_of(ARM_WINDOW_CLOSE);
    NETW_CHECK_EQ(run.host_reason, int(netw::NetwPlayer::LEAVE_QUIT));
    NETW_CHECK_EQ(run.watcher_reason, int(netw::NetwPlayer::LEAVE_QUIT));
    NETW_CHECK_GE(run.watcher_frames, 0);
    NETW_CHECK_LT(run.watcher_frames, GIVE_UP_FRAMES / 2);
}

TEST_CASE(
    "[Networked][Session][Frame] SL6 every peer publishes player_left once "
    "for each player who leaves"
) {
    REQUIRE(leave_evidence().driven);
    const std::vector<Heard> &heard = leave_evidence().heard;
    NETW_CHECK_GT(int(heard.size()), 0);
    for (size_t at = 0; at < heard.size(); ++at) {
        for (size_t later = at + 1; later < heard.size(); ++later) {
            const bool twice = heard[at].seat == heard[later].seat
                && heard[at].peer == heard[later].peer;
            CHECK_FALSE(twice);
        }
    }
}

TEST_CASE(
    "[Networked][Session][Frame] SL5 a server that leaves is reported to its "
    "clients as LEAVE_QUIT"
) {
    REQUIRE(leave_evidence().driven);
    NETW_CHECK_EQ(
        leave_evidence().host_leave_reason,
        int(netw::NetwPlayer::LEAVE_QUIT)
    );
}

} // namespace NetwTests

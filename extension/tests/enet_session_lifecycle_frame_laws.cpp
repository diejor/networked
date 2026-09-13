#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int PORT_RANGE_START = 30100;
constexpr int PORT_RANGE_SIZE = 100;
constexpr int64_t STEP_BUDGET_MSEC = 5000;

struct LifecycleEvidence {
    bool driven = false;
    int port = 0;
    bool host_online = false;
    bool host_scene_free = false;
    bool host_admitted_itself = false;
    int kicked_id = 0;
    bool kicked_seated = false;
    bool kicked_gone = false;
    int leaving_id = 0;
    bool leaving_seated = false;
    bool leaving_offline = false;
    bool leaving_gone = false;
};

LifecycleEvidence &lifecycle_evidence() {
    static LifecycleEvidence evidence;
    return evidence;
}

bool seats(const Ref<netw::NetwMultiplayer> &p_session, int p_peer) {
    const PackedInt32Array seated = p_session->NETW_API_VIRTUAL(get_peer_ids)();
    for (int at = 0; at < seated.size(); at++) {
        if (seated[at] == p_peer) {
            return true;
        }
    }
    return false;
}

class EnetLifecycleScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t step_opened = 0;
    Ref<netw::NetwMultiplayer> host;
    Ref<netw::NetwMultiplayer> kicked;
    Ref<netw::NetwMultiplayer> leaving;

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

    int open_host() {
        for (int candidate = PORT_RANGE_START;
             candidate < PORT_RANGE_START + PORT_RANGE_SIZE;
             candidate++) {
            const Ref<MultiplayerPeer> peer = enet_peer();
            if (peer.is_null()) {
                return 0;
            }
            const Error bound = Error(
                int(peer->call(StringName("create_server"), candidate))
            );
            if (bound != OK) {
                continue;
            }
            host.instantiate();
            host->session_prepare_join(StringName("host"), Array());
            host->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
            return candidate;
        }
        return 0;
    }

    Ref<netw::NetwMultiplayer> open_client(
        const StringName &p_name,
        int p_port
    ) {
        const Ref<MultiplayerPeer> peer = enet_peer();
        if (peer.is_null()) {
            return Ref<netw::NetwMultiplayer>();
        }
        const Error reached = Error(
            int(peer->call(
                StringName("create_client"),
                String("127.0.0.1"),
                p_port
            ))
        );
        if (reached != OK) {
            return Ref<netw::NetwMultiplayer>();
        }
        Ref<netw::NetwMultiplayer> joined;
        joined.instantiate();
        joined->session_prepare_join(p_name, Array());
        joined->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
        return joined;
    }

    void pump() {
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(poll)();
        }
        if (kicked.is_valid()) {
            kicked->NETW_API_VIRTUAL(poll)();
        }
        if (leaving.is_valid()) {
            leaving->NETW_API_VIRTUAL(poll)();
        }
    }

    void close(const Ref<netw::NetwMultiplayer> &p_session) {
        if (p_session.is_valid()) {
            p_session->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
        }
    }

public:
    bool advance() override {
        LifecycleEvidence &evidence = lifecycle_evidence();
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
                if (host->is_online()) {
                    evidence.host_online = true;
                    evidence.host_scene_free = host->scene_list().is_empty();
                    kicked = open_client(StringName("kicked"), evidence.port);
                    enter(2);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                if (kicked.is_null()) {
                    enter(99);
                    return true;
                }
                if (host->participant_local().is_valid()) {
                    evidence.host_admitted_itself = true;
                }
                const int seated = kicked->NETW_API_VIRTUAL(get_unique_id)();
                if (seated > 1 && seats(host, seated)) {
                    evidence.kicked_id = seated;
                    evidence.kicked_seated = true;
                    host->peer_kick(seated, String("gate"));
                    enter(3);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 3: {
                if (!seats(host, evidence.kicked_id)) {
                    evidence.kicked_gone = true;
                    close(kicked);
                    kicked.unref();
                    leaving = open_client(StringName("leaving"), evidence.port);
                    enter(4);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 4: {
                if (leaving.is_null()) {
                    enter(99);
                    return true;
                }
                const int seated = leaving->NETW_API_VIRTUAL(get_unique_id)();
                if (seated > 1 && seats(host, seated)) {
                    evidence.leaving_id = seated;
                    evidence.leaving_seated = true;
                    leaving->session_leave();
                    enter(5);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 5: {
                const bool offline = leaving->session_get_state()
                    == netw::NetwMultiplayer::SESSION_STATE_OFFLINE;
                if (offline) {
                    evidence.leaving_offline = true;
                }
                if (offline && !seats(host, evidence.leaving_id)) {
                    evidence.leaving_gone = true;
                    enter(99);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            default: {
                close(leaving);
                close(kicked);
                close(host);
                leaving.unref();
                kicked.unref();
                host.unref();
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(EnetLifecycleScenario, enet_lifecycle_scenario);

} // namespace

TEST_CASE(
    "[Networked][Connect][Frame] EL1 a session handed an ENet server peer "
    "comes online over a real socket, so hosting needs no owning node and no "
    "connector, only a prepared player and an assigned peer"
) {
    const LifecycleEvidence &evidence = lifecycle_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GT(evidence.port, 0);
    CHECK(evidence.host_online);
}

TEST_CASE(
    "[Networked][Connect][Frame] EL6 a host that brings up no scene is still "
    "admitted to its own roster, because the startup announcement its join "
    "waits on is queued after the join installs its listener rather than "
    "raised inside session entry"
) {
    const LifecycleEvidence &evidence = lifecycle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.host_online);
    REQUIRE(evidence.host_scene_free);
    CHECK(evidence.host_admitted_itself);
}

TEST_CASE(
    "[Networked][Connect][Frame] EL2 a client that assigned an ENet client "
    "peer is seated in the host roster, which is what a join is, rather than "
    "an outcome a connector reports"
) {
    const LifecycleEvidence &evidence = lifecycle_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.kicked_seated);
    NETW_CHECK_GT(evidence.kicked_id, 1);
}

TEST_CASE(
    "[Networked][Connect][Frame] EL3 a kicked peer leaves the host roster, so "
    "the kick reaches the socket rather than only the session's own book"
) {
    const LifecycleEvidence &evidence = lifecycle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.kicked_seated);
    CHECK(evidence.kicked_gone);
}

TEST_CASE(
    "[Networked][Connect][Frame] EL4 the host admits a second peer after a "
    "kick, so a kick closes one connection rather than the listener"
) {
    const LifecycleEvidence &evidence = lifecycle_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.leaving_seated);
}

TEST_CASE(
    "[Networked][Connect][Frame] EL5 a peer that leaves of its own accord "
    "reads offline on its own side and is gone from the host roster, so the "
    "two sides agree without either being told twice"
) {
    const LifecycleEvidence &evidence = lifecycle_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.leaving_seated);
    CHECK(evidence.leaving_offline);
    CHECK(evidence.leaving_gone);
}

} // namespace NetwTests

#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/minted_script.h"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int PORT_RANGE_START = 30300;
constexpr int PORT_RANGE_SIZE = 100;
constexpr int64_t STEP_BUDGET_MSEC = 5000;

struct RefusalEvidence {
    bool driven = false;
    int port = 0;
    bool host_online = false;
    bool host_seated_itself = false;
    bool client_reached = false;
    bool client_told = false;
    int64_t client_error = OK;
    String client_reason;
    bool host_forgot_client = false;
    bool host_still_seats = false;
    int client_end_state = -1;
};

RefusalEvidence &refusal_evidence() {
    static RefusalEvidence evidence;
    return evidence;
}

void record_join_failed(int64_t p_error, const String &p_reason) {
    RefusalEvidence &evidence = refusal_evidence();
    evidence.client_told = true;
    evidence.client_error = p_error;
    evidence.client_reason = p_reason;
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

class EnetJoinRefusalScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t step_opened = 0;
    int client_id = 0;
    Ref<netw::NetwMultiplayer> host;
    Ref<netw::NetwMultiplayer> client;
    Ref<RefCounted> policy;

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

    bool arm_refusing_policy() {
        const Ref<Script> shape = netw_test::minted_script(
            "extends RefCounted\n"
            "func seat(_rj) -> String:\n"
            "\treturn \"not a placement\"\n"
        );
        if (shape.is_null()) {
            return false;
        }
        policy = shape->call("new");
        if (policy.is_null()) {
            return false;
        }
        host->session_set_join_override(
            Callable(policy.ptr(), StringName("seat")),
            Array()
        );
        return true;
    }

    bool open_client(int p_port) {
        const Ref<MultiplayerPeer> peer = enet_peer();
        if (peer.is_null()) {
            return false;
        }
        const Error reached = Error(
            int(peer->call(
                StringName("create_client"),
                String("127.0.0.1"),
                p_port
            ))
        );
        if (reached != OK) {
            return false;
        }
        client.instantiate();
        client->connect(
            StringName("session_join_failed"),
            callable_mp_static(&record_join_failed)
        );
        client->session_prepare_join(StringName("turned_away"), Array());
        client->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
        return true;
    }

    void pump() {
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(poll)();
        }
        if (client.is_valid()) {
            client->NETW_API_VIRTUAL(poll)();
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
        RefusalEvidence &evidence = refusal_evidence();
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
                if (host->is_online() && host->participant_local().is_valid()) {
                    evidence.host_online = true;
                    evidence.host_seated_itself = true;
                    if (!arm_refusing_policy() || !open_client(evidence.port)) {
                        enter(99);
                        return true;
                    }
                    enter(2);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                const int seated = client->NETW_API_VIRTUAL(get_unique_id)();
                if (seated > 1) {
                    evidence.client_reached = true;
                    client_id = seated;
                    enter(3);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 3: {
                if (evidence.client_told || over_budget()) {
                    evidence.host_forgot_client
                        = host->peer_get_accepted_join(client_id).is_null();
                    enter(4);
                }
                return true;
            }
            case 4: {
                const bool settled = client->session_get_state()
                    != netw::NetwMultiplayer::SESSION_STATE_ONLINE;
                if (settled || over_budget()) {
                    evidence.client_end_state
                        = int(client->session_get_state());
                    enter(5);
                }
                return true;
            }
            case 5: {
                evidence.host_still_seats = seats(host, client_id);
                if (!evidence.host_still_seats || over_budget()) {
                    enter(99);
                }
                return true;
            }
            default: {
                close(client);
                close(host);
                client.unref();
                host.unref();
                policy.unref();
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(EnetJoinRefusalScenario, enet_join_refusal_scenario);

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] JR1 a join a server refuses reaches the "
    "peer that asked for it, over a real ENet socket, so the reason rides "
    "the reliable channel ahead of the disconnect rather than being lost to "
    "it"
) {
    const RefusalEvidence &evidence = refusal_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GT(evidence.port, 0);
    CHECK(evidence.host_online);
    CHECK(evidence.client_reached);
    CHECK(evidence.client_told);
    CHECK_FALSE(evidence.client_reason.is_empty());
    NETW_CHECK_EQ(evidence.client_error, int64_t(ERR_UNAUTHORIZED));
}

TEST_CASE(
    "[Networked][Session][Frame] JR2 a handler answering something that is "
    "not a placement seats nobody, so the peer is turned away rather than "
    "left holding a session it is not in"
) {
    const RefusalEvidence &evidence = refusal_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.client_reached);
    CHECK(evidence.host_forgot_client);
    CHECK_FALSE(evidence.host_still_seats);
    NETW_CHECK_EQ(
        evidence.client_end_state,
        int(netw::NetwMultiplayer::SESSION_STATE_OFFLINE)
    );
}

TEST_CASE(
    "[Networked][Session][Frame] JR3 a failed placement on one peer never "
    "disconnects the server from itself, because the host's own admission "
    "stands and only the refused peer is turned away"
) {
    const RefusalEvidence &evidence = refusal_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.host_seated_itself);
}

} // namespace NetwTests

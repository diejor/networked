#include "support/frame_drive.h"
#include "support/netw_test.h"

#include <vector>

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/time.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/connect/creation.hpp"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int PORT_RANGE_START = 30500;
constexpr int PORT_RANGE_SIZE = 40;
constexpr int CLOSED_PORT_OFFSET = 50;
constexpr int64_t STEP_BUDGET_MSEC = 5000;
constexpr int SETTLE_FRAMES = 20;

enum Side {
    SIDE_HOST,
    SIDE_ASSIGNED,
    SIDE_CLOSED,
    SIDE_REFUSED,
    SIDE_COUNT,
};

struct SideEvidence {
    std::vector<int64_t> states;
    std::vector<int64_t> walked;
    int failed = 0;
    int64_t failed_error = OK;
    bool promise_completed = false;
    String address_when_settled;
    int end_session_state = -1;
};

struct AttemptEvidence {
    bool driven = false;
    int port = 0;
    SideEvidence sides[SIDE_COUNT];
};

AttemptEvidence &attempt_evidence() {
    static AttemptEvidence evidence;
    return evidence;
}

void record_state(int64_t p_state, int64_t p_side) {
    attempt_evidence().sides[p_side].states.push_back(p_state);
}

void record_failed(int64_t p_error, String, int64_t p_side) {
    SideEvidence &side = attempt_evidence().sides[p_side];
    side.failed += 1;
    side.failed_error = p_error;
}

void record_settled_address(Variant, uint64_t p_session) {
    netw::NetwMultiplayer *session = Object::cast_to<netw::NetwMultiplayer>(
        netw::gd::object_of(ObjectID(p_session))
    );
    if (session != nullptr) {
        attempt_evidence().sides[SIDE_HOST].address_when_settled
            = session->peer_join_address();
    }
}

class ConnectAttemptScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t step_opened = 0;
    int candidate = PORT_RANGE_START;
    int settle = 0;
    Ref<netw::NetwMultiplayer> host;
    Ref<netw::NetwMultiplayer> client;
    Ref<netw::NetwPromise> hosting;

    static int64_t now_msec() {
        return int64_t(Time::get_singleton()->get_ticks_msec());
    }

    bool over_budget() const {
        return now_msec() - step_opened > STEP_BUDGET_MSEC;
    }

    void enter(int p_step) {
        step = p_step;
        step_opened = now_msec();
        settle = 0;
    }

    static Ref<netw::NetwMultiplayer> watched(Side p_side) {
        Ref<netw::NetwMultiplayer> made;
        made.instantiate();
        made->observe(
            netw::NetwMultiplayer::CONNECTION_STATE_CHANGED,
            callable_mp_static(&record_state).bind(int64_t(p_side))
        );
        made->observe(
            netw::NetwMultiplayer::CONNECTION_FAILED,
            callable_mp_static(&record_failed).bind(int64_t(p_side))
        );
        return made;
    }

    static RID enet(const Ref<netw::NetwMultiplayer> &p_session) {
        return p_session->transport_find(StringName("ENetMultiplayerPeer"));
    }

    void try_host() {
        attempt_evidence().sides[SIDE_HOST] = SideEvidence();
        host = watched(SIDE_HOST);
        host->session_prepare_join(StringName("host"), Array());
        Dictionary settings;
        settings["port"] = candidate;
        hosting = host->transport_host(enet(host), settings);
        hosting->then(
            callable_mp_static(&record_settled_address)
                .bind(uint64_t(netw::gd::instance_id(host.ptr())))
        );
    }

    void join(Side p_side, int p_port) {
        client = watched(p_side);
        client->session_prepare_join(StringName("client"), Array());
        client->transport_join(
            enet(client),
            String("127.0.0.1:") + itos(p_port),
            Dictionary()
        );
    }

    void close(Ref<netw::NetwMultiplayer> &r_session) {
        if (r_session.is_valid()) {
            r_session->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
            r_session->embed_dispose();
            r_session.unref();
        }
    }

    void finish_client(Side p_side) {
        SideEvidence &side = attempt_evidence().sides[p_side];
        side.end_session_state = int(client->session_get_state());
        side.walked = side.states;
        close(client);
    }

    void shorten_enet_timeout() {
        const Ref<MultiplayerPeer> peer
            = client->NETW_API_VIRTUAL(get_multiplayer_peer)();
        if (peer.is_null()) {
            return;
        }
        Object *link = peer->call(StringName("get_peer"), 1);
        if (link != nullptr) {
            link->call(StringName("set_timeout"), 32, 200, 600);
        }
    }

    bool connecting() const {
        return client->transport_get_attempt_state()
            == int64_t(netw::connect::ATTEMPT_CONNECTING);
    }

public:
    bool advance() override {
        AttemptEvidence &evidence = attempt_evidence();
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(poll)();
        }
        if (client.is_valid()) {
            client->NETW_API_VIRTUAL(poll)();
        }

        switch (step) {
            case 0: {
                try_host();
                enter(1);
                return true;
            }
            case 1: {
                if (hosting->get_is_completed()) {
                    evidence.port = candidate;
                    evidence.sides[SIDE_HOST].promise_completed = true;
                    evidence.sides[SIDE_HOST].walked
                        = evidence.sides[SIDE_HOST].states;
                    client = watched(SIDE_ASSIGNED);
                    const Ref<MultiplayerPeer> peer(
                        ClassDB::instantiate(StringName("ENetMultiplayerPeer"))
                    );
                    peer->call(
                        StringName("create_client"),
                        String("127.0.0.1"),
                        candidate
                    );
                    client->session_prepare_join(StringName("own"), Array());
                    client->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
                    enter(2);
                } else if (hosting->get_is_failed()
                           && candidate + 1
                               < PORT_RANGE_START + PORT_RANGE_SIZE) {
                    close(host);
                    candidate += 1;
                    enter(0);
                } else if (hosting->get_is_failed() || over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                if (client->is_online() || over_budget()) {
                    finish_client(SIDE_ASSIGNED);
                    join(SIDE_CLOSED, evidence.port + CLOSED_PORT_OFFSET);
                    enter(3);
                }
                return true;
            }
            case 3: {
                if (connecting()) {
                    shorten_enet_timeout();
                    enter(4);
                } else if (over_budget()) {
                    enter(4);
                }
                return true;
            }
            case 4: {
                const bool settled = evidence.sides[SIDE_CLOSED].failed > 0
                    || over_budget();
                if (settled && ++settle >= SETTLE_FRAMES) {
                    finish_client(SIDE_CLOSED);
                    join(SIDE_REFUSED, evidence.port);
                    enter(5);
                }
                return true;
            }
            case 5: {
                if (connecting()) {
                    client->auth_set_app_tag(host->auth_app_tag_of() + 1);
                    enter(6);
                } else if (over_budget()) {
                    enter(6);
                }
                return true;
            }
            case 6: {
                const bool settled = evidence.sides[SIDE_REFUSED].failed > 0
                    || over_budget();
                if (settled && ++settle >= SETTLE_FRAMES) {
                    finish_client(SIDE_REFUSED);
                    enter(99);
                }
                return true;
            }
            default: {
                close(client);
                close(host);
                hosting.unref();
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(ConnectAttemptScenario, connect_attempt_scenario);

void check_states(
    const SideEvidence &p_side,
    const std::vector<int64_t> &p_want
) {
    NETW_REQUIRE_EQ(int(p_side.walked.size()), int(p_want.size()));
    for (size_t at = 0; at < p_want.size(); at++) {
        NETW_CHECK_EQ(p_side.walked[at], p_want[at]);
    }
}

} // namespace

TEST_CASE(
    "[Networked][Connect][Frame] AT1 hosting over ENet walks the attempt "
    "through creating and connecting to online, in order, and resolves its "
    "promise"
) {
    const AttemptEvidence &evidence = attempt_evidence();
    REQUIRE(evidence.driven);
    NETW_CHECK_GT(evidence.port, 0);
    const SideEvidence &side = evidence.sides[SIDE_HOST];
    CHECK(side.promise_completed);
    CHECK_FALSE(side.address_when_settled.is_empty());
    NETW_CHECK_EQ(side.failed, 0);
    check_states(
        side,
        {netw::connect::ATTEMPT_CREATING,
         netw::connect::ATTEMPT_CONNECTING,
         netw::connect::ATTEMPT_ONLINE}
    );
}

TEST_CASE(
    "[Networked][Connect][Frame] AT5 a peer the game assigns itself walks "
    "the attempt through connecting to online with no verb"
) {
    const AttemptEvidence &evidence = attempt_evidence();
    REQUIRE(evidence.driven);
    const SideEvidence &side = evidence.sides[SIDE_ASSIGNED];
    NETW_CHECK_EQ(side.failed, 0);
    NETW_CHECK_EQ(
        side.end_session_state,
        int(netw::NetwMultiplayer::SESSION_STATE_ONLINE)
    );
    check_states(
        side,
        {netw::connect::ATTEMPT_CONNECTING, netw::connect::ATTEMPT_ONLINE}
    );
}

TEST_CASE(
    "[Networked][Connect][Frame] AT2 a join to a closed port fails once with "
    "ERR_CANT_CONNECT and ends offline"
) {
    const AttemptEvidence &evidence = attempt_evidence();
    REQUIRE(evidence.driven);
    const SideEvidence &side = evidence.sides[SIDE_CLOSED];
    NETW_CHECK_EQ(side.failed, 1);
    NETW_CHECK_EQ(side.failed_error, int64_t(ERR_CANT_CONNECT));
    check_states(
        side,
        {netw::connect::ATTEMPT_CREATING,
         netw::connect::ATTEMPT_CONNECTING,
         netw::connect::ATTEMPT_OFFLINE}
    );
}

TEST_CASE(
    "[Networked][Connect][Frame] a join the server refuses during "
    "authentication fails once with ERR_CANT_CONNECT and leaves the session "
    "offline rather than connecting forever"
) {
    const AttemptEvidence &evidence = attempt_evidence();
    REQUIRE(evidence.driven);
    const SideEvidence &side = evidence.sides[SIDE_REFUSED];
    NETW_CHECK_EQ(side.failed, 1);
    NETW_CHECK_EQ(side.failed_error, int64_t(ERR_CANT_CONNECT));
    NETW_CHECK_EQ(
        side.end_session_state,
        int(netw::NetwMultiplayer::SESSION_STATE_OFFLINE)
    );
    check_states(
        side,
        {netw::connect::ATTEMPT_CREATING,
         netw::connect::ATTEMPT_CONNECTING,
         netw::connect::ATTEMPT_OFFLINE}
    );
}

} // namespace NetwTests

#include "support/deaf_peer.h"
#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include <memory>

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/api/admission_config.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"
#include "support/minted_script.h"

using namespace godot;

namespace NetwTests {

namespace {

constexpr int PORT_RANGE_START = 30300;
constexpr int PORT_RANGE_SIZE = 100;
constexpr int64_t STEP_BUDGET_MSEC = 5000;
constexpr int SETTLE_POLLS = 10;

struct RefusalEvidence {
    bool driven = false;
    bool host_seated_itself = false;
    bool second_reached = false;
    bool second_told = false;
    bool host_still_seats_second = false;
};

RefusalEvidence &refusal_evidence() {
    static RefusalEvidence evidence;
    return evidence;
}

void record_second_join_failed(int64_t, const String &) {
    refusal_evidence().second_told = true;
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
    int port = 0;
    int client_id = 0;
    int settle_count = 0;
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

    bool arm_return_ignored_policy() {
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
        client->observe(
            netw::NetwMultiplayer::SESSION_JOIN_FAILED,
            callable_mp_static(&record_second_join_failed)
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
                port = open_host();
                if (port == 0) {
                    evidence.driven = true;
                    return false;
                }
                enter(1);
                return true;
            }
            case 1: {
                if (host->is_online() && host->player_local().is_valid()) {
                    evidence.host_seated_itself = true;
                    if (!arm_return_ignored_policy() || !open_client(port)) {
                        enter(99);
                        return true;
                    }
                    enter(6);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 6: {
                const int seated = client->NETW_API_VIRTUAL(get_unique_id)();
                if (seated > 1) {
                    evidence.second_reached = true;
                    client_id = seated;
                    settle_count = 0;
                    enter(60);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 60: {
                settle_count = 0;
                enter(7);
                return true;
            }
            case 7: {
                settle_count++;
                if (settle_count >= SETTLE_POLLS || over_budget()) {
                    evidence.host_still_seats_second = seats(host, client_id);
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
    "[Networked][Session][Frame] JR2 a handler answering something that is "
    "not a placement leaves the peer seated, because a return value has no "
    "network effect and only an explicit refusal turns a peer away"
) {
    const RefusalEvidence &evidence = refusal_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.second_reached);
    CHECK_FALSE(evidence.second_told);
    CHECK(evidence.host_still_seats_second);
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

#if defined(DEBUG_ENABLED)

namespace {

constexpr const char *LOBBY_GATE = R"(extends Node

var open := false

func admit(request: NetwJoinRequest) -> Error:
	if open or request.username != &"turned_away":
		return OK
	return request.refuse(ERR_LOCKED, "The lobby is closed.")
)";

constexpr const char *LOBBY_SINK = R"(extends Node

var senders: Array[int] = []

@rpc("any_peer", "call_remote", "reliable")
func note_call(_tag: int) -> void:
	senders.append(multiplayer.get_remote_sender_id())
)";

struct LobbyEvidence {
    bool driven = false;
    int refused_id = 0;
    int admitted_id = 0;
    bool host_connected_refused = false;
    bool host_listed_refused = false;
    bool refused_heard_host = false;
    bool host_heard_refused = false;
    bool admitted_heard_host = false;
    bool host_heard_admitted = false;
    int failed = 0;
    int64_t failed_code = OK;
    String failed_reason;
    int join_failed = 0;
    int64_t join_failed_code = OK;
    String join_failed_reason;
    int exited = 0;
    bool admitted_entered = false;
    bool readmitted = false;
    int64_t errors = 0;
};

LobbyEvidence &enet_lobby_evidence() {
    static LobbyEvidence evidence;
    return evidence;
}

LobbyEvidence &deaf_lobby_evidence() {
    static LobbyEvidence evidence;
    return evidence;
}

struct LobbySeat {
    Node *branch = nullptr;
    Node *gate = nullptr;
    Node *sink = nullptr;
    Ref<netw::NetwMultiplayer> session;
    std::unique_ptr<netw_test::CallLog> heard;

    bool open(const char *p_name, netw::NetwMultiplayer::Role p_role) {
        heard = std::make_unique<netw_test::CallLog>();
        branch = memnew(Node);
        branch->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(branch);
        Ref<SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(branch->get_path());
        session.instantiate();
        session->session_set_inner(inner);
        session->session_set_role(p_role);
        netw::gd::scene_tree()->set_multiplayer(session, branch->get_path());
        gate = netw_test::minted_node(LOBBY_GATE);
        sink = netw_test::minted_node(LOBBY_SINK);
        if (gate == nullptr || sink == nullptr) {
            return false;
        }
        sink->set_name(StringName("Sink"));
        branch->add_child(gate);
        branch->add_child(sink);
        return netw::Netw::configure_admission(
                   Callable(gate, StringName("admit"))
        )
            .is_valid();
    }

    bool sink_heard(int p_peer) const {
        const Array senders = sink->get(StringName("senders"));
        return senders.has(p_peer);
    }

    void join(const char *p_username, const Ref<MultiplayerPeer> &p_peer) {
        session->session_prepare_join(StringName(p_username), Array());
        session->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
    }

    void close() {
        if (session.is_null()) {
            return;
        }
        session->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        session->embed_dispose();
        netw::gd::scene_tree()->set_multiplayer(
            Ref<MultiplayerAPI>(),
            branch->get_path()
        );
        netw::gd::scene_root()->remove_child(branch);
        memdelete(branch);
        session.unref();
        heard.reset();
    }
};

Ref<MultiplayerPeer> enet_client(int p_port) {
    const Ref<MultiplayerPeer> peer
        = ClassDB::instantiate(StringName("ENetMultiplayerPeer"));
    const Error reached = Error(
        int(
            peer->call(StringName("create_client"), String("127.0.0.1"), p_port)
        )
    );
    return reached == OK ? peer : Ref<MultiplayerPeer>();
}

class LobbyRefusalScenario final : public netw_test::FrameScenario {
    LobbyEvidence &(*evidence_of)();
    bool deaf;
    int step = 0;
    int port = 0;
    int64_t step_opened = 0;
    bool recorded = false;
    LobbySeat host;
    LobbySeat admitted;
    LobbySeat refused;
    Ref<netw_test::FaultLog> diagnostics;

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

    bool open_host() {
        if (!host.open(
                "LobbyHost",
                netw::NetwMultiplayer::ROLE_LISTEN_SERVER
            )) {
            return false;
        }
        for (int candidate = PORT_RANGE_START;
             candidate < PORT_RANGE_START + PORT_RANGE_SIZE;
             candidate++) {
            const Ref<MultiplayerPeer> peer
                = ClassDB::instantiate(StringName("ENetMultiplayerPeer"));
            if (Error(int(peer->call(StringName("create_server"), candidate)))
                != OK) {
                continue;
            }
            port = candidate;
            host.session->connect(
                StringName("peer_connected"),
                host.heard->callable("connected")
            );
            host.join("host", deaf ? netw_test::DeafPeer::over(peer) : peer);
            return true;
        }
        return false;
    }

    bool open_clients() {
        if (!admitted.open("LobbyAdmitted", netw::NetwMultiplayer::ROLE_CLIENT)
            || !refused
                    .open("LobbyRefused", netw::NetwMultiplayer::ROLE_CLIENT)) {
            return false;
        }
        const Ref<netw::NetwSession> door = refused.session->get_session();
        door->connect(
            StringName("join_failed"),
            refused.heard->callable("join_failed")
        );
        door->connect(StringName("exited"), refused.heard->callable("exited"));
        refused.session->get_connection()->connect(
            StringName("failed"),
            refused.heard->callable("failed")
        );
        const Ref<MultiplayerPeer> refused_peer = enet_client(port);
        const Ref<MultiplayerPeer> admitted_peer = enet_client(port);
        if (refused_peer.is_null() || admitted_peer.is_null()) {
            return false;
        }
        evidence_of().refused_id = refused_peer->get_unique_id();
        evidence_of().admitted_id = admitted_peer->get_unique_id();
        refused.join("turned_away", refused_peer);
        admitted.join("ana", admitted_peer);
        return true;
    }

    void pump() {
        for (LobbySeat *seat : {&host, &admitted, &refused}) {
            if (seat->session.is_valid()) {
                seat->session->NETW_API_VIRTUAL(poll)();
            }
        }
    }

    void watch_refused() {
        LobbyEvidence &evidence = evidence_of();
        const int refused_id = evidence.refused_id;
        const PackedInt32Array listed
            = host.session->NETW_API_VIRTUAL(get_peer_ids)();
        evidence.host_listed_refused
            = evidence.host_listed_refused || listed.has(refused_id);
        for (LobbySeat *seat : {&host, &admitted, &refused}) {
            const Ref<MultiplayerPeer> peer
                = seat->session->session_get_inner()->get_multiplayer_peer();
            if (peer.is_valid()
                && peer->get_connection_status()
                    == MultiplayerPeer::CONNECTION_CONNECTED) {
                seat->sink->rpc(StringName("note_call"), 1);
            }
        }
    }

    void record() {
        LobbyEvidence &evidence = evidence_of();
        if (recorded) {
            return;
        }
        recorded = true;
        const netw_test::CallLog &heard = *refused.heard;
        for (int at = 0; at < host.heard->count(StringName("connected"));
             at++) {
            const int peer
                = int(host.heard->args(StringName("connected"), at)[0]);
            evidence.host_connected_refused = evidence.host_connected_refused
                || peer == evidence.refused_id;
        }
        evidence.refused_heard_host = refused.sink_heard(1);
        evidence.admitted_heard_host = admitted.sink_heard(1);
        evidence.host_heard_refused = host.sink_heard(evidence.refused_id);
        evidence.host_heard_admitted = host.sink_heard(evidence.admitted_id);
        evidence.failed = heard.count(StringName("failed"));
        if (evidence.failed > 0) {
            const Array args = heard.args(StringName("failed"), 0);
            evidence.failed_code = int64_t(args[0]);
            evidence.failed_reason = args[1];
        }
        evidence.join_failed = heard.count(StringName("join_failed"));
        if (evidence.join_failed > 0) {
            const Array args = heard.args(StringName("join_failed"), 0);
            evidence.join_failed_code = int64_t(args[0]);
            evidence.join_failed_reason = args[1];
        }
        evidence.exited = heard.count(StringName("exited"));
    }

public:
    LobbyRefusalScenario(LobbyEvidence &(*p_evidence_of)(), bool p_deaf)
        : evidence_of(p_evidence_of), deaf(p_deaf) {
    }

    bool advance() override {
        LobbyEvidence &evidence = evidence_of();
        pump();

        switch (step) {
            case 0: {
                diagnostics.instantiate();
                netw_test::FaultLog::attach(diagnostics);
                enter(open_host() ? 1 : 99);
                return true;
            }
            case 1: {
                if (host.session->is_online()
                    && host.session->player_local().is_valid()) {
                    enter(open_clients() ? 2 : 99);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                watch_refused();
                const bool settled = admitted.session->player_local().is_valid()
                    && refused.heard->count(StringName("failed")) > 0
                    && admitted.sink_heard(1)
                    && host.sink_heard(evidence.admitted_id);
                if (settled) {
                    evidence.admitted_entered
                        = host.session->get_session()
                              ->player_of(evidence.admitted_id)
                              .is_valid();
                    record();
                    host.gate->set(StringName("open"), true);
                    enter(4);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 4: {
                if (refused.session->session_get_state()
                    != netw::NetwMultiplayer::SESSION_STATE_OFFLINE) {
                    if (over_budget()) {
                        enter(99);
                    }
                    return true;
                }
                const Ref<MultiplayerPeer> again = enet_client(port);
                if (again.is_null()) {
                    enter(99);
                    return true;
                }
                refused.join("turned_away", again);
                enter(5);
                return true;
            }
            case 5: {
                if (refused.session->player_local().is_valid()) {
                    evidence.readmitted = true;
                    enter(99);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            default: {
                record();
                refused.close();
                admitted.close();
                host.close();
                netw_test::FaultLog::detach(diagnostics);
                evidence.errors = diagnostics->error_count();
                diagnostics.unref();
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(
    LobbyRefusalScenario,
    enet_lobby_scenario,
    &enet_lobby_evidence,
    false
);
NETW_FRAME_SCENARIO(
    LobbyRefusalScenario,
    deaf_lobby_scenario,
    &deaf_lobby_evidence,
    true
);

struct LobbyRow {
    const char *transport;
    const LobbyEvidence &evidence;
};

const LobbyRow *lobby_rows() {
    static const LobbyRow rows[] = {
        {"ENetMultiplayerPeer", enet_lobby_evidence()},
        {"a server peer whose disconnect_peer does nothing",
         deaf_lobby_evidence()},
    };
    return rows;
}

constexpr int LOBBY_ROW_COUNT = 2;

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] JR4 a client the admission handler refuses "
    "never becomes a peer of the server, so the server fires no "
    "peer_connected for it and no RPC runs either way, while the admitted "
    "client is a peer both ways"
) {
    for (int at = 0; at < LOBBY_ROW_COUNT; at++) {
        const LobbyRow &row = lobby_rows()[at];
        NETW_FORMAT_TEXT(transport, row.transport);
        CAPTURE(transport);
        const LobbyEvidence &evidence = row.evidence;
        REQUIRE(evidence.driven);
        REQUIRE(evidence.refused_id > 1);
        CHECK_FALSE(evidence.host_connected_refused);
        CHECK_FALSE(evidence.host_listed_refused);
        CHECK_FALSE(evidence.refused_heard_host);
        CHECK_FALSE(evidence.host_heard_refused);
        CHECK(evidence.admitted_heard_host);
        CHECK(evidence.host_heard_admitted);
        CHECK(evidence.admitted_entered);
    }
}

TEST_CASE(
    "[Networked][Session][Frame] JR5 a refused client hears the handler's "
    "own Error and reason once through NetwConnection.failed and "
    "NetwSession.join_failed, and never exits a session it never entered"
) {
    for (int at = 0; at < LOBBY_ROW_COUNT; at++) {
        const LobbyRow &row = lobby_rows()[at];
        NETW_FORMAT_TEXT(transport, row.transport);
        CAPTURE(transport);
        const LobbyEvidence &evidence = row.evidence;
        REQUIRE(evidence.driven);
        NETW_CHECK_EQ(evidence.failed, 1);
        NETW_CHECK_EQ(evidence.failed_code, int64_t(ERR_LOCKED));
        CHECK(evidence.failed_reason == String("The lobby is closed."));
        NETW_CHECK_EQ(evidence.join_failed, 1);
        NETW_CHECK_EQ(evidence.join_failed_code, int64_t(ERR_LOCKED));
        CHECK(evidence.join_failed_reason == String("The lobby is closed."));
        NETW_CHECK_EQ(evidence.exited, 0);
    }
}

TEST_CASE(
    "[Networked][Session][Frame] JR6 a refused client closes its own peer, "
    "so on a server that cannot close the link it still joins again on the "
    "same session once the handler admits it"
) {
    for (int at = 0; at < LOBBY_ROW_COUNT; at++) {
        const LobbyRow &row = lobby_rows()[at];
        NETW_FORMAT_TEXT(transport, row.transport);
        CAPTURE(transport);
        const LobbyEvidence &evidence = row.evidence;
        REQUIRE(evidence.driven);
        CHECK(evidence.readmitted);
        NETW_CHECK_EQ(evidence.errors, int64_t(0));
    }
}

#endif

} // namespace NetwTests

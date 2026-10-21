#include "support/fault_log.h"
#include "support/netw_test.h"
#include "support/frame_drive.h"
#include "support/directory_stubs.h"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/time.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/context.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/session_core.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwSessionAdmissionLaws {

using namespace godot;
using netw::NetwMultiplayer;

constexpr const char *GATE = R"(extends Node

var calls := 0
var seen_peer := 0
var seen_name := StringName()
var seen_team := StringName()
var verdict := OK

func admit(request: NetwJoinRequest) -> Error:
	calls += 1
	seen_peer = request.peer_id
	seen_name = request.username
	if not request.args.is_empty():
		seen_team = request.args[0]
	return verdict

func seat(_who, _team: StringName) -> void:
	pass
)";

constexpr const char *GATE_ANSWERING_NO_ERROR = R"(extends Node

var calls := 0

func admit(_request: NetwJoinRequest):
	calls += 1
	return "admitted"
)";

Ref<Script> shape_of(const char *p_source) {
    const Ref<Script> shape
        = netw::gd::instantiate_class(StringName("GDScript"));
    shape->set_source_code(String(p_source));
    shape->reload();
    return shape;
}

Node *mounted_branch(const char *p_name) {
    Node *branch = memnew(Node);
    branch->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(branch);
    return branch;
}

Ref<NetwMultiplayer> branch_session(Node *p_branch) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(p_branch->get_path());
    Ref<NetwMultiplayer> made;
    made.instantiate();
    made->session_set_inner(inner);
    made->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    netw::gd::scene_tree()->set_multiplayer(made, p_branch->get_path());
    return made;
}

void release_branch(const Ref<NetwMultiplayer> &p_api, Node *p_branch) {
    p_api->embed_dispose();
    netw::gd::scene_tree()->set_multiplayer(
        Ref<MultiplayerAPI>(),
        p_branch->get_path()
    );
    netw::gd::scene_root()->remove_child(p_branch);
    memdelete(p_branch);
}

Node *gate_on(Node *p_branch, const char *p_source) {
    const Ref<Script> shape = shape_of(p_source);
    Node *written = Object::cast_to<Node>(shape->call("new"));
    p_branch->add_child(written);
    return written;
}

void join_as(
    const Ref<NetwMultiplayer> &p_api,
    const char *p_username,
    int64_t p_peer
) {
    netw::JoinRequest claim;
    claim.username = StringName(p_username);
    claim.app_tag = p_api->auth_app_tag_of();
    claim.wire_identity = netw::SessionCore::compute_wire_identity();
    claim.schema_identity = p_api->session_schema_identity();
    p_api->session_receive_join(claim.serialize(), p_peer);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G1 a join an admission handler answers "
    "OK for is admitted, and the handler saw it before the membership existed"
) {
    Node *branch = mounted_branch("AdmitG1");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    REQUIRE(gate != nullptr);
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_valid());
    CHECK(String(host->session_refusal(9)).is_empty());

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G2 a join an admission handler answers "
    "an Error for is turned down, and the refusal names that Error"
) {
    Node *branch = mounted_branch("AdmitG2");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_null());
    CHECK(String(host->session_refusal(9))
              .contains(String(netw::gd::error_name(ERR_UNAUTHORIZED))));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G3 a refused join spends no membership, "
    "so the peer admitted after it holds the one the refused peer would have "
    "had. The handler runs before the mint rather than after it, which is "
    "what a kick written inside the join handler cannot undo"
) {
    Node *branch = mounted_branch("AdmitG3");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());

    join_as(host, "ana", 9);
    CHECK(host->peer_get_player(9).is_null());

    gate->set(StringName("verdict"), int(OK));
    join_as(host, "bo", 11);

    const Ref<netw::NetwPlayer> seated = host->peer_get_player(11);
    REQUIRE(seated.is_valid());
    NETW_CHECK_EQ(seated->player_id(), int64_t(1));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G4 the handler is handed the joining "
    "peer, the username it claimed and the join's own arguments unflattened"
) {
    Node *branch = mounted_branch("AdmitG4");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    REQUIRE(
        netw::Netw::configure_join(Callable(gate, StringName("seat")))
            .is_valid()
    );
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());

    Array carried;
    carried.push_back(StringName("red"));
    host->session_submit_join(StringName("ana"), carried);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    NETW_CHECK_EQ(
        int64_t(gate->get(StringName("seen_peer"))),
        int64_t(host->NETW_API_VIRTUAL(get_unique_id)())
    );
    CHECK(StringName(gate->get(StringName("seen_name"))) == StringName("ana"));
    CHECK(StringName(gate->get(StringName("seen_team"))) == StringName("red"));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G5 the host's own join is judged by the "
    "same handler, so a gate that refuses everyone locks its own host out "
    "loudly rather than seating one peer under a rule it refused every other "
    "peer by"
) {
    Node *branch = mounted_branch("AdmitG5");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());

    host->session_submit_join(StringName("host"), Array());

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->player_local().is_null());

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G6 a handler that answers no Error at "
    "all refuses the join, because a gate that decides nothing has admitted "
    "nobody"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] The admission handler returned String. "
        "Return OK or an Error.",
        1
    );

    Node *branch = mounted_branch("AdmitG6");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE_ANSWERING_NO_ERROR);
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_null());
    CHECK(String(host->session_refusal(9))
              .contains(String("did not return an Error")));

    release_branch(host, branch);
}

constexpr const char *ASYNC_GATE = R"(extends Node

signal decided
signal proof_ready
var calls := 0
var proof_calls := 0
var delay_admit := true
var delay_proof := false
var padding := ""
var request: NetwJoinRequest

func prove(challenge: String) -> String:
    proof_calls += 1
    if delay_proof:
        await proof_ready
    return "proof:" + challenge + padding

func admit(value: NetwJoinRequest) -> Error:
    calls += 1
    request = value
    if delay_admit and value.peer_id != 1:
        await decided
    if not value.challenge.is_empty() \
            and value.proof != "proof:" + value.challenge + padding:
        return ERR_UNAUTHORIZED
    return OK
)";

TEST_CASE(
    "[Networked][Session][SceneTree] an awaited admission seats nobody until "
    "it decides, and a verdict for a timed out join seats nobody"
) {
    for (const bool timed_out : {false, true}) {
        CAPTURE(timed_out);
        Node *branch = mounted_branch("AwaitAdmission");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        Node *gate = gate_on(branch, ASYNC_GATE);
        REQUIRE(netw::Netw::configure_admission(
            Callable(gate, StringName("admit"))
        ).is_valid());
        join_as(host, "ana", 9);
        CHECK(host->peer_get_player(9).is_null());
        if (timed_out) {
            host->session_poll_admissions(
                int64_t(Time::get_singleton()->get_ticks_msec()) + 10000
            );
        }
        gate->emit_signal(StringName("decided"));
        CHECK(host->peer_get_player(9).is_valid() == !timed_out);
        NETW_CHECK_EQ(int(gate->get("calls")), 1);
        release_branch(host, branch);
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a required proof is separate from join "
    "arguments and a local host can await its own challenge"
) {
    Node *branch = mounted_branch("LocalProof");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, ASYNC_GATE);
    gate->set(StringName("delay_proof"), true);
    const Ref<netw::NetwAdmissionConfig> config
        = netw::Netw::configure_admission(
            Callable(gate, StringName("admit"))
        );
    REQUIRE(config.is_valid());
    config->proof(Callable(gate, StringName("prove")));

    join_as(host, "missing", 9);
    CHECK(host->peer_get_player(9).is_null());
    NETW_CHECK_EQ(int(gate->get("calls")), 0);

    host->session_submit_join(StringName("host"), Array());
    CHECK(host->player_local().is_null());
    gate->emit_signal(StringName("proof_ready"));
    CHECK(host->player_local().is_valid());
    const Ref<netw::NetwJoinRequest> request = gate->get("request");
    REQUIRE(request.is_valid());
    CHECK(bool(
        String(request->get_proof()) == "proof:" + request->get_challenge()
    ));
    CHECK(request->get_args().is_empty());
    CHECK(request->get_challenge_bound());
    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a default join with no username is "
    "refused for a missing proof like any other join"
) {
    Node *branch = mounted_branch("DefaultJoinProof");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, ASYNC_GATE);
    const Ref<netw::NetwAdmissionConfig> config
        = netw::Netw::configure_admission(
            Callable(gate, StringName("admit"))
        );
    REQUIRE(config.is_valid());
    config->proof(Callable(gate, StringName("prove")));

    join_as(host, "", 9);

    CHECK(host->peer_get_player(9).is_null());
    NETW_CHECK_EQ(int(gate->get("calls")), 0);
    CHECK(String(host->session_refusal(9))
              .contains(String("admission proof is missing")));
    release_branch(host, branch);
}

struct ProofEvidence {
    bool driven = false;
    bool waited_for_proof = false;
    bool outlasted_timeout_hint = false;
    bool waited_for_admission = false;
    bool admitted = false;
    bool challenge_bound = false;
    int errors = 0;
    int failure = OK;
    String failure_reason;
};

ProofEvidence &proof_evidence() {
    static ProofEvidence evidence;
    return evidence;
}

ProofEvidence &abandoned_proof_evidence() {
    static ProofEvidence evidence;
    return evidence;
}

ProofEvidence &webrtc_proof_evidence() {
    static ProofEvidence evidence;
    return evidence;
}

constexpr const char *PAIRED_SIGNALER = R"(extends NetwWebRTCSignaler

static var inbox := {}
var me := 0

func _open(_room_id: String, local_peer_id: int) -> Error:
    me = local_peer_id
    inbox[me] = []
    return OK

func _room_id() -> String:
    return "PAIRED"

func _send(
        to_peer_id: int, _to: String, kind: String, payload: Dictionary
) -> void:
    if not inbox.has(to_peer_id):
        inbox[to_peer_id] = []
    inbox[to_peer_id].append([me, kind, payload])

func _poll(_delta: float) -> void:
    report_ready()
    var mail: Array = inbox.get(me, [])
    inbox[me] = []
    for sent in mail:
        receive(sent[0], str(sent[0]), sent[1], sent[2])
)";

class ProofScenario : public netw_test::FrameScenario {
    ProofEvidence &(*evidence_of)();
    const char *peer_class;
    bool server_gives_up;
    Ref<Script> signaler;
    Node *host_branch = nullptr;
    Node *client_branch = nullptr;
    Node *host_gate = nullptr;
    Node *client_gate = nullptr;
    Ref<NetwMultiplayer> host;
    Ref<NetwMultiplayer> client;
    Ref<netw::NetwPromise> hosting;
    Ref<netw::NetwPromise> joining;
    Ref<netw_test::FaultLog> diagnostics;
    int phase = 0;
    int64_t started_msec = 0;

    Dictionary settings() const {
        Dictionary made;
        if (signaler.is_valid()) {
            made["signaler"] = signaler->call("new");
        } else {
            made["port"] = 0;
        }
        return made;
    }

public:
    ProofScenario(
        ProofEvidence &(*p_evidence_of)(),
        const char *p_peer_class,
        bool p_server_gives_up = false
    )
        : evidence_of(p_evidence_of), peer_class(p_peer_class),
          server_gives_up(p_server_gives_up) {
    }

    bool advance() override {
        ProofEvidence &evidence = evidence_of();
        const int64_t now = int64_t(Time::get_singleton()->get_ticks_msec());
        if (phase == 0) {
            started_msec = now;
            if (String(peer_class) == "WebRTCMultiplayerPeer") {
                signaler = shape_of(PAIRED_SIGNALER);
            }
            diagnostics.instantiate();
            netw_test::FaultLog::attach(diagnostics);
            host_branch = mounted_branch("ProofHost");
            client_branch = mounted_branch("ProofClient");
            host = branch_session(host_branch);
            client = branch_session(client_branch);
            client->session_set_role(NetwMultiplayer::ROLE_CLIENT);
            host_gate = gate_on(host_branch, ASYNC_GATE);
            client_gate = gate_on(client_branch, ASYNC_GATE);
            client_gate->set(StringName("delay_proof"), true);
            const String padding = String("x").repeat(2048);
            client_gate->set(StringName("padding"), padding);
            host_gate->set(StringName("padding"), padding);
            const Ref<netw::NetwAdmissionConfig> server_config
                = netw::Netw::configure_admission(
                    Callable(host_gate, StringName("admit"))
                );
            const Ref<netw::NetwAdmissionConfig> client_config
                = netw::Netw::configure_admission(
                    Callable(client_gate, StringName("admit"))
                );
            server_config->proof(Callable(host_gate, StringName("prove")));
            client_config->proof(Callable(client_gate, StringName("prove")));
            host->session_prepare_join(StringName("host"), Array());
            client->session_prepare_join(StringName("client"), Array());
            hosting = host->get_connection()->host(
                StringName(peer_class), settings()
            );
            phase = 1;
            return true;
        }
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(poll)();
        }
        if (client.is_valid()) {
            client->NETW_API_VIRTUAL(poll)();
        }
        if (now - started_msec > 15000) {
            phase = 5;
        }
        switch (phase) {
            case 1: {
                if (hosting->get_is_failed()) {
                    phase = 5;
                    break;
                }
                if (!hosting->get_is_completed()) {
                    break;
                }
                const String address = host->peer_join_address();
                const Ref<netw::NetwEndpoint> endpoint
                    = client->get_connection()->add(
                        StringName(peer_class),
                        signaler.is_valid()
                            ? address
                            : String("127.0.0.1:")
                                + address.get_slice(String(":"), 1),
                        String()
                    );
                joining = client->get_connection()->join(endpoint, settings());
                phase = 2;
                break;
            }
            case 2: {
                if (int(client_gate->get("proof_calls")) == 0) {
                    break;
                }
                evidence.waited_for_proof = !client->is_online();
                client->connect_plane().on_poll(60.0);
                evidence.outlasted_timeout_hint
                    = client->connect_plane().attempt_state()
                    == int(netw::connect::ATTEMPT_CONNECTING);
                if (server_gives_up) {
                    host->set_auth_timeout(0.001);
                    phase = 6;
                    break;
                }
                client_gate->emit_signal(StringName("proof_ready"));
                phase = 3;
                break;
            }
            case 6: {
                if (!joining->get_is_failed()) {
                    break;
                }
                evidence.failure = int(joining->get_code());
                evidence.failure_reason = joining->get_detail();
                phase = 5;
                break;
            }
            case 3: {
                if (int(host_gate->get("calls")) < 2) {
                    break;
                }
                evidence.waited_for_admission
                    = client->player_local().is_null()
                    && host->peer_get_player(
                        client->get_unique_id()
                    ).is_null();
                const Ref<netw::NetwJoinRequest> request
                    = host_gate->get("request");
                evidence.challenge_bound
                    = request.is_valid() && request->get_challenge_bound();
                host_gate->emit_signal(StringName("decided"));
                phase = 4;
                break;
            }
            case 4: {
                if (client->player_local().is_null()) {
                    break;
                }
                evidence.admitted = host->peer_get_player(
                    client->get_unique_id()
                ).is_valid();
                phase = 5;
                break;
            }
            default: {
                hosting.unref();
                joining.unref();
                client->NETW_API_VIRTUAL(set_multiplayer_peer)(
                    Ref<MultiplayerPeer>()
                );
                host->NETW_API_VIRTUAL(set_multiplayer_peer)(
                    Ref<MultiplayerPeer>()
                );
                release_branch(client, client_branch);
                release_branch(host, host_branch);
                client.unref();
                host.unref();
                evidence.errors = int(diagnostics->error_count());
                netw_test::FaultLog::detach(diagnostics);
                diagnostics.unref();
                signaler.unref();
                evidence.driven = true;
                return false;
            }
        }
        return true;
    }
};

NETW_FRAME_SCENARIO(
    ProofScenario, proof_scenario, &proof_evidence, "ENetMultiplayerPeer"
);
NETW_FRAME_SCENARIO(
    ProofScenario, webrtc_proof_scenario, &webrtc_proof_evidence,
    "WebRTCMultiplayerPeer"
);
NETW_FRAME_SCENARIO(
    ProofScenario, abandoned_proof_scenario, &abandoned_proof_evidence,
    "ENetMultiplayerPeer", true
);

TEST_CASE(
    "[Networked][Session][Frame] a server that drops a client waiting on "
    "its proof fails the join as a closed connection, not an unreachable "
    "server"
) {
    const ProofEvidence &evidence = abandoned_proof_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.waited_for_proof);
    NETW_CHECK_EQ(evidence.failure, int(ERR_CONNECTION_ERROR));
    CHECK(evidence.failure_reason.contains("auth_timeout"));
    NETW_CHECK_EQ(evidence.errors, 0);
}

TEST_CASE(
    "[Networked][Session][Frame] a client is announced only after its proof "
    "and an awaited admission, and only WebRTC binds the challenge to the "
    "server's certificate"
) {
    struct Row {
        const char *peer_class;
        const ProofEvidence &evidence;
        bool bound;
    };
    for (const Row &row : {
             Row{"ENetMultiplayerPeer", proof_evidence(), false},
             Row{"WebRTCMultiplayerPeer", webrtc_proof_evidence(), true},
         }) {
        NETW_FORMAT_TEXT(peer_class, row.peer_class);
        CAPTURE(peer_class);
        REQUIRE(row.evidence.driven);
        CHECK(row.evidence.waited_for_proof);
        CHECK(row.evidence.outlasted_timeout_hint);
        CHECK(row.evidence.waited_for_admission);
        CHECK(row.evidence.admitted);
        CHECK(row.evidence.challenge_bound == row.bound);
        NETW_CHECK_EQ(row.evidence.errors, 0);
    }
}

constexpr const char *PROMISE_GATE = R"(extends Node

var results: Array[NetwPromise] = []

func admit(_request: NetwJoinRequest) -> NetwPromise:
    var result := NetwPromise.new()
    results.append(result)
    return result
)";

TEST_CASE(
    "[Networked][Session][SceneTree] a late promise from a disconnected "
    "join cannot complete or refuse a later join from the same peer id"
) {
    Node *branch = mounted_branch("ReplacedAdmission");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, PROMISE_GATE);
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());
    join_as(host, "ana", 9);
    const Array first_results = gate->get("results");
    const Ref<netw::NetwPromise> first = first_results[0];
    host->session_clear_disconnected_peer(9);
    join_as(host, "bo", 9);
    const Array results = gate->get("results");
    const Ref<netw::NetwPromise> second = results[1];

    first->resolve(OK);
    CHECK(host->peer_get_player(9).is_null());
    second->resolve(OK);
    const Ref<netw::NetwPlayer> player = host->peer_get_player(9);
    CHECK(player.is_valid());
    if (player.is_valid()) {
        CHECK(bool(player->get_username() == StringName("bo")));
    }
    release_branch(host, branch);
}

class IdentityDirectory : public netw_test::StubDirectory {
public:
    String account_of(int64_t) override {
        return String("test:verified");
    }
    String member_name(int64_t) override {
        return String("Verified Name");
    }
    void host_lobby(const Dictionary &) override {
        Ref<netw::LocalMultiplayerPeer> peer;
        peer.instantiate();
        peer->create_server();
        deliver(peer);
    }
};

TEST_CASE(
    "[Networked][Session][SceneTree] only the active transport supplies "
    "identities and initializes the admission account and name"
) {
    for (int arm = 0; arm < 2; ++arm) {
        Node *branch = mounted_branch("TransportAdmissionIdentity");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        IdentityDirectory *directory = memnew(IdentityDirectory);
        host->service_register(directory, nullptr);
        Node *gate = gate_on(branch, ASYNC_GATE);
        REQUIRE(netw::Netw::configure_admission(
            Callable(gate, StringName("admit"))
        ).is_valid());
        Dictionary settings;
        settings["port"] = 0;
        const Ref<netw::NetwPromise> hosting = host->get_connection()->host(
            StringName(arm == 0 ? "StubLobbyPeer" : "ENetMultiplayerPeer"),
            settings
        );
        host->connect_plane().on_poll(0.0);
        CHECK(hosting->get_is_completed());
        join_as(host, "requested alias", 9);
        const Ref<netw::NetwJoinRequest> request = gate->get("request");
        REQUIRE(request.is_valid());
        const bool lobby = arm == 0;
        NETW_CHECK_EQ(int(request->get_identities().size()), lobby ? 1 : 0);
        CHECK(bool(request->get_account() == (lobby ? "test:verified" : "")));
        CHECK(bool(
            request->get_account_name() == (lobby ? "Verified Name" : "")
        ));
        CHECK(bool(request->get_username() == StringName("requested alias")));
        host->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        host->service_unregister(directory, nullptr);
        memdelete(directory);
        release_branch(host, branch);
    }
}

constexpr const char *ACCOUNT_GATE = R"(extends Node

var seats := 0
var kick_signed_in := false

func admit(request: NetwJoinRequest) -> Error:
    request.account = "test:" + request.username
    request.account_name = String(request.username).to_upper()
    if kick_signed_in:
        var session: NetwSession = Netw.session(self)
        var signed_in: NetwPlayer = session.find_player(request.account)
        if signed_in != null:
            session.kick(signed_in, "Signed in somewhere else.")
    return OK

func seat(_player: NetwPlayer) -> void:
    seats += 1
)";

Node *account_gate_on(Node *p_branch) {
    Node *gate = gate_on(p_branch, ACCOUNT_GATE);
    REQUIRE(
        netw::Netw::configure_join(Callable(gate, StringName("seat")))
            .is_valid()
    );
    REQUIRE(netw::Netw::configure_admission(
        Callable(gate, StringName("admit"))
    ).is_valid());
    return gate;
}

TEST_CASE(
    "[Networked][Session][SceneTree] a second join of an account already "
    "playing is refused with ERR_ALREADY_IN_USE, unless the admission "
    "handler kicks the first player or the first player left"
) {
    for (const char *arm : {"playing", "kicked", "left"}) {
        NETW_FORMAT_TEXT(arm_text, arm);
        CAPTURE(arm_text);
        const String path(arm);
        const bool replaced = path != "playing";
        Node *branch = mounted_branch("AccountInUse");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        Node *gate = account_gate_on(branch);
        gate->set(StringName("kick_signed_in"), path == "kicked");

        join_as(host, "ana", 9);
        const Ref<netw::NetwPlayer> first = host->peer_get_player(9);
        REQUIRE(first.is_valid());
        if (path == "left") {
            host->session_settle_peer_loss(9);
        }
        join_as(host, "ana", 11);

        const Ref<netw::NetwPlayer> second = host->peer_get_player(11);
        CHECK(second.is_valid() == replaced);
        CHECK(host->peer_get_player(9).is_valid() == !replaced);
        CHECK(first->get_is_active() == !replaced);
        CHECK(String(host->session_refusal(11)).contains("already playing")
              == !replaced);
        CHECK(bool(host->player_find_account("test:ana")
                   == (replaced ? second : first)));
        NETW_CHECK_EQ(int(gate->get(StringName("seats"))), replaced ? 2 : 1);
        release_branch(host, branch);
    }
}

} // namespace TestNetwSessionAdmissionLaws

#endif

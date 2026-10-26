#include "support/directory_stubs.h"
#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/account_provider.hpp"
#include "netw/api/admission_config.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/context.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"
#include "netw/auth_protocol.hpp"
#include "netw/session_core.hpp"

#include <initializer_list>
#include <vector>

#if defined(NETW_TIER_HOSTED)

namespace TestNetwSessionAdmissionLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

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

constexpr const char *GATE_TAKING_PEER_ID = R"(extends Node

var calls := 0

func admit(_peer_id: int, _username: StringName) -> Error:
	calls += 1
	return OK
)";

constexpr const char *ASYNC_GATE = R"(extends Node

signal decided
var calls := 0
var delay_admit := false
var request: NetwJoinRequest
var account_seen := ""
var name_seen := ""

func admit(value: NetwJoinRequest) -> Error:
    calls += 1
    request = value
    account_seen = value.account
    name_seen = value.account_name
    if delay_admit and value.peer_id != 1:
        await decided
    return OK
)";

constexpr const char *CHALLENGE = "0123456789abcdef0123456789abcdef";

class TestAccount : public netw::NetwAccountProvider {
    Ref<netw::NetwPromise> hold() {
        Ref<netw::NetwPromise> later;
        later.instantiate();
        held.push_back(later);
        return later;
    }

    void note(const char *p_step) {
        if (events != nullptr) {
            events->push_back(String(p_step) + String(" ") + label);
        }
    }

public:
    String label;
    String account;
    String name;
    String padding;
    bool answers_challenge = true;
    bool by_username = false;
    Error verdict = OK;
    bool hold_prove = false;
    bool hold_verify = false;
    PackedStringArray *events = nullptr;
    PackedStringArray heard;
    int verify_calls = 0;
    int64_t accounts_before_verify = -1;
    String account_during_verify;
    Ref<netw::NetwSession> proved_in;
    Ref<netw::NetwSession> verified_in;
    LocalVector<Ref<netw::NetwPromise>> held;

    String answer_to(const String &p_challenge) const {
        return label + String(":") + p_challenge + padding;
    }

    Variant prove(
        const String &p_challenge,
        const Ref<netw::NetwSession> &p_session
    ) override {
        note("prove");
        heard.push_back(p_challenge);
        proved_in = p_session;
        if (account.is_empty()) {
            return Variant();
        }
        return hold_prove ? Variant(hold()) : Variant(answer_to(p_challenge));
    }

    Variant verify(
        const Ref<netw::NetwJoinRequest> &p_request,
        const Variant &p_proof
    ) override {
        note("verify");
        verify_calls += 1;
        accounts_before_verify = p_request->get_accounts().size();
        account_during_verify = p_request->get_account();
        verified_in = p_request->get_session();
        if (!account.is_empty() && p_proof.get_type() == Variant::STRING
            && String(p_proof) == answer_to(p_request->get_challenge())) {
            p_request->add_account(
                by_username
                    ? account + String(":") + String(p_request->get_username())
                    : account,
                name,
                answers_challenge
            );
        }
        return hold_verify ? Variant(hold()) : Variant(int64_t(verdict));
    }

    void release_prove() {
        held[held.size() - 1]->resolve(answer_to(heard[heard.size() - 1]));
    }
};

Ref<TestAccount> account_row(
    const char *p_label,
    const char *p_account = "",
    const char *p_name = ""
) {
    Ref<TestAccount> made(memnew(TestAccount));
    made->label = String(p_label);
    made->account = String(p_account);
    made->name = String(p_name);
    return made;
}

Ref<netw::NetwAccountProvider> as_provider(const Ref<TestAccount> &p_row) {
    return Ref<netw::NetwAccountProvider>(p_row.ptr());
}

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

Ref<netw::NetwAdmissionConfig> admit_through(Node *p_gate) {
    const Ref<netw::NetwAdmissionConfig> config
        = netw::Netw::configure_admission(
            Callable(p_gate, StringName("admit"))
        );
    REQUIRE(config.is_valid());
    return config;
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

void join_proving(
    const Ref<NetwMultiplayer> &p_api,
    int64_t p_peer,
    const Variant &p_proofs
) {
    p_api->auth_open_challenge_under_test(p_peer, String(CHALLENGE));
    netw::JoinRequest claim;
    claim.username = StringName("ana");
    claim.app_tag = p_api->auth_app_tag_of();
    claim.wire_identity = netw::SessionCore::compute_wire_identity();
    claim.schema_identity = p_api->session_schema_identity();
    claim.proof_bytes = netw::gd::var_to_bytes(p_proofs);
    p_api->session_receive_join(claim.serialize(), p_peer);
}

Array proofs_for(
    std::initializer_list<const TestAccount *> p_rows,
    const String &p_challenge
) {
    Array proofs;
    for (const TestAccount *row : p_rows) {
        proofs.push_back(
            row->account.is_empty() ? Variant()
                                    : Variant(row->answer_to(p_challenge))
        );
    }
    return proofs;
}

TEST_CASE(
    "[Networked][Session][SceneTree] G1 a join an admission handler answers "
    "OK for is admitted, and the handler saw it before the membership existed"
) {
    Node *branch = mounted_branch("AdmitG1");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    REQUIRE(gate != nullptr);
    admit_through(gate);

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
    admit_through(gate);

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
    admit_through(gate);

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
    admit_through(gate);

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
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] The host could not join its own session, so it has "
        "no NetwPlayer. This server did not admit the join (Unauthorized).",
        1
    );

    Node *branch = mounted_branch("AdmitG5");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE);
    gate->set(StringName("verdict"), int(ERR_UNAUTHORIZED));
    admit_through(gate);

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
    admit_through(gate);

    join_as(host, "ana", 9);

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 1);
    CHECK(host->peer_get_player(9).is_null());
    CHECK(String(host->session_refusal(9))
              .contains(String("did not return an Error")));

    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] G7 a handler that cannot take a "
    "NetwJoinRequest refuses the join with one error naming the signature, "
    "and the host's own join reports it once"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] The admission handler cannot take a "
        "NetwJoinRequest, so the join was refused. Declare it as func "
        "admit(request: NetwJoinRequest) -> Error.",
        1
    );
    expected_faults.expect_error(
        "[error][session] The host could not join its own session, so it has "
        "no NetwPlayer. The admission handler cannot take a NetwJoinRequest*",
        1
    );

    Node *branch = mounted_branch("AdmitG7");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, GATE_TAKING_PEER_ID);
    admit_through(gate);

    join_as(host, "ana", 9);
    host->session_submit_join(StringName("host"), Array());

    NETW_CHECK_EQ(int(gate->get(StringName("calls"))), 0);
    CHECK(host->peer_get_player(9).is_null());
    CHECK(host->player_local().is_null());

    release_branch(host, branch);
}

enum AwaitedStep {
    AWAIT_ADMIT,
    AWAIT_VERIFY,
    AWAIT_PROVE,
};

TEST_CASE(
    "[Networked][Session][SceneTree] an awaited prove, verify or admission "
    "seats nobody until it decides, and a step that outlives auth_timeout "
    "seats nobody when it decides later"
) {
    struct Row {
        const char *step;
        AwaitedStep awaited;
    };
    for (const Row &row : {
             Row{"admit", AWAIT_ADMIT},
             Row{"verify", AWAIT_VERIFY},
             Row{"prove", AWAIT_PROVE},
         }) {
        for (const bool timed_out : {false, true}) {
            NETW_FORMAT_TEXT(step, row.step);
            CAPTURE(step);
            CAPTURE(timed_out);
            Node *branch = mounted_branch("AwaitAdmission");
            const Ref<NetwMultiplayer> host = branch_session(branch);
            Node *gate = gate_on(branch, ASYNC_GATE);
            gate->set(StringName("delay_admit"), row.awaited == AWAIT_ADMIT);
            const Ref<netw::NetwAdmissionConfig> config = admit_through(gate);
            const Ref<TestAccount> held = account_row("held", "held:ana");
            held->hold_verify = row.awaited == AWAIT_VERIFY;
            held->hold_prove = row.awaited == AWAIT_PROVE;
            if (row.awaited != AWAIT_ADMIT) {
                config->accept(as_provider(held));
            }
            const auto seated = [&]() {
                return row.awaited == AWAIT_PROVE
                    ? host->player_local().is_valid()
                    : host->peer_get_player(9).is_valid();
            };

            if (row.awaited == AWAIT_ADMIT) {
                join_as(host, "ana", 9);
            } else if (row.awaited == AWAIT_VERIFY) {
                join_proving(
                    host,
                    9,
                    proofs_for({held.ptr()}, String(CHALLENGE))
                );
            } else {
                host->session_submit_join(StringName("host"), Array());
            }
            CHECK_FALSE(seated());
            if (timed_out) {
                host->session_poll_admissions(
                    int64_t(Time::get_singleton()->get_ticks_msec()) + 10000
                );
            }
            if (row.awaited == AWAIT_ADMIT) {
                gate->emit_signal(StringName("decided"));
            } else if (row.awaited == AWAIT_VERIFY) {
                REQUIRE(held->held.size() == 1);
                held->held[0]->resolve(int64_t(OK));
            } else {
                REQUIRE(held->held.size() == 1);
                held->release_prove();
            }

            CHECK(seated() == !timed_out);
            NETW_CHECK_EQ(
                int(gate->get("calls")),
                timed_out && row.awaited != AWAIT_ADMIT ? 0 : 1
            );
            release_branch(host, branch);
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a required proof is separate from join "
    "arguments, a join without one is refused before the handler, and a "
    "local host awaits its own challenge"
) {
    Node *branch = mounted_branch("LocalProof");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, ASYNC_GATE);
    const Ref<TestAccount> held = account_row("held", "held:host");
    held->hold_prove = true;
    admit_through(gate)->accept(as_provider(held));

    join_as(host, "missing", 9);
    CHECK(host->peer_get_player(9).is_null());
    NETW_CHECK_EQ(int(gate->get("calls")), 0);
    CHECK(String(host->session_refusal(9))
              .contains(String("admission proof is missing")));

    join_as(host, "", 10);
    CHECK(host->peer_get_player(10).is_null());
    CHECK(String(host->session_refusal(10))
              .contains(String("admission proof is missing")));

    host->session_submit_join(StringName("host"), Array());
    CHECK(host->player_local().is_null());
    held->release_prove();
    CHECK(host->player_local().is_valid());
    const Ref<netw::NetwJoinRequest> request = gate->get("request");
    REQUIRE(request.is_valid());
    CHECK(request->get_args().is_empty());
    CHECK(bool(request->get_accounts() == PackedStringArray({"held:host"})));
    CHECK(request->is_bound(String("held:host")));
    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] every provider proves in accept order "
    "with the same challenge, then every provider verifies in accept order, "
    "and only then does the admission handler run"
) {
    Node *branch = mounted_branch("AcceptOrder");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, ASYNC_GATE);
    PackedStringArray events;
    const Ref<TestAccount> first = account_row("first", "first:1");
    const Ref<TestAccount> second = account_row("second", "second:2");
    first->events = &events;
    second->events = &events;
    admit_through(gate)
        ->accept(as_provider(first))
        ->accept(as_provider(second));

    host->session_submit_join(StringName("host"), Array());

    CHECK(
        bool(
            events
            == PackedStringArray(
                {"prove first", "prove second", "verify first", "verify second"}
            )
        )
    );
    const Ref<netw::NetwJoinRequest> request = gate->get("request");
    REQUIRE(request.is_valid());
    REQUIRE(first->heard.size() == 1);
    REQUIRE(second->heard.size() == 1);
    CHECK(bool(first->heard[0] == request->get_challenge()));
    CHECK(bool(second->heard[0] == request->get_challenge()));
    CHECK_FALSE(request->get_challenge().is_empty());
    NETW_CHECK_EQ(int(gate->get("calls")), 1);
    CHECK(host->player_local().is_valid());
    release_branch(host, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a config with no provider sends no "
    "challenge, and one provider is enough to send one"
) {
    for (const bool providing : {false, true}) {
        CAPTURE(providing);
        Node *branch = mounted_branch("NoChallenge");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        Node *gate = gate_on(branch, ASYNC_GATE);
        const Ref<netw::NetwAdmissionConfig> config = admit_through(gate);
        if (providing) {
            config->accept(as_provider(account_row("only", "only:1")));
        }

        host->session_submit_join(StringName("host"), Array());

        CHECK(host->session_requires_proof() == providing);
        const Ref<netw::NetwJoinRequest> request = gate->get("request");
        REQUIRE(request.is_valid());
        CHECK(request->get_challenge().is_empty() == !providing);
        CHECK(host->player_local().is_valid());
        release_branch(host, branch);
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a joining peer that proves a different "
    "number of accounts than the server accepts is refused before any "
    "provider verifies"
) {
    struct Row {
        const char *shape;
        int sent;
        bool array;
    };
    for (const Row &row : {
             Row{"one of two", 1, true},
             Row{"three of two", 3, true},
             Row{"not an array", 0, false},
         }) {
        NETW_FORMAT_TEXT(shape, row.shape);
        CAPTURE(shape);
        Node *branch = mounted_branch("ProofCount");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        Node *gate = gate_on(branch, ASYNC_GATE);
        const Ref<TestAccount> first = account_row("first", "first:1");
        const Ref<TestAccount> second = account_row("second", "second:2");
        admit_through(gate)
            ->accept(as_provider(first))
            ->accept(as_provider(second));
        Array sent;
        for (int at = 0; at < row.sent; ++at) {
            sent.push_back(first->answer_to(String(CHALLENGE)));
        }

        join_proving(
            host,
            9,
            row.array ? Variant(sent) : Variant(String("forged"))
        );

        CHECK(host->peer_get_player(9).is_null());
        NETW_CHECK_EQ(first->verify_calls, 0);
        NETW_CHECK_EQ(int(gate->get("calls")), 0);
        const String refusal = host->session_refusal(9);
        CHECK(refusal.contains(String("accepts 2 accounts")));
        CHECK(refusal.contains(vformat("proved %d", row.sent)));
        release_branch(host, branch);
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] an Error from a provider's verify "
    "refuses the join with that Error, and no later provider or admission "
    "handler runs"
) {
    Node *branch = mounted_branch("VerifyRefuses");
    const Ref<NetwMultiplayer> host = branch_session(branch);
    Node *gate = gate_on(branch, ASYNC_GATE);
    const Ref<TestAccount> first = account_row("first", "first:1");
    const Ref<TestAccount> second = account_row("second", "second:2");
    first->verdict = ERR_UNAUTHORIZED;
    admit_through(gate)
        ->accept(as_provider(first))
        ->accept(as_provider(second));

    join_proving(
        host,
        9,
        proofs_for({first.ptr(), second.ptr()}, String(CHALLENGE))
    );

    CHECK(host->peer_get_player(9).is_null());
    NETW_CHECK_EQ(first->verify_calls, 1);
    NETW_CHECK_EQ(second->verify_calls, 0);
    NETW_CHECK_EQ(int(gate->get("calls")), 0);
    CHECK(String(host->session_refusal(9))
              .contains(String(netw::gd::error_name(ERR_UNAUTHORIZED))));
    release_branch(host, branch);
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
    "join, from the admission handler or from a provider's verify, cannot "
    "complete or refuse a later join from the same peer id"
) {
    for (const bool verifying : {false, true}) {
        CAPTURE(verifying);
        Node *branch = mounted_branch("ReplacedAdmission");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        Node *gate = gate_on(branch, verifying ? ASYNC_GATE : PROMISE_GATE);
        const Ref<netw::NetwAdmissionConfig> config = admit_through(gate);
        const Ref<TestAccount> held = account_row("held", "held:ana");
        held->hold_verify = true;
        if (verifying) {
            config->accept(as_provider(held));
        }
        const Array proofs = proofs_for({held.ptr()}, String(CHALLENGE));
        const auto join = [&]() {
            if (verifying) {
                join_proving(host, 9, proofs);
            } else {
                join_as(host, "ana", 9);
            }
        };
        const auto promise_at = [&](int p_at) -> Ref<netw::NetwPromise> {
            if (verifying) {
                return held->held[p_at];
            }
            const Array results = gate->get("results");
            return results[p_at];
        };

        join();
        const Ref<netw::NetwPromise> first = promise_at(0);
        host->session_clear_disconnected_peer(9);
        join();
        const Ref<netw::NetwPromise> second = promise_at(1);

        first->resolve(int64_t(OK));
        CHECK(host->peer_get_player(9).is_null());
        second->resolve(int64_t(OK));
        CHECK(host->peer_get_player(9).is_valid());
        release_branch(host, branch);
    }
}

TEST_CASE(
    "[Networked][Session] an account added twice is listed once, keeps the "
    "first name that is not empty, and is bound when either addition was "
    "bound"
) {
    struct Add {
        const char *account;
        const char *name;
        bool answers_challenge;
        bool vouched;
    };
    struct Row {
        const char *label;
        bool challenge_bound;
        std::vector<Add> adds;
        PackedStringArray accounts;
        const char *first_name;
        bool first_bound;
    };
    const Row rows[] = {
        {"bound challenge",
         true,
         {{"x:1", "X", true, false}},
         {"x:1"},
         "X",
         true},
        {"unbound challenge",
         false,
         {{"x:1", "X", true, false}},
         {"x:1"},
         "X",
         false},
        {"answers no challenge",
         true,
         {{"x:1", "X", false, false}},
         {"x:1"},
         "X",
         false},
        {"vouched later",
         false,
         {{"x:1", "", false, false}, {"x:1", "V", false, true}},
         {"x:1"},
         "V",
         true},
        {"first name kept",
         true,
         {{"x:1", "First", true, false}, {"x:1", "Second", true, false}},
         {"x:1"},
         "First",
         true},
        {"empty name filled",
         false,
         {{"x:1", "", true, false}, {"x:1", "Second", true, false}},
         {"x:1"},
         "Second",
         false},
        {"two accounts in order",
         true,
         {{"y:2", "Y", true, false}, {"x:1", "X", false, false}},
         {"y:2", "x:1"},
         "Y",
         true},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(label, row.label);
        CAPTURE(label);
        Ref<netw::NetwJoinRequest> request;
        request.instantiate();
        request->bind_to(
            9,
            StringName("ana"),
            Array(),
            String(CHALLENGE),
            row.challenge_bound
        );
        for (const Add &add : row.adds) {
            if (add.vouched) {
                request->add_vouched_account(
                    String(add.account),
                    String(add.name)
                );
            } else {
                request->add_account(
                    String(add.account),
                    String(add.name),
                    add.answers_challenge
                );
            }
        }
        request->start_from_first_account();
        CHECK(bool(request->get_accounts() == row.accounts));
        const String first = row.accounts[0];
        CHECK(bool(request->name_of(first) == String(row.first_name)));
        CHECK(request->is_bound(first) == row.first_bound);
        CHECK(bool(request->get_account() == first));
        CHECK(bool(request->get_account_name() == String(row.first_name)));
        CHECK(request->name_of(String("z:9")).is_empty());
        CHECK_FALSE(request->is_bound(String("z:9")));
    }
}

class IdentityDirectory : public netw_test::StubDirectory {
public:
    String account_of(int64_t p_peer) override {
        return p_peer == 1 ? String("test:host")
                           : String("test:") + String::num_int64(p_peer);
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

struct HostedBranch {
    Node *branch = nullptr;
    Ref<NetwMultiplayer> host;
    IdentityDirectory *directory = nullptr;

    explicit HostedBranch(bool p_directory) {
        branch = mounted_branch("AccountsHost");
        host = branch_session(branch);
        if (p_directory) {
            directory = memnew(IdentityDirectory);
            host->service_register(directory, nullptr);
        }
        Dictionary settings;
        settings["port"] = 0;
        const Ref<netw::NetwPromise> hosting = host->get_connection()->host(
            StringName(p_directory ? "StubLobbyPeer" : "ENetMultiplayerPeer"),
            settings
        );
        host->connect_plane().on_poll(0.0);
        CHECK(hosting->get_is_completed());
    }

    ~HostedBranch() {
        host->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        if (directory != nullptr) {
            host->service_unregister(directory, nullptr);
            memdelete(directory);
        }
        release_branch(host, branch);
    }
};

TEST_CASE(
    "[Networked][Session][SceneTree] a server served by a directory binds "
    "the challenge to the directory's account for peer 1, and a server "
    "whose transport identifies nobody leaves it unbound"
) {
    for (const bool directory : {false, true}) {
        CAPTURE(directory);
        HostedBranch hosted(directory);
        bool bound = false;
        const String made
            = hosted.host->session_bind_challenge(String(CHALLENGE), 9, bound);
        CHECK(bound == directory);
        CHECK(
            bool(
                made
                == netw::auth::bind_challenge(
                    String(CHALLENGE),
                    directory ? String("test:host") : String()
                )
            )
        );
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a client that joined through a "
    "directory binds the challenge to the same account for peer 1 as the "
    "server does"
) {
    Node *branch = mounted_branch("BindingClient");
    const Ref<NetwMultiplayer> client = branch_session(branch);
    client->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    IdentityDirectory *directory = memnew(IdentityDirectory);
    client->service_register(directory, nullptr);
    const Ref<netw::NetwConnection> connection = client->get_connection();
    const Ref<netw::NetwEndpoint> endpoint
        = connection->add(StringName("StubLobbyPeer"), String("77"), String());
    connection->join(endpoint, Dictionary());
    client->connect_plane().on_poll(0.0);

    bool bound = false;
    const String made
        = client->session_bind_challenge(String(CHALLENGE), 1, bound);
    CHECK(bound);
    CHECK(
        bool(
            made
            == netw::auth::bind_challenge(
                String(CHALLENGE),
                String("test:host")
            )
        )
    );

    client->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
    client->service_unregister(directory, nullptr);
    memdelete(directory);
    release_branch(client, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] leaving a session a directory delivered "
    "leaves the directory's lobby, on the server and on a client"
) {
    {
        HostedBranch hosted(true);
        NETW_CHECK_EQ(hosted.directory->leave_calls, 0);
        hosted.host->session_leave();
        NETW_CHECK_EQ(hosted.directory->leave_calls, 1);
    }
    Node *branch = mounted_branch("LeavingClient");
    const Ref<NetwMultiplayer> client = branch_session(branch);
    client->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    IdentityDirectory *directory = memnew(IdentityDirectory);
    client->service_register(directory, nullptr);
    const Ref<netw::NetwConnection> connection = client->get_connection();
    connection->join(
        connection->add(StringName("StubLobbyPeer"), String("77"), String()),
        Dictionary()
    );
    client->connect_plane().on_poll(0.0);
    NETW_CHECK_EQ(directory->leave_calls, 0);

    client->session_leave();

    NETW_CHECK_EQ(directory->leave_calls, 1);
    client->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
    client->service_unregister(directory, nullptr);
    memdelete(directory);
    release_branch(client, branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] accounts list each provider's in accept "
    "order and then the transport's, and account and account_name start as "
    "the first before the admission handler runs"
) {
    struct Provider {
        const char *label;
        const char *account;
        const char *name;
        bool answers_challenge;
    };
    struct Row {
        const char *label;
        bool directory;
        std::vector<Provider> providers;
        PackedStringArray accounts;
        const char *first_name;
        bool first_bound;
    };
    const Row rows[] = {
        {"two providers over ENet",
         false,
         {{"a", "a:1", "Ann", true}, {"b", "b:2", "Bea", true}},
         {"a:1", "b:2"},
         "Ann",
         false},
        {"a provider with no account",
         false,
         {{"a", "", "", true}, {"b", "b:2", "Bea", true}},
         {"b:2"},
         "Bea",
         false},
        {"providers before the transport",
         true,
         {{"a", "a:1", "Ann", true}},
         {"a:1", "test:9"},
         "Ann",
         true},
        {"the transport vouches a provider's account",
         true,
         {{"t", "test:9", "", false}},
         {"test:9"},
         "Verified Name",
         true},
        {"the transport alone", true, {}, {"test:9"}, "Verified Name", true},
        {"nothing proven", false, {}, {}, "", false},
    };
    for (const Row &row : rows) {
        NETW_FORMAT_TEXT(label, row.label);
        CAPTURE(label);
        HostedBranch hosted(row.directory);
        Node *gate = gate_on(hosted.branch, ASYNC_GATE);
        const Ref<netw::NetwAdmissionConfig> config = admit_through(gate);
        LocalVector<Ref<TestAccount>> made;
        Array proofs;
        const String challenge = netw::auth::bind_challenge(
            String(CHALLENGE),
            row.directory ? String("test:host") : String()
        );
        for (const Provider &provider : row.providers) {
            const Ref<TestAccount> account
                = account_row(provider.label, provider.account, provider.name);
            account->answers_challenge = provider.answers_challenge;
            config->accept(as_provider(account));
            proofs.push_back(
                account->account.is_empty()
                    ? Variant()
                    : Variant(account->answer_to(challenge))
            );
            made.push_back(account);
        }

        if (made.is_empty()) {
            join_as(hosted.host, "ana", 9);
        } else {
            join_proving(hosted.host, 9, proofs);
        }

        const Ref<netw::NetwJoinRequest> request = gate->get("request");
        REQUIRE(request.is_valid());
        CHECK(bool(request->get_accounts() == row.accounts));
        int64_t proven_before = 0;
        for (const Ref<TestAccount> &account : made) {
            NETW_CHECK_EQ(account->accounts_before_verify, proven_before);
            CHECK(account->account_during_verify.is_empty());
            proven_before += account->account.is_empty() ? 0 : 1;
        }
        const String first
            = row.accounts.is_empty() ? String() : row.accounts[0];
        CHECK(bool(String(gate->get("account_seen")) == first));
        CHECK(bool(String(gate->get("name_seen")) == String(row.first_name)));
        CHECK(request->is_bound(first) == row.first_bound);
        CHECK(hosted.host->peer_get_player(9).is_valid());
    }
}

struct ProofEvidence {
    bool driven = false;
    bool waited_for_proof = false;
    bool outlasted_timeout_hint = false;
    bool waited_for_admission = false;
    bool admitted = false;
    bool account_bound = false;
    bool prove_heard_challenge = false;
    bool proved_in_own_session = false;
    bool verified_in_server_session = false;
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
    Ref<TestAccount> host_account;
    Ref<TestAccount> client_account;
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
            host_gate->set(StringName("delay_admit"), true);
            const String padding = String("x").repeat(2048);
            host_account = account_row("club", "club:member");
            client_account = account_row("club", "club:member");
            host_account->padding = padding;
            client_account->padding = padding;
            host_account->by_username = true;
            client_account->hold_prove = true;
            netw::Netw::configure_admission(
                Callable(host_gate, StringName("admit"))
            )
                ->accept(as_provider(host_account));
            netw::Netw::configure_admission(
                Callable(client_gate, StringName("admit"))
            )
                ->accept(as_provider(client_account));
            host->session_prepare_join(StringName("host"), Array());
            client->session_prepare_join(StringName("client"), Array());
            hosting = host->get_connection()->host(
                StringName(peer_class),
                settings()
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
                        signaler.is_valid() ? address
                                            : String("127.0.0.1:")
                                + address.get_slice(String(":"), 1),
                        String()
                    );
                joining = client->get_connection()->join(endpoint, settings());
                phase = 2;
                break;
            }
            case 2: {
                if (client_account->heard.is_empty()) {
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
                client_account->release_prove();
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
                evidence.waited_for_admission = client->player_local().is_null()
                    && host->peer_get_player(client->get_unique_id()).is_null();
                const Ref<netw::NetwJoinRequest> request
                    = host_gate->get("request");
                evidence.account_bound = request.is_valid()
                    && request->is_bound(String("club:member:client"));
                evidence.prove_heard_challenge = request.is_valid()
                    && client_account->heard[0] == request->get_challenge();
                evidence.proved_in_own_session
                    = client_account->proved_in.is_valid()
                    && client_account->proved_in == client->get_session()
                    && host_account->proved_in == host->get_session();
                evidence.verified_in_server_session = request.is_valid()
                    && request->get_session().is_valid()
                    && request->get_session() == host->get_session()
                    && host_account->verified_in == host->get_session();
                host_gate->emit_signal(StringName("decided"));
                phase = 4;
                break;
            }
            case 4: {
                if (client->player_local().is_null()) {
                    break;
                }
                evidence.admitted
                    = host->peer_get_player(client->get_unique_id()).is_valid();
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
                host_account.unref();
                client_account.unref();
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
    ProofScenario,
    proof_scenario,
    &proof_evidence,
    "ENetMultiplayerPeer"
);
NETW_FRAME_SCENARIO(
    ProofScenario,
    webrtc_proof_scenario,
    &webrtc_proof_evidence,
    "WebRTCMultiplayerPeer"
);
NETW_FRAME_SCENARIO(
    ProofScenario,
    abandoned_proof_scenario,
    &abandoned_proof_evidence,
    "ENetMultiplayerPeer",
    true
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
    "and an awaited admission, its provider proves the challenge the server "
    "verifies against, and only WebRTC binds that challenge to the server"
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
        CHECK(row.evidence.prove_heard_challenge);
        CHECK(row.evidence.account_bound == row.bound);
        NETW_CHECK_EQ(row.evidence.errors, 0);
    }
}

TEST_CASE(
    "[Networked][Session][Frame] with a host and a client in one process, "
    "each provider proves with its own peer's session, and the request it "
    "verifies names the server's session"
) {
    for (const ProofEvidence *evidence :
         {&proof_evidence(), &webrtc_proof_evidence()}) {
        REQUIRE(evidence->driven);
        CHECK(evidence->proved_in_own_session);
        CHECK(evidence->verified_in_server_session);
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
    admit_through(gate);
    return gate;
}

struct SecondLogin {
    const char *arm;
    int64_t first_peer;
    bool replaced;
    int left_reason;
};

const SecondLogin SECOND_LOGINS[] = {
    {"playing", 9, true, netw::NetwPlayer::LEAVE_DROPPED},
    {"kicked", 9, true, netw::NetwPlayer::LEAVE_KICKED},
    {"dropped", 9, true, netw::NetwPlayer::LEAVE_DROPPED},
    {"server", 1, false, -1},
};

TEST_CASE(
    "[Networked][Session][SceneTree] a second join of an account already "
    "playing replaces the first player, who leaves with its reason before "
    "the second is seated, and the server's own player is never replaced"
) {
    for (const SecondLogin &row : SECOND_LOGINS) {
        NETW_FORMAT_TEXT(arm_text, row.arm);
        CAPTURE(arm_text);
        const String path(row.arm);
        Node *branch = mounted_branch("AccountInUse");
        const Ref<NetwMultiplayer> host = branch_session(branch);
        Node *gate = account_gate_on(branch);
        gate->set(StringName("kick_signed_in"), path == "kicked");
        const CallLog heard;
        const Ref<netw::NetwSession> door = host->get_session();
        door->connect(StringName("player_left"), heard.callable("left"));
        door->connect(StringName("player_joined"), heard.callable("joined"));

        join_as(host, "ana", row.first_peer);
        const Ref<netw::NetwPlayer> first
            = host->peer_get_player(row.first_peer);
        REQUIRE(first.is_valid());
        if (path == "dropped") {
            host->session_settle_peer_loss(row.first_peer);
        }
        join_as(host, "ana", 11);

        const Ref<netw::NetwPlayer> second = host->peer_get_player(11);
        CHECK(second.is_valid() == row.replaced);
        CHECK(first->get_is_active() == !row.replaced);
        CHECK(
            bool(
                host->player_find_account("test:ana")
                == (row.replaced ? second : first)
            )
        );
        NETW_CHECK_EQ(
            int(gate->get(StringName("seats"))),
            row.replaced ? 2 : 1
        );
        if (!row.replaced) {
            CHECK(
                String(host->session_refusal(11)).contains("already playing")
            );
            NETW_CHECK_EQ(heard.count(StringName("left")), 0);
            release_branch(host, branch);
            continue;
        }
        NETW_REQUIRE_EQ(heard.count(StringName("left")), 1);
        CHECK(
            bool(
                Ref<netw::NetwPlayer>(heard.args(StringName("left"))[0])
                == first
            )
        );
        NETW_CHECK_EQ(int(heard.args(StringName("left"))[1]), row.left_reason);
        const Vector<StringName> order = heard.order();
        NETW_REQUIRE_EQ(int(order.size()), 3);
        CHECK(bool(order[1] == StringName("left")));
        CHECK(bool(order[2] == StringName("joined")));
        release_branch(host, branch);
    }
}

} // namespace TestNetwSessionAdmissionLaws

#endif

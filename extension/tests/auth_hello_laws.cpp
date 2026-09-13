#include "support/auth_stand.h"
#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/auth_protocol.hpp"
#include "netw/session_decl.hpp"
#include "support/netw_call_log.h"

namespace TestNetwAuthHello {

using namespace godot;
using netw::AuthResult;
using netw::NetwIdentity;
using netw::NetwMultiplayer;
using netw_test::NetwTestAuthFlow;

namespace auth = netw::auth;

void check_text(const String &p_seen, const String &p_want) {
    NETW_FORMAT_TEXT(seen_text, p_seen.utf8().get_data());
    NETW_FORMAT_TEXT(want_text, p_want.utf8().get_data());
    CAPTURE(seen_text);
    CAPTURE(want_text);
    const bool same = p_seen == p_want;
    CHECK(same);
}

const int64_t APP_TAG = 0x5a5a5a5a;
const int64_t PEER = 42;

Ref<NetwMultiplayer> tagged_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->auth_set_app_tag(APP_TAG);
    return session;
}

Ref<AuthResult> accepting_as(const StringName &p_username) {
    Ref<NetwIdentity> identity;
    identity.instantiate();
    identity->set_username(p_username);
    Ref<AuthResult> verdict;
    verdict.instantiate();
    verdict->set_accepted(true);
    verdict->set_identity(identity);
    return verdict;
}

Ref<NetwTestAuthFlow> flow_answering(const Ref<AuthResult> &p_verdict) {
    Ref<NetwTestAuthFlow> flow;
    flow.instantiate();
    flow->set_verdict(p_verdict);
    return flow;
}

TEST_CASE(
    "[Networked][Session][Hosted] H1 a hello stamped with another build's "
    "app tag is refused by name, and the flow never sees it"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    session->auth_set_flow(flow);
    session->auth_receive_hello(
        PEER,
        auth::encode_client_hello(PackedByteArray(), 0x11111111, 0)
    );
    check_text(
        String(session->session_refusal(PEER)),
        String("Incompatible game build")
    );
    NETW_CHECK_EQ(flow->verify_count(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] H2 a payload that is not a hello at all "
    "is dropped with no refusal recorded, because nothing decided about it"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    PackedByteArray foreign;
    for (int at = 0; at < 12; at++) {
        foreign.push_back(uint8_t(at));
    }
    session->auth_receive_hello(PEER, foreign);
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H3 a session with no flow admits a "
    "well-formed hello without recording a refusal or an identity"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_receive_hello(
        PEER,
        auth::encode_client_hello(PackedByteArray(), APP_TAG, 0)
    );
    check_text(String(session->session_refusal(PEER)), String());
    const bool nameless = session->peer_get_identity(PEER).is_null();
    CHECK(nameless);
}

TEST_CASE(
    "[Networked][Session][Hosted] H4 an accepting flow has its identity "
    "stored against the peer, and it verified the hello's own payload"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    session->auth_set_flow(flow);
    PackedByteArray proof;
    proof.push_back(7);
    proof.push_back(9);
    session->auth_receive_hello(
        PEER,
        auth::encode_client_hello(proof, APP_TAG, 0)
    );
    const Ref<NetwIdentity> stored = session->peer_get_identity(PEER);
    const bool held = stored.is_valid();
    CHECK(held);
    if (held) {
        check_text(String(stored->get_username()), String("ana"));
    }
    NETW_CHECK_EQ(flow->verify_count(), 1);
    const bool same_bytes = flow->last_verified_payload() == proof;
    CHECK(same_bytes);
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H5 a rejecting flow has its reason "
    "recorded, and a rejection with no reason still records one"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    Ref<AuthResult> spoken;
    spoken.instantiate();
    spoken->set_rejection_reason("bad ticket");
    session->auth_set_flow(flow_answering(spoken));
    session->auth_receive_hello(
        PEER,
        auth::encode_client_hello(PackedByteArray(), APP_TAG, 0)
    );
    check_text(String(session->session_refusal(PEER)), String("bad ticket"));

    const Ref<NetwMultiplayer> silent = tagged_session();
    Ref<AuthResult> wordless;
    wordless.instantiate();
    silent->auth_set_flow(flow_answering(wordless));
    silent->auth_receive_hello(
        PEER,
        auth::encode_client_hello(PackedByteArray(), APP_TAG, 0)
    );
    check_text(
        String(silent->session_refusal(PEER)),
        String("Authentication failed")
    );
    const bool nameless = silent->peer_get_identity(PEER).is_null();
    CHECK(nameless);
}

TEST_CASE(
    "[Networked][Session][Hosted] H6 a flow that answers no verdict at all "
    "refuses fail-closed rather than admitting the peer"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_set_flow(flow_answering(Ref<AuthResult>()));
    session->auth_receive_hello(
        PEER,
        auth::encode_client_hello(PackedByteArray(), APP_TAG, 0)
    );
    check_text(
        String(session->session_refusal(PEER)),
        String("Authentication failed")
    );
    const bool nameless = session->peer_get_identity(PEER).is_null();
    CHECK(nameless);
}

TEST_CASE(
    "[Networked][Session][Hosted] H7 the flow, the app tag and the "
    "application callback are one book the session clears together"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    session->auth_set_flow(flow);
    const bool seated = session->auth_flow() == flow;
    CHECK(seated);
    NETW_CHECK_EQ(session->auth_app_tag_of(), APP_TAG);
    session->auth_clear();
    const bool released = session->auth_flow().is_null();
    CHECK(released);
    const bool silent = !session->get_auth_callback().is_valid();
    CHECK(silent);
}

TEST_CASE(
    "[Networked][Session][Hosted] H8 an application callback is handed the "
    "hello unchanged, and the seated flow is never asked to verify it"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    session->auth_set_flow(flow);
    session->set_auth_callback(Callable(flow.ptr(), "record"));
    session->auth_receive(
        PEER,
        auth::encode_client_hello(PackedByteArray(), APP_TAG, 0)
    );
    NETW_CHECK_EQ(flow->record_count(), 1);
    NETW_CHECK_EQ(flow->verify_count(), 0);
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H9 a probe is consumed by the session even "
    "while an application callback owns every other packet"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    session->set_auth_callback(Callable(flow.ptr(), "record"));
    session->auth_receive(PEER, auth::encode_probe_request(0));
    NETW_CHECK_EQ(flow->record_count(), 0);
    PackedByteArray foreign;
    for (int at = 0; at < 12; at++) {
        foreign.push_back(uint8_t(at));
    }
    session->auth_receive(PEER, foreign);
    NETW_CHECK_EQ(flow->record_count(), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] H10 a session with nothing to prepare "
    "answers a promise that has already settled, so the join never suspends"
) {
    const Ref<NetwMultiplayer> bare = tagged_session();
    const Ref<netw::NetwPromise> nothing
        = bare->auth_prepare(StringName("ana"));
    CHECK(nothing->get_is_settled());
    NETW_CHECK_EQ(int(nothing->get_result()), int(OK));

    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    flow->set_prepared(netw::NetwPromise::resolved(ERR_UNAUTHORIZED));
    session->auth_set_flow(flow);
    const Ref<netw::NetwPromise> asked
        = session->auth_prepare(StringName("ana"));
    NETW_CHECK_EQ(int(asked->get_result()), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(flow->prepare_count(), 1);

    session->set_auth_callback(Callable(flow.ptr(), "record"));
    const Ref<netw::NetwPromise> skipped
        = session->auth_prepare(StringName("ana"));
    NETW_CHECK_EQ(int(skipped->get_result()), int(OK));
    NETW_CHECK_EQ(flow->prepare_count(), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] H11 the host seats the identity its own "
    "flow claims, because no handshake ever runs for the server peer"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    Ref<NetwIdentity> host;
    host.instantiate();
    host->set_username("hosting");
    flow->set_host(host);
    session->auth_set_flow(flow);
    session->auth_seat_host_identity();
    const Ref<NetwIdentity> seated = session->peer_get_identity(1);
    const bool held = seated.is_valid();
    CHECK(held);
    if (held) {
        check_text(String(seated->get_username()), String("hosting"));
    }

    const Ref<NetwMultiplayer> silent = tagged_session();
    silent->auth_set_flow(flow_answering(accepting_as("ana")));
    silent->auth_seat_host_identity();
    const bool nameless = silent->peer_get_identity(1).is_null();
    CHECK(nameless);
}

TEST_CASE(
    "[Networked][Session][Hosted] H12 a join is renamed to the identity the "
    "server verified, and keeps its claim when nothing verified one"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_set_flow(flow_answering(accepting_as("ana")));
    Ref<NetwIdentity> verified;
    verified.instantiate();
    verified->set_username("ana");
    session->peer_set_identity(PEER, verified);

    netw::JoinRequest join;
    join.username = StringName("claimed");
    session->auth_resolve_identity(PEER, join);
    check_text(String(join.username), String("ana"));

    netw::JoinRequest unverified;
    unverified.username = StringName("claimed");
    session->auth_resolve_identity(77, unverified);
    check_text(String(unverified.username), String("claimed"));

    netw::JoinRequest hosting;
    hosting.username = StringName("claimed");
    session->peer_set_identity(1, verified);
    session->auth_resolve_identity(1, hosting);
    check_text(String(hosting.username), String("claimed"));
}

Node *auth_branch(const char *p_name) {
    Node *branch = memnew(Node);
    branch->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(branch);
    return branch;
}

Ref<NetwMultiplayer> auth_session_at(Node *p_branch) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(p_branch->get_path());
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_inner(inner);
    session->auth_set_app_tag(APP_TAG);
    netw::gd::scene_tree()->set_multiplayer(session, p_branch->get_path());
    return session;
}

void release_auth_branch(Node *p_branch) {
    netw::gd::scene_tree()->set_multiplayer(
        Ref<MultiplayerAPI>(),
        p_branch->get_path()
    );
    netw::gd::scene_root()->remove_child(p_branch);
    memdelete(p_branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] H13 a declared factory runs once per "
    "session, on the first demand rather than at declaration, and the "
    "session keeps the flow it built"
) {
    Node *branch = auth_branch("AuthDeclared");
    Node *scope = memnew(Node);
    scope->set_name(StringName("Session"));
    branch->add_child(scope);
    const Ref<NetwTestAuthFlow> declared = flow_answering(accepting_as("ana"));

    const Ref<NetwMultiplayer> session = auth_session_at(branch);
    netw::Netw::configure_auth(scope, Callable(declared.ptr(), "mint"));
    NETW_CHECK_EQ(declared->mint_count(), 0);

    const bool built = session->auth_effective_flow() == declared;
    CHECK(built);
    NETW_CHECK_EQ(declared->mint_count(), 1);
    session->auth_effective_flow();
    NETW_CHECK_EQ(declared->mint_count(), 1);

    SUBCASE("and a per-session override outranks the declaration") {
        const Ref<NetwTestAuthFlow> chosen = flow_answering(accepting_as("bo"));
        session->auth_set_flow(chosen);
        const bool overridden = session->auth_effective_flow() == chosen;
        CHECK(overridden);
        const bool seated = session->auth_flow() == chosen;
        CHECK(seated);
    }

    session->embed_dispose();
    release_auth_branch(branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] H13b a flow the session already built "
    "outlives the node that declared its factory, because losing a factory "
    "is not losing the policy it produced"
) {
    Node *branch = auth_branch("AuthOutlives");
    Node *scope = memnew(Node);
    scope->set_name(StringName("Session"));
    branch->add_child(scope);
    const Ref<NetwTestAuthFlow> declared = flow_answering(accepting_as("ana"));

    const Ref<NetwMultiplayer> session = auth_session_at(branch);
    netw::Netw::configure_auth(scope, Callable(declared.ptr(), "mint"));
    CHECK(session->auth_effective_flow() == declared);

    branch->remove_child(scope);
    memdelete(scope);

    const bool kept = session->auth_effective_flow() == declared;
    CHECK(kept);
    CHECK(session->auth_is_configured());
    NETW_CHECK_EQ(declared->mint_count(), 1);

    session->embed_dispose();
    release_auth_branch(branch);
}

TEST_CASE(
    "[Networked][Session][SceneTree] H13c a factory that answers no flow "
    "fails authentication closed and is not retried for every peer, because "
    "a broken policy is not an absent one"
) {
    Node *branch = auth_branch("AuthBroken");
    Node *scope = memnew(Node);
    scope->set_name(StringName("Session"));
    branch->add_child(scope);
    const netw_test::CallLog log;

    const Ref<NetwMultiplayer> session = auth_session_at(branch);
    netw::Netw::configure_auth(scope, log.callable("mint"));

    CHECK(session->auth_effective_flow().is_null());
    CHECK(session->auth_is_configured());
    session->auth_effective_flow();
    session->auth_effective_flow();
    NETW_CHECK_EQ(log.count("mint"), 1);

    const Ref<netw::NetwPromise> prepared
        = session->auth_prepare(StringName("ana"));
    CHECK(prepared->get_is_failed());

    session->embed_dispose();
    release_auth_branch(branch);
}

TEST_CASE(
    "[Networked][Session][Hosted] H13d a session nobody configured admits "
    "on the hello alone, and its preparation is not a refusal"
) {
    const Ref<NetwMultiplayer> session = tagged_session();

    CHECK(session->auth_effective_flow().is_null());
    CHECK_FALSE(session->auth_is_configured());
    const Ref<netw::NetwPromise> prepared
        = session->auth_prepare(StringName("ana"));
    CHECK(prepared->get_is_completed());
    NETW_CHECK_EQ(int(prepared->get_result()), int(OK));
}

TEST_CASE(
    "[Networked][Session][Hosted] H14 a prepare the flow refuses settles the "
    "join's own promise with that refusal and leaves no prepared join behind, "
    "so a caller learns its identity was rejected and nothing is left staged "
    "to submit on the next online edge"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    flow->set_prepared(netw::NetwPromise::resolved(ERR_UNAUTHORIZED));
    session->auth_set_flow(flow);

    const Ref<netw::NetwPromise> asked
        = session->session_prepare_join(StringName("ana"), Array());

    NETW_CHECK_EQ(int(asked->get_is_settled()), 1);
    NETW_CHECK_EQ(int(asked->get_result()), int(ERR_UNAUTHORIZED));
    CHECK_FALSE(session->session_prepared_join().has_value());

    SUBCASE("a prepare the flow allows stages the request it was handed") {
        flow->set_prepared(netw::NetwPromise::resolved(OK));
        const Ref<netw::NetwPromise> allowed
            = session->session_prepare_join(StringName("ana"), Array());

        NETW_CHECK_EQ(int(allowed->get_result()), int(OK));
        REQUIRE(session->session_prepared_join().has_value());
        CHECK(session->session_prepared_join()->username == StringName("ana"));
    }

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] H10 a hello waits while the join "
    "preparation is unsettled and goes out when it settles, because a "
    "client that answered the handshake before its flow had credentials "
    "would authenticate as nobody"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    Ref<netw::NetwPromise> preparing;
    preparing.instantiate();
    flow->set_prepared(preparing);
    session->auth_set_flow(flow);

    session->session_prepare_join(StringName("ana"), Array());

    session->auth_send_hello(1);
    NETW_CHECK_EQ(flow->credential_count(), 0);

    preparing->resolve(OK);
    NETW_CHECK_EQ(flow->credential_count(), 1);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] H11 a hello sent with no preparation "
    "pending goes out at once, so a session that never prepared a join "
    "still completes the handshake"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const Ref<NetwTestAuthFlow> flow = flow_answering(accepting_as("ana"));
    flow->set_prepared(netw::NetwPromise::resolved(OK));
    session->auth_set_flow(flow);

    session->session_prepare_join(StringName("ana"), Array());

    session->auth_send_hello(1);
    NETW_CHECK_EQ(flow->credential_count(), 1);

    session->embed_dispose();
}

} // namespace TestNetwAuthHello

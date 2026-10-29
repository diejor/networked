#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/auth_protocol.hpp"
#include "netw/session_decl.hpp"
#include "support/netw_call_log.h"

namespace TestNetwAuthHello {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

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

void hold_auth(int64_t, const PackedByteArray &) {
}

struct PendingAuth {
    Ref<NetwMultiplayer> session = tagged_session();
    Ref<netw::LocalMultiplayerPeer> local;
    Ref<netw::LocalMultiplayerPeer> remote;

    explicit PendingAuth(bool p_server = true) {
        local.instantiate();
        remote.instantiate();
        if (p_server) {
            local->create_server();
            remote->create_client(PEER);
        } else {
            local->create_client(9);
            remote->create_server(PEER);
        }
        local->force_connect_peer(PEER, remote.ptr());
        remote->force_connect_peer(p_server ? 1 : 9, local.ptr());
        session->set_auth_callback(callable_mp_static(&hold_auth));
        session->auth_arm();
        session->session_get_inner()->set_multiplayer_peer(local);
        session->session_get_inner()->poll();
        session->set_auth_callback(Callable());
    }

    ~PendingAuth() {
        session->embed_dispose();
        local->NETW_PEER_VIRTUAL(close)();
        remote->NETW_PEER_VIRTUAL(close)();
    }
};

PackedByteArray foreign_bytes() {
    PackedByteArray foreign;
    for (int at = 0; at < 12; at++) {
        foreign.push_back(uint8_t(at));
    }
    return foreign;
}

TEST_CASE(
    "[Networked][Session][Hosted] H1 a hello stamped with another build's "
    "app tag is refused by name"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][session] Peer * was refused, because its app tag * "
        "differs from this server's *. Set the same tag on both "
        "with NetwMultiplayer.auth_set_app_tag.",
        1
    );

    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_receive_hello(PEER, auth::encode_client_hello(0x11111111, 0));
    check_text(
        String(session->session_refusal(PEER)),
        String("Incompatible game build")
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] H2 a payload that is not a hello at all "
    "is dropped with no refusal recorded, because nothing decided about it"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][session] Peer * was disconnected, because its "
        "authentication could not be read (framing). Run the same "
        "Networked version on both peers.",
        1
    );

    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_receive_hello(PEER, foreign_bytes());
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H3 a hello carrying this build's app tag "
    "records no refusal, and the server keeps the link in auth until it has "
    "admitted the join"
) {
    const netw_test::FaultWatch faults;
    PendingAuth stand;
    const Ref<NetwMultiplayer> session = stand.session;
    session->session_set_desired_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    session->session_peer_assigned(true, 1);
    REQUIRE(session->is_host());

    session->auth_receive_hello(PEER, auth::encode_client_hello(APP_TAG, 0));
    check_text(String(session->session_refusal(PEER)), String());
    CHECK_FALSE(session->auth_link_is_completed(PEER));
    NETW_CHECK_EQ(faults.errors(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] H7 the app tag and the application "
    "callback are one book the session clears together"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const CallLog log;
    session->set_auth_callback(log.callable(StringName("hello")));
    NETW_CHECK_EQ(session->auth_app_tag_of(), APP_TAG);
    session->auth_clear();
    const bool silent = !session->get_auth_callback().is_valid();
    CHECK(silent);
}

TEST_CASE(
    "[Networked][Session][Hosted] H8 an application callback is handed the "
    "data the other peer's game sent, unchanged, and the session decides "
    "nothing about it"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const CallLog log;
    session->set_auth_callback(log.callable(StringName("data")));
    session->auth_receive(PEER, auth::encode_game(foreign_bytes()));
    NETW_CHECK_EQ(log.count(StringName("data")), 1);
    const Array carried = log.args(StringName("data"));
    REQUIRE(carried.size() == 2);
    NETW_CHECK_EQ(int64_t(carried[0]), PEER);
    const bool whole = PackedByteArray(carried[1]) == foreign_bytes();
    CHECK(whole);
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H9 a probe is consumed by the session even "
    "while an application callback is set"
) {
    const netw_test::FaultWatch faults;
    PendingAuth stand;
    const Ref<NetwMultiplayer> session = stand.session;
    const CallLog log;
    session->set_auth_callback(log.callable(StringName("packet")));
    session->auth_receive(PEER, auth::encode_probe_request(0));
    NETW_CHECK_EQ(log.count(StringName("packet")), 0);
    session->auth_receive(PEER, auth::encode_game(foreign_bytes()));
    NETW_CHECK_EQ(log.count(StringName("packet")), 1);
    NETW_CHECK_EQ(faults.errors(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] H19 a client keeps another client's link "
    "in auth until the server has announced that client's player, so a "
    "refused client never becomes a peer of the admitted ones"
) {
    const netw_test::FaultWatch faults;
    PendingAuth stand(false);
    const Ref<NetwMultiplayer> session = stand.session;
    session->session_set_desired_role(NetwMultiplayer::ROLE_CLIENT);
    session->session_peer_assigned(true, 9);
    REQUIRE_FALSE(session->is_host());

    session->auth_receive_hello(PEER, auth::encode_client_hello(APP_TAG, 0));

    check_text(String(session->session_refusal(PEER)), String());
    CHECK_FALSE(session->auth_link_is_completed(PEER));

    netw::session::AcceptFrame announced;
    announced.peer_id = PEER;
    announced.username = StringName("ana");
    announced.player_id = 2;
    session->session_admit(announced);

    CHECK(session->auth_link_is_completed(PEER));

    session->embed_dispose();
    NETW_CHECK_EQ(faults.errors(), 0);
}

} // namespace TestNetwAuthHello

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
    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_receive_hello(
        PEER,
        auth::encode_client_hello(0x11111111, 0)
    );
    check_text(
        String(session->session_refusal(PEER)),
        String("Incompatible game build")
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] H2 a payload that is not a hello at all "
    "is dropped with no refusal recorded, because nothing decided about it"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_receive_hello(PEER, foreign_bytes());
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H3 a hello carrying this build's app tag "
    "completes the link without recording a refusal"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    session->auth_receive_hello(PEER, auth::encode_client_hello(APP_TAG, 0));
    check_text(String(session->session_refusal(PEER)), String());
    CHECK(session->auth_link_is_completed(PEER));
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
    "hello unchanged, and the session decides nothing about it"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const CallLog log;
    session->set_auth_callback(log.callable(StringName("hello")));
    session->auth_receive(PEER, auth::encode_client_hello(APP_TAG, 0));
    NETW_CHECK_EQ(log.count(StringName("hello")), 1);
    const Array carried = log.args(StringName("hello"));
    REQUIRE(carried.size() == 2);
    NETW_CHECK_EQ(int64_t(carried[0]), PEER);
    const bool whole
        = PackedByteArray(carried[1]) == auth::encode_client_hello(APP_TAG, 0);
    CHECK(whole);
    check_text(String(session->session_refusal(PEER)), String());
}

TEST_CASE(
    "[Networked][Session][Hosted] H9 a probe is consumed by the session even "
    "while an application callback owns every other packet"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    const CallLog log;
    session->set_auth_callback(log.callable(StringName("packet")));
    session->auth_receive(PEER, auth::encode_probe_request(0));
    NETW_CHECK_EQ(log.count(StringName("packet")), 0);
    session->auth_receive(PEER, foreign_bytes());
    NETW_CHECK_EQ(log.count(StringName("packet")), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] H19 a session that admits nobody still "
    "authenticates the link a hello arrives on, because two peers that never "
    "join each other still have to agree about the build they speak"
) {
    const Ref<NetwMultiplayer> session = tagged_session();
    session->session_set_authority_peer(7);
    session->session_set_desired_role(NetwMultiplayer::ROLE_CLIENT);
    session->session_peer_assigned(true, true, 9);
    REQUIRE_FALSE(session->is_host());

    session->auth_receive_hello(PEER, auth::encode_client_hello(APP_TAG, 0));

    check_text(String(session->session_refusal(PEER)), String());
    CHECK(session->auth_link_is_completed(PEER));

    session->embed_dispose();
}

} // namespace TestNetwAuthHello

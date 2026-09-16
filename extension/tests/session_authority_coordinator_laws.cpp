#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session/frames.hpp"

namespace TestNetwSessionAuthorityCoordinator {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> a_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 the default coordinator is peer 1, so "
    "a plain peer 1 arrival still resolves to server role with nobody "
    "reconfiguring anything"
) {
    Ref<NetwMultiplayer> session = a_session();
    NETW_CHECK_EQ(session->session_authority_peer(), int64_t(1));

    session->session_peer_assigned(true, false, 1);

    CHECK(session->is_host());
    CHECK(session->session_get_role() == NetwMultiplayer::ROLE_LISTEN_SERVER);
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 an explicit coordinator is the peer "
    "asked about authority, and transport peer 1 arriving under a coordinator "
    "of 7 is an ordinary actor with no incidental server role"
) {
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);
    NETW_CHECK_EQ(session->session_authority_peer(), int64_t(7));

    session->session_peer_assigned(true, false, 1);

    CHECK_FALSE(session->is_host());
    CHECK(
        session->session_get_state()
        == NetwMultiplayer::SESSION_STATE_CONNECTING
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 peer 7 arriving under a coordinator of "
    "7 is admitted to server role, the peer id that answers the coordinator "
    "query rather than the literal 1"
) {
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);

    session->session_peer_assigned(true, false, 7);

    CHECK(session->is_host());
    CHECK(session->session_get_role() == NetwMultiplayer::ROLE_LISTEN_SERVER);
    CHECK(
        session->session_get_state() == NetwMultiplayer::SESSION_STATE_ONLINE
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 a connected edge resolves online "
    "regardless of which peer it names, because a transport-confirmed "
    "connection needs no coordinator match to settle a client's own role"
) {
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);
    session->session_set_desired_role(NetwMultiplayer::ROLE_CLIENT);

    session->session_peer_assigned(true, true, 1);

    CHECK_FALSE(session->is_host());
    CHECK(session->session_get_role() == NetwMultiplayer::ROLE_CLIENT);
    CHECK(
        session->session_get_state() == NetwMultiplayer::SESSION_STATE_ONLINE
    );
}

PackedByteArray pong_payload(uint64_t p_origin, uint64_t p_tick) {
    netw::session::ClockPong pong;
    pong.origin = p_origin;
    pong.tick = p_tick;
    pong.phase = 128;
    return netw::session::frame_write(pong);
}

int &pong_accepted_count() {
    static int count = 0;
    return count;
}

void count_pong_accepted(const Dictionary &) {
    pong_accepted_count() += 1;
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 the clock's pong evidence answers the "
    "coordinator, not the literal peer 1, so a pong from transport peer 1 "
    "present under a coordinator of 7 is refused while the same pong from "
    "the coordinator settles"
) {
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);
    pong_accepted_count() = 0;
    session->connect(
        StringName("clock_pong_received"),
        callable_mp_static(&count_pong_accepted)
    );

    session->clock_receive_pong(pong_payload(0, 10), 1);
    NETW_CHECK_EQ(pong_accepted_count(), 0);

    session->clock_receive_pong(pong_payload(0, 10), 7);
    NETW_CHECK_EQ(pong_accepted_count(), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] L6 the coordinator is chosen while the "
    "session is still offline or connecting, which is the whole window a "
    "bootstrap has, and a peer id of zero or less is refused outright"
) {
    Ref<NetwMultiplayer> session = a_session();
    NETW_CHECK_EQ(
        session->session_get_state(),
        NetwMultiplayer::SESSION_STATE_OFFLINE
    );

    session->session_set_authority_peer(7);
    NETW_CHECK_EQ(session->session_authority_peer(), int64_t(7));

    session->session_peer_assigned(true, false, 9);
    NETW_CHECK_EQ(
        session->session_get_state(),
        NetwMultiplayer::SESSION_STATE_CONNECTING
    );

    session->session_set_authority_peer(11);
    NETW_CHECK_EQ(session->session_authority_peer(), int64_t(11));

    session->session_set_authority_peer(0);
    NETW_CHECK_EQ(session->session_authority_peer(), int64_t(11));
}

TEST_CASE(
    "[Networked][Session][Hosted] L7 a live session refuses to change its "
    "coordinator, so no caller can leave the authority half moved while the "
    "world is already running against the old one"
) {
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);
    session->session_peer_assigned(true, false, 7);
    REQUIRE(
        session->session_get_state() == NetwMultiplayer::SESSION_STATE_ONLINE
    );

    session->session_set_authority_peer(9);

    NETW_CHECK_EQ(session->session_authority_peer(), int64_t(7));
    CHECK(session->is_host());
}

TEST_CASE(
    "[Networked][Session][Hosted] L8 a session with no role and no connection "
    "is still its own authority, whatever coordinator it was configured with, "
    "because an offline game holds the authority Godot's own spawner assumes"
) {
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);

    CHECK(session->is_host());
    NETW_CHECK_EQ(
        session->session_get_role(),
        NetwMultiplayer::ROLE_NONE
    );
}

PackedByteArray deny_payload(const StringName &p_key) {
    netw::session::DenyKey denied;
    denied.key = p_key;
    return netw::session::frame_write(denied);
}

TEST_CASE(
    "[Networked][Session][Hosted] L9 a lag compensation denial reverts the "
    "speculative effect only when it comes from the coordinator that served "
    "the action, so transport peer 1 cannot revert a peer's prediction under "
    "a coordinator of 7"
) {
    const StringName KEY = StringName("an-effect");
    Ref<NetwMultiplayer> session = a_session();
    session->session_set_authority_peer(7);

    session->lagcomp_effect_arm(KEY, Callable(), 30);
    NETW_CHECK_EQ(int(session->lagcomp_effect_pending(KEY)), 1);

    session->handle_deny(deny_payload(KEY), 1);
    NETW_CHECK_EQ(int(session->lagcomp_effect_pending(KEY)), 1);

    session->handle_deny(deny_payload(KEY), 7);
    NETW_CHECK_EQ(int(session->lagcomp_effect_pending(KEY)), 0);
}

} // namespace TestNetwSessionAuthorityCoordinator

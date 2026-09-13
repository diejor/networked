#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionPropertySurface {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 every wrapped-transport property reads "
    "an inert default once the transport is cleared rather than reaching "
    "through it"
) {
    Ref<NetwMultiplayer> session = make_session();
    session->session_set_inner(Ref<SceneMultiplayer>());

    CHECK(session->get_root_path().is_empty());
    CHECK_FALSE(session->is_object_decoding_allowed());
    CHECK_FALSE(session->is_refusing_new_connections());
    CHECK_FALSE(session->is_server_relay_enabled());
    NETW_CHECK_EQ(session->get_auth_timeout(), 0);
    NETW_CHECK_EQ(session->get_max_sync_packet_size(), 0);
    NETW_CHECK_EQ(session->get_max_delta_packet_size(), 0);

    SUBCASE("and writing one is a no-op rather than a null deref") {
        session->set_root_path(NodePath("/root"));
        session->set_allow_object_decoding(true);
        session->set_server_relay_enabled(true);
        session->set_max_sync_packet_size(1200);
        session->set_auth_timeout(9.0);
        CHECK(session->get_root_path().is_empty());
        NETW_CHECK_EQ(session->get_auth_timeout(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 the authentication timeout is the "
    "wrapped transport's own rather than a copy the session keeps, so a "
    "write through either one is the single deadline the other reads"
) {
    Ref<NetwMultiplayer> session = make_session();
    Ref<SceneMultiplayer> transport;
    transport.instantiate();
    session->session_set_inner(transport);

    session->set_auth_timeout(12.0);

    NETW_CHECK_EQ(transport->get_auth_timeout(), 12);
    NETW_CHECK_EQ(session->get_auth_timeout(), 12);

    SUBCASE("and a write on the transport is the deadline the session reads") {
        transport->set_auth_timeout(4.0);
        NETW_CHECK_EQ(session->get_auth_timeout(), 4);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 the action gates carry the shell's own "
    "defaults, so a session that configures nothing still gates sanely"
) {
    Ref<NetwMultiplayer> session = make_session();

    NETW_CHECK_EQ(session->get_max_future_action_ticks(), 8);
    NETW_CHECK_EQ(session->get_input_gate_deadline_ticks(), 12);

    SUBCASE("and each is writable on its own") {
        session->set_max_future_action_ticks(3);
        NETW_CHECK_EQ(session->get_max_future_action_ticks(), 3);
        NETW_CHECK_EQ(session->get_input_gate_deadline_ticks(), 12);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a session with no peer connected lists "
    "no participants at all"
) {
    Ref<NetwMultiplayer> session = make_session();

    NETW_CHECK_EQ(session->get_connected_participants().size(), 0);
}

} // namespace TestNetwSessionPropertySurface

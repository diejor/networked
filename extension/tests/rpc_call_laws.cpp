#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwRpcCallLaws {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] RC1 a call aimed at a node the session "
    "holds no entity for is charged to the unroutable drop alone, so a "
    "missing entity never reads as a missing route"
) {
    Ref<NetwMultiplayer> session = hosting();
    NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(0));
    NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));

    Node *stray = memnew(Node);
    session->rpc_call(Callable(stray, StringName("get_name")), Array(), 0);

    NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(1));
    NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));

    SUBCASE("and a call with no object at all is the same drop") {
        session->rpc_call(Callable(), Array(), 0);
        NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(2));
        NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));
    }

    SUBCASE("and a reported route drop is charged to the other counter") {
        session->rpc_note_dropped_not_live();
        NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(1));
        NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(1));
    }

    memdelete(stray);
}

TEST_CASE(
    "[Networked][Session][Hosted] RC2 the sender of the frame under dispatch "
    "is the session's own fact, so a handler reached through the carrier "
    "reads it without the session owning a script to hold it"
) {
    Ref<NetwMultiplayer> session = hosting();
    NETW_CHECK_EQ(session->rpc_get_relay_sender(), int64_t(0));

    session->set_relay_sender(7);
    NETW_CHECK_EQ(session->rpc_get_relay_sender(), int64_t(7));

    SUBCASE("and it restores to nothing when the dispatch unwinds") {
        session->set_relay_sender(0);
        NETW_CHECK_EQ(session->rpc_get_relay_sender(), int64_t(0));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] RC3 a component call refuses an entity "
    "this session holds no component node for, and charges no drop for it, "
    "because a call that was never addressed was never sent"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();

    NETW_CHECK_EQ(
        int(session->entity_call(RID(), 0, StringName("go"), Array(), 0)),
        int(ERR_DOES_NOT_EXIST)
    );

    SUBCASE("an entity the record plane knows but nothing owns is the same") {
        const RID handle = session->get_liveness_core()->entity_create();
        NETW_CHECK_EQ(
            int(session->entity_call(handle, 0, StringName("go"), Array(), 0)),
            int(ERR_DOES_NOT_EXIST)
        );
    }

    NETW_CHECK_EQ(session->rpc_sends_dropped_unroutable(), int64_t(0));
    NETW_CHECK_EQ(session->rpc_sends_dropped_not_live(), int64_t(0));
}

} // namespace TestNetwRpcCallLaws

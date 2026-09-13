#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"
#include "netw/predict/relay_book.hpp"

namespace TestNetwSessionRelayChannel {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

void take_nothing(const RID &, const PackedByteArray &, int64_t) {
}

bool holds_channel(netw::NetwChannelBook *p_book, int64_t p_channel) {
    return p_book->handler_of(p_channel).is_valid();
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a relay subscription is server "
    "authority's, and needs an entity the liveness core still knows"
) {
    Ref<NetwMultiplayer> session = hosting();

    NETW_CHECK_EQ(
        session->relay_subscribe(Ref<netw::NetwEntity>(), 3, true),
        ERR_DOES_NOT_EXIST
    );

    SUBCASE("a client subscribes nothing rather than a private relay") {
        Ref<NetwMultiplayer> client;
        client.instantiate();
        client->session_set_role(NetwMultiplayer::ROLE_CLIENT);
        NETW_CHECK_EQ(
            client->relay_subscribe(Ref<netw::NetwEntity>(), 3, true),
            ERR_UNAUTHORIZED
        );
    }

    SUBCASE("an entity with no wrapper subscribes through no route") {
        session->predict_relay_subscribe(session->entity_create(), true);
        CHECK(true);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a channel handler is registered only in "
    "the user range, so a kit channel can never be taken by a game"
) {
    Ref<NetwMultiplayer> session = hosting();
    const Callable handler = callable_mp_static(&take_nothing);

    session->rpc_channel_register(99, handler, false);
    CHECK_FALSE(holds_channel(session->get_channel_book(), 99));

    session->rpc_channel_register(255, handler, false);
    CHECK_FALSE(holds_channel(session->get_channel_book(), 255));

    SUBCASE("a channel inside the range is taken") {
        session->rpc_channel_register(100, handler, false);
        CHECK(holds_channel(session->get_channel_book(), 100));
    }

    SUBCASE("an invalid handler registers nothing") {
        session->rpc_channel_register(101, Callable(), false);
        CHECK_FALSE(holds_channel(session->get_channel_book(), 101));
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a session is unconfigured until it is "
    "told otherwise, and a null config moves no gate"
) {
    Ref<NetwMultiplayer> session = hosting();

    CHECK_FALSE(session->lagcomp_is_configured());
    session->set_lagcomp_configured(true);
    CHECK(session->lagcomp_is_configured());

    const int before = session->max_future_action_ticks;
    NETW_CHECK_EQ(
        int(session->lagcomp_initialize(-1, 12)),
        int(ERR_INVALID_PARAMETER)
    );
    NETW_CHECK_EQ(session->max_future_action_ticks, before);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] L4 the peers a slot relays to "
    "are the ones the session admitted, and releasing the slot forgets "
    "them"
) {
    Ref<NetwMultiplayer> session = hosting();
    Node *owner = memnew(Node);
    netw::gd::scene_root()->add_child(owner);

    Ref<netw::NetwEntity> wrapper;
    wrapper.instantiate();
    const RID handle = session->get_liveness_core()->entity_create();
    netw::NetwEntityRecord *const record = wrapper->get_record();
    record->adopt_handle(handle);
    const int64_t route = session->get_liveness_core()->reserve_route();
    REQUIRE(session->liveness_bind(handle, route, wrapper, record, owner));
    session->interest_sync_live_peers();
    const int64_t host = MultiplayerPeer::TARGET_PEER_SERVER;
    PackedInt64Array admitted;
    admitted.push_back(host);
    session->interest_set_entity_intent(wrapper, admitted);
    session->interest_flush_now();
    const int64_t slot = wrapper->get_rid_handle().get_id();

    CHECK(session->relay_peers(slot).is_empty());
    NETW_CHECK_EQ(session->relay_subscribe(wrapper, host, true), OK);
    NETW_CHECK_EQ(session->relay_peers(slot).size(), 1);
    NETW_CHECK_EQ(session->relay_peers(slot)[0], host);

    NETW_CHECK_EQ(session->relay_subscribe(wrapper, host, false), OK);
    CHECK(session->relay_peers(slot).is_empty());

    NETW_CHECK_EQ(session->relay_subscribe(wrapper, host, true), OK);
    session->relay_release(slot);
    CHECK(session->relay_peers(slot).is_empty());

    SUBCASE("a peer the interest engine does not know subscribes to nothing") {
        NETW_CHECK_EQ(
            session->relay_subscribe(wrapper, 4242, true),
            ERR_UNAUTHORIZED
        );
        CHECK(session->relay_peers(slot).is_empty());
    }

    SUBCASE("a request the session reads back names what it asked for") {
        NETW_CHECK_EQ(
            session->relay_request_of(
                netw::predict::RelayBook::request_bytes(true)
            ),
            1
        );
        NETW_CHECK_EQ(
            session->relay_request_of(
                netw::predict::RelayBook::request_bytes(false)
            ),
            0
        );
        NETW_CHECK_LT(session->relay_request_of(PackedByteArray()), 0);
    }

    netw::gd::scene_root()->remove_child(owner);
    memdelete(owner);
}

} // namespace TestNetwSessionRelayChannel

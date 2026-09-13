#include "support/auth_stand.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"

namespace TestNetwSessionPreparedJoin {

using namespace godot;
using netw::LocalMultiplayerPeer;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw_test::CallLog;
using netw_test::NetwTestAuthFlow;

Ref<NetwMultiplayer> a_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

int64_t missing_join_warnings(const Ref<NetwMultiplayer> &p_session) {
    return netw::session_missing_local_join_warnings(p_session.ptr());
}

TEST_CASE(
    "[Networked][Session][Hosted] PJ1 a client that reaches a session submits "
    "the join it prepared exactly once, because entering spends the "
    "preparation rather than reading it, so a reconnect on the same session "
    "submits nothing until a fresh join is prepared"
) {
    const Ref<NetwMultiplayer> session = a_session();
    NETW_CHECK_EQ(
        int(session->session_prepare_join(StringName("ana"), Array())
                ->get_result()),
        int(OK)
    );
    REQUIRE(session->session_prepared_join().has_value());
    CHECK(session->session_prepared_join()->username == StringName("ana"));

    CallLog log;
    session->connect(
        StringName("session_join_submitted"),
        log.callable("submitted")
    );

    session->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    session->session_announce_entered();

    NETW_CHECK_EQ(log.count("submitted"), 1);
    CHECK_FALSE(session->session_prepared_join().has_value());

    session->session_announce_entered();
    NETW_CHECK_EQ(log.count("submitted"), 1);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] PJ2 a listen server holds its prepared join "
    "until its startup scenes exist and then submits it exactly once, because "
    "a host's own player cannot be seated into scenes that have not spawned"
) {
    const Ref<NetwMultiplayer> session = a_session();
    session->session_prepare_join(StringName("ana"), Array());

    CallLog log;
    session->connect(
        StringName("session_join_submitted"),
        log.callable("submitted")
    );

    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    session->session_announce_entered();

    NETW_CHECK_EQ(log.count("submitted"), 0);
    REQUIRE(session->session_prepared_join().has_value());
    CHECK(session->session_prepared_join()->username == StringName("ana"));

    session->emit_signal(StringName("scene_startup_spawned"));

    NETW_CHECK_EQ(log.count("submitted"), 1);
    CHECK_FALSE(session->session_prepared_join().has_value());

    session->emit_signal(StringName("scene_startup_spawned"));
    NETW_CHECK_EQ(log.count("submitted"), 1);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] PJ3 falling offline discards the prepared "
    "join, so a bring-up that was cancelled never submits a stale identity on "
    "the next session this one enters"
) {
    const Ref<NetwMultiplayer> session = a_session();
    session->session_prepare_join(StringName("ana"), Array());
    CHECK(session->session_prepared_join().has_value());

    session->session_transition(NetwMultiplayer::SESSION_STATE_CONNECTING);
    session->session_transition(NetwMultiplayer::SESSION_STATE_OFFLINE);

    CHECK_FALSE(session->session_prepared_join().has_value());

    CallLog log;
    session->connect(
        StringName("session_join_submitted"),
        log.callable("submitted")
    );
    session->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    session->session_announce_entered();
    NETW_CHECK_EQ(log.count("submitted"), 0);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] PJ4 a join with no username prepares "
    "nothing, because the name IS the request and no other field could stand "
    "in for one"
) {
    const Ref<NetwMultiplayer> session = a_session();
    NETW_CHECK_EQ(
        int(session->session_prepare_join(StringName(), Array())->get_result()),
        int(ERR_INVALID_PARAMETER)
    );
    CHECK_FALSE(session->session_prepared_join().has_value());

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] W1 a session that enters with no prepared "
    "join, no submitted request and no local participant warns once, "
    "because an ordinary assignment with nothing arranged for the local "
    "player is a state a developer should be told about"
) {
    const Ref<NetwMultiplayer> session = a_session();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    session->session_announce_entered();

    NETW_CHECK_EQ(missing_join_warnings(session), 1);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] W2 a dedicated server never warns for "
    "having no local player, because it deliberately has none"
) {
    const Ref<NetwMultiplayer> session = a_session();
    session->session_set_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    session->session_announce_entered();

    NETW_CHECK_EQ(missing_join_warnings(session), 0);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] W3 a session that enters while its join "
    "preparation is still in flight does not warn, because the local player "
    "is arranged and only waiting on the flow to settle"
) {
    Ref<NetwPromise> pending;
    pending.instantiate();
    Ref<NetwTestAuthFlow> flow;
    flow.instantiate();
    flow->set_prepared(pending);

    const Ref<NetwMultiplayer> session = a_session();
    session->auth_set_flow(flow);
    session->session_prepare_join(StringName("ana"), Array());
    REQUIRE_FALSE(session->session_prepared_join().has_value());

    session->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    session->session_announce_entered();

    NETW_CHECK_EQ(missing_join_warnings(session), 0);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] W4 a session that enters and submits its "
    "prepared join does not warn while that request is still awaiting "
    "admission, because the local player is already on its way in"
) {
    Ref<LocalMultiplayerPeer> client;
    client.instantiate();
    NETW_CHECK_EQ(int(client->create_client(7)), int(OK));

    const Ref<NetwMultiplayer> session = a_session();
    session->NETW_API_VIRTUAL(set_multiplayer_peer)(client);
    NETW_CHECK_EQ(int64_t(session->NETW_API_VIRTUAL(get_unique_id)()), 7);

    session->session_prepare_join(StringName("ana"), Array());
    REQUIRE(session->session_prepared_join().has_value());

    session->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    session->session_announce_entered();

    CHECK_FALSE(session->session_prepared_join().has_value());
    CHECK(session->participant_local().is_null());
    NETW_CHECK_EQ(missing_join_warnings(session), 0);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] W5 a session that already seated its local "
    "participant before entering never warns, because the local player is "
    "already present"
) {
    const Ref<NetwMultiplayer> session = a_session();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    session->session_submit_join(StringName("ana"), Array());
    REQUIRE(session->participant_local().is_valid());

    session->session_announce_entered();

    NETW_CHECK_EQ(missing_join_warnings(session), 0);

    session->embed_dispose();
}

TEST_CASE(
    "[Networked][Session][Hosted] W6 a client that assigned a peer with no "
    "player arranged is warned once and is left exactly as connected as it "
    "was, because the guidance is diagnostics and a warning that took the "
    "peer away would be the defect it warns about"
) {
    Ref<LocalMultiplayerPeer> client;
    client.instantiate();
    NETW_CHECK_EQ(int(client->create_client(7)), int(OK));

    const Ref<NetwMultiplayer> session = a_session();
    session->NETW_API_VIRTUAL(set_multiplayer_peer)(client);
    session->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    REQUIRE_FALSE(session->session_prepared_join().has_value());

    session->session_announce_entered();

    NETW_CHECK_EQ(missing_join_warnings(session), 1);
    CHECK(
        session->NETW_API_VIRTUAL(get_multiplayer_peer)()
        == Ref<MultiplayerPeer>(client)
    );
    NETW_CHECK_EQ(
        int(session->session_get_role()),
        int(NetwMultiplayer::ROLE_CLIENT)
    );
    CHECK(
        int(session->session_get_state())
        != int(NetwMultiplayer::SESSION_STATE_OFFLINE)
    );
    CHECK(session->participant_local().is_null());

    session->embed_dispose();
}

} // namespace TestNetwSessionPreparedJoin

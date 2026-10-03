#include "support/netw_test.h"

#include "support/netw_recorder.h"

#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session.hpp"

namespace TestNetwSessionDoor {

using namespace godot;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwSession;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D1 one session mints one door and "
    "keeps it, so a game holding it across an await holds the same state"
) {
    Ref<NetwMultiplayer> session = make_session();

    const Ref<NetwSession> door = session->get_session();
    REQUIRE(door.is_valid());
    CHECK(door == session->get_session());

    SUBCASE("a second session mints its own door rather than sharing one") {
        Ref<NetwMultiplayer> other = make_session();
        CHECK(other->get_session() != door);
    }
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D2 every read on the door answers "
    "what the flat verb answers, because the flat verb is the spelling"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwSession> door = session->get_session();

    NETW_CHECK_EQ(door->get_role(), int64_t(session->session_get_role()));
    NETW_CHECK_EQ(door->get_is_online(), session->is_online());
    NETW_CHECK_EQ(door->get_is_local_client(), session->is_local_client());
    CHECK(door->get_root() == session->session_root());
    CHECK(
        door->get_config()->get_app_id()
        == session->session_get_config()->get_app_id()
    );
    CHECK(door->get_config() != session->session_get_config());
    NETW_CHECK_EQ(
        door->get_players().size(),
        session->player_joined_all().size()
    );
    CHECK(door->get_local_player() == session->player_local());
    NETW_CHECK_EQ(door->get_stats().size(), session->stats_snapshot().size());
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D3 the door answers scene views "
    "rather than the internal handles the scene book keys its rows by"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwSession> door = session->get_session();

    const TypedArray<netw::NetwScene> scenes = door->get_scenes();
    NETW_CHECK_EQ(scenes.size(), session->scene_list().size());
    for (int at = 0; at < scenes.size(); ++at) {
        NETW_CHECK_EQ(int(scenes[at].get_type()), int(Variant::OBJECT));
    }
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D4 the door re-emits the session's "
    "own edges, so one object answers every cause a session has to announce"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwSession> door = session->get_session();

    netw_test::Recorder heard(
        door.ptr(),
        {
            StringName("entered"),
            StringName("exiting"),
            StringName("player_joined"),
            StringName("local_joined"),
        }
    );

    session->session_announce_entered();
    session->session_announce_ended();

    NETW_CHECK_EQ(heard.count(StringName("entered")), 1);
    NETW_CHECK_EQ(heard.count(StringName("exiting")), 1);
    NETW_CHECK_EQ(
        int(heard.args(StringName("exiting"))[0]),
        int(netw::NetwSession::CONNECTION_LOST)
    );

    SUBCASE("a player edge carries the row it is about") {
        Ref<netw::NetwPlayer> who;
        who.instantiate();
        door->announce_player_joined(who);
        door->announce_local_joined(who);

        NETW_CHECK_EQ(heard.count(StringName("player_joined")), 1);
        NETW_CHECK_EQ(heard.count(StringName("local_joined")), 1);
        NETW_CHECK_EQ(
            int(heard.args(StringName("local_joined"))[0].get_type()),
            int(Variant::OBJECT)
        );
    }
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D6 a door outliving its session "
    "answers empty rather than dereferencing what is gone"
) {
    Ref<NetwSession> door;
    {
        Ref<NetwMultiplayer> session = make_session();
        door = session->get_session();
    }

    NETW_CHECK_EQ(door->get_role(), int64_t(NetwMultiplayer::ROLE_NONE));
    CHECK_FALSE(door->get_is_online());
    CHECK_FALSE(door->get_is_local_client());
    CHECK(door->get_root() == nullptr);
    CHECK(door->get_config().is_null());
    CHECK(door->get_local_player().is_null());
    NETW_CHECK_EQ(door->get_players().size(), 0);
    NETW_CHECK_EQ(door->get_scenes().size(), 0);
    NETW_CHECK_EQ(door->get_stats().size(), 0);
    CHECK(door->player_of(1).is_null());
    NETW_CHECK_EQ(
        int(door->bucket_of(1, Variant()).get_type()),
        int(Variant::NIL)
    );

    SUBCASE("and its verbs refuse through the promise rather than crashing") {
        CHECK(door->leave().is_valid());
        CHECK(door->request_scene(
                      String("res://nowhere.tscn"),
                      NetwMultiplayer::SCENE_CHANGE_SESSION
        )
                  .is_valid());
    }
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D7 Netw.session answers the session "
    "the node reaches, and nothing when the node reaches none"
) {
    Node *orphan = memnew(Node);
    CHECK(Netw::session(orphan).is_null());
    memdelete(orphan);

    CHECK(Netw::session(nullptr).is_null());
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D9 the session hands out the same "
    "clock, connection, layer and service the session core holds, so the "
    "two routes never answer with different objects"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwSession> door = session->get_session();
    REQUIRE(door.is_valid());

    const bool same_clock = door->get_clock() == session->get_clock();
    const bool same_connection
        = door->get_connection() == session->get_connection();
    const bool same_layer = door->interest_layer(StringName("sight"))
        == session->interest_layer(StringName("sight"));
    CHECK(same_clock);
    CHECK(same_connection);
    CHECK(same_layer);
    CHECK(door->get_clock().is_valid());

    Ref<Resource> type;
    type.instantiate();
    Node *service = memnew(Node);
    door->register_service(service, type.ptr());
    const bool found = door->service(type.ptr()) == service;
    CHECK(found);
    door->unregister_service(service, type.ptr());
    CHECK(door->service(type.ptr()) == nullptr);
    memdelete(service);
}

} // namespace TestNetwSessionDoor

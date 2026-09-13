#include "support/netw_test.h"

#include "support/netw_recorder.h"

#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/session_handle.hpp"

namespace TestNetwSessionDoor {

using namespace godot;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwSessionHandle;

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

    const Ref<NetwSessionHandle> door = session->get_session();
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
    const Ref<NetwSessionHandle> door = session->get_session();

    NETW_CHECK_EQ(door->get_role(), int64_t(session->session_get_role()));
    NETW_CHECK_EQ(door->get_is_online(), session->is_online());
    NETW_CHECK_EQ(door->get_is_local_client(), session->is_local_client());
    CHECK(door->get_root() == session->session_root());
    CHECK(
        door->get_config()->get_app_id()
        == session->session_get_config()->get_app_id()
    );
    CHECK(door->get_config() != session->session_get_config());
    CHECK(door->get_auth_flow() == session->auth_effective_flow());
    NETW_CHECK_EQ(
        door->get_participants().size(),
        session->participant_joined_all().size()
    );
    CHECK(door->get_local_participant() == session->participant_local());
    CHECK(door->get_local_player() == session->scene_player_local());
    NETW_CHECK_EQ(door->get_stats().size(), session->stats_snapshot().size());
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D3 the door answers scene views "
    "rather than the internal handles the scene book keys its rows by"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwSessionHandle> door = session->get_session();

    const TypedArray<netw::NetwSceneHandle> scenes = door->get_scenes();
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
    const Ref<NetwSessionHandle> door = session->get_session();

    netw_test::Recorder heard(
        door.ptr(),
        {
            StringName("entered"),
            StringName("ended"),
            StringName("disconnecting"),
            StringName("participant_joined"),
            StringName("local_joined"),
        }
    );

    session->emit_signal(StringName("session_entered"));
    session->emit_signal(StringName("session_ended"));
    session->emit_signal(
        StringName("session_server_disconnecting"),
        String("the host is going down")
    );

    NETW_CHECK_EQ(heard.count(StringName("entered")), 1);
    NETW_CHECK_EQ(heard.count(StringName("ended")), 1);
    NETW_CHECK_EQ(heard.count(StringName("disconnecting")), 1);
    CHECK(
        String(heard.args(StringName("disconnecting"))[0])
        == String("the host is going down")
    );

    SUBCASE("a participant edge carries the row it is about") {
        Ref<netw::NetwParticipant> who;
        who.instantiate();
        session->emit_signal(StringName("participant_joined"), who);
        session->emit_signal(StringName("participant_local_joined"), who);

        NETW_CHECK_EQ(heard.count(StringName("participant_joined")), 1);
        NETW_CHECK_EQ(heard.count(StringName("local_joined")), 1);
        NETW_CHECK_EQ(
            int(heard.args(StringName("local_joined"))[0].get_type()),
            int(Variant::OBJECT)
        );
    }
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D5 the stock disconnect reaches the "
    "door too, because a game pairing it with ended wants one listener"
) {
    Ref<NetwMultiplayer> session = make_session();
    const Ref<NetwSessionHandle> door = session->get_session();

    netw_test::Recorder heard(door.ptr(), {StringName("disconnected")});
    session->emit_signal(StringName("server_disconnected"));

    NETW_CHECK_EQ(heard.count(StringName("disconnected")), 1);
}

TEST_CASE(
    "[Networked][Session][Door][Hosted] D6 a door outliving its session "
    "answers empty rather than dereferencing what is gone"
) {
    Ref<NetwSessionHandle> door;
    {
        Ref<NetwMultiplayer> session = make_session();
        door = session->get_session();
    }

    NETW_CHECK_EQ(door->get_role(), int64_t(NetwMultiplayer::ROLE_NONE));
    CHECK_FALSE(door->get_is_online());
    CHECK_FALSE(door->get_is_local_client());
    CHECK(door->get_root() == nullptr);
    CHECK(door->get_config().is_null());
    CHECK(door->get_auth_flow().is_null());
    CHECK(door->get_local_participant().is_null());
    CHECK(door->get_local_player().is_null());
    NETW_CHECK_EQ(door->get_participants().size(), 0);
    NETW_CHECK_EQ(door->get_scenes().size(), 0);
    NETW_CHECK_EQ(door->get_stats().size(), 0);
    CHECK(door->participant_of(1).is_null());
    NETW_CHECK_EQ(
        int(door->bucket_of(1, Variant()).get_type()),
        int(Variant::NIL)
    );

    SUBCASE("and its verbs refuse through the promise rather than crashing") {
        CHECK(door->leave().is_valid());
        CHECK(door->request_scene(String("res://nowhere.tscn"), 0).is_valid());
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

} // namespace TestNetwSessionDoor

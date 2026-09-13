#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionDisplayRead {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 every display read against an entity "
    "with no runtime answers the absent shape rather than a plausible zero"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(
        session->display_get_param(entity, NetwMultiplayer::DISPLAY_PARAM_ROLE)
            .get_type(),
        Variant::NIL
    );
    NETW_CHECK_EQ(
        session->display_get_value(entity, StringName("position")).get_type(),
        Variant::NIL
    );
    NETW_CHECK_EQ(
        session
            ->display_get_track_stat(
                entity,
                StringName("position"),
                StringName("age")
            )
            .get_type(),
        Variant::NIL
    );

    SUBCASE("the authoring tick answers -1, which no real tick ever is") {
        NETW_CHECK_EQ(session->display_get_tick(entity), -1);
    }

    SUBCASE("snapping a track that has no channel is a no-op") {
        session->display_snap(entity, StringName("position"), Variant());
        CHECK(true);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a participant is answered only for a "
    "peer whose join the roster accepted"
) {
    Ref<NetwMultiplayer> session = make_session();

    CHECK(session->peer_get_participant(7).is_null());
    CHECK(session->peer_get_accepted_join(7).is_null());

    SUBCASE("forgetting a peer nobody knows is a no-op") {
        session->peer_forget(7);
        CHECK(session->peer_get_participant(7).is_null());
    }
}

} // namespace TestNetwSessionDisplayRead

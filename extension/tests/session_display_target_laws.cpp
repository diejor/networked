#include "support/netw_test.h"

#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionDisplayTarget {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a display sink is set only for an entity "
    "the session holds, so a stray RID never parks a callback"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    session->display_set_target_item(entity, RID());
    session->display_set_callback(entity, Callable());

    SUBCASE("with no sink parked, a track write finds nowhere to land") {
        NETW_CHECK_EQ(
            session->display_write_default(
                entity,
                StringName("position"),
                Variant()
            ),
            ERR_DOES_NOT_EXIST
        );
    }

    SUBCASE("an entity nobody minted parks nothing either") {
        session->display_set_callback(RID(), Callable());
        NETW_CHECK_EQ(
            session->display_write_default(RID(), StringName("x"), Variant()),
            ERR_DOES_NOT_EXIST
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a display declaration asks for the "
    "wrapper first, so an entity with none is refused before its arguments"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(
        session->display_declare(
            entity,
            0,
            StringName("position"),
            Ref<netw::NetwInterpolate>()
        ),
        ERR_DOES_NOT_EXIST
    );

    SUBCASE("an empty track name reaches the same refusal") {
        NETW_CHECK_EQ(
            session->display_declare(
                entity,
                0,
                StringName(),
                Ref<netw::NetwInterpolate>()
            ),
            ERR_DOES_NOT_EXIST
        );
    }

    SUBCASE("and an entity nobody minted, likewise") {
        NETW_CHECK_EQ(
            session->display_declare(
                RID(),
                0,
                StringName("position"),
                Ref<netw::NetwInterpolate>()
            ),
            ERR_DOES_NOT_EXIST
        );
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 undeclaring a display drops every sink "
    "the entity parked, so a re-declare starts from nothing"
) {
    Ref<NetwMultiplayer> session = make_session();
    const RID entity = session->entity_create();

    session->display_undeclare(entity);
    NETW_CHECK_EQ(
        session->display_write_default(entity, StringName("x"), Variant()),
        ERR_DOES_NOT_EXIST
    );
}

} // namespace TestNetwSessionDisplayTarget

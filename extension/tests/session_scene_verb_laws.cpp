#include "support/fault_log.h"
#include "support/netw_test.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSessionSceneVerb {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> hosting() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 a scene facet declared before the "
    "wrapper exists is parked, and lands on the wrapper when one arrives"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(session->scene_declare(entity), OK);

    SUBCASE("an entity nobody minted parks nothing") {
        NETW_CHECK_EQ(session->scene_declare(RID()), ERR_DOES_NOT_EXIST);
    }

    SUBCASE("undeclaring parks the other answer") {
        NETW_CHECK_EQ(session->scene_undeclare(entity), OK);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a scene facet is server authority's to "
    "write, so a client peer is refused rather than silently parking one"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][scene] Condition \"!is_host() && "
        "entity_get_route(p_entity) <= 0\" is true. Returning: "
        "ERR_UNAUTHORIZED. A client cannot set the scene of an "
        "entity that has not spawned yet.",
        1
    );
    expected_faults.expect_error(
        "[error][scene] Condition \"!is_host()\" is true. Returning: "
        "RID(). NetwMultiplayer.scene_create runs only on the "
        "server.",
        1
    );

    Ref<NetwMultiplayer> session;
    session.instantiate();
    session->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(session->scene_declare(entity), ERR_UNAUTHORIZED);
    NETW_CHECK_EQ(
        session->scene_create(Variant(), NetwMultiplayer::SCENE_ISOLATION_NONE)
                .is_valid()
            ? 1
            : 0,
        0
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 a session that has spawned no container "
    "answers every scene lookup empty rather than with an unusable handle"
) {
    Ref<NetwMultiplayer> session = hosting();

    CHECK_FALSE(session->scene_find(StringName("arena")).is_valid());
    NETW_CHECK_EQ(session->scene_find_all(StringName("arena")).size(), 0);
    NETW_CHECK_EQ(session->scene_list().size(), 0);
    CHECK_FALSE(session->scene_presented().is_valid());
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 a scene parameter read or written "
    "against an entity with no wrapper answers the absent shape"
) {
    Ref<NetwMultiplayer> session = hosting();
    const RID entity = session->entity_create();

    NETW_CHECK_EQ(
        session->scene_set_param(
            entity,
            NetwMultiplayer::SCENE_PARAM_LABEL,
            StringName("arena")
        ),
        ERR_DOES_NOT_EXIST
    );
    NETW_CHECK_EQ(
        session->scene_get_param(entity, NetwMultiplayer::SCENE_PARAM_LABEL)
            .get_type(),
        Variant::NIL
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L5 a scene recipe that is neither a path "
    "nor null is refused before anything spawns"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][scene] Condition \"recipe_type != Variant::NIL && "
        "recipe_type != Variant::STRING\" is true. Returning: RID()."
        " NetwMultiplayer.scene_create got a int. Pass a scene file"
        " path, or null for an empty scene.",
        1
    );
    expected_faults.expect_error(
        "[error][scene] Condition \"recipe_type != Variant::NIL && "
        "recipe_type != Variant::STRING\" is true. Returning: RID()."
        " NetwMultiplayer.scene_create got a StringName. Pass a "
        "scene file path, or null for an empty scene.",
        1
    );

    Ref<NetwMultiplayer> session = hosting();
    const int before = int(session->scene_list().size());

    CHECK_FALSE(
        session
            ->scene_create(Variant(42), NetwMultiplayer::SCENE_ISOLATION_NONE)
            .is_valid()
    );
    CHECK_FALSE(session
                    ->scene_create(
                        Variant(StringName("res://level.tscn")),
                        NetwMultiplayer::SCENE_ISOLATION_NONE
                    )
                    .is_valid());
    NETW_CHECK_EQ(int(session->scene_list().size()), before);
}

} // namespace TestNetwSessionSceneVerb

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
    CHECK_FALSE(session->scene_get_current().is_valid());
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

} // namespace TestNetwSessionSceneVerb

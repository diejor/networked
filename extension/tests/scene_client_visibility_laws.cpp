#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestSceneClientVisibilityLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayer;

TEST_CASE(
    "[Networked][Scene] CV1 a client answers what it sees out of the "
    "projection it was told rather than out of the bitmask only the server "
    "keeps, so two peers standing in one scene each read the other as seen "
    "without either of them computing an admission"
) {
    LoopbackRig rig(1);
    NetwMultiplayer *client = rig.client(0);

    const EntityDecl pawn = EntityDecl().named(StringName("Pawn")).on_route(11);
    rig.declare_entity(pawn);
    const RID mirror = rig.declare_mirror(0, pawn);
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);

    const Ref<netw::NetwEntity> seen = client->entity_get_view(mirror);
    NETW_CHECK_EQ(int(seen.is_valid()), 1);
    if (seen.is_null()) {
        return;
    }

    CHECK(client->interest_participant_sees(rig.peer_id(0), seen));
    CHECK_FALSE(client->interest_participant_sees(0, seen));
    CHECK_FALSE(client->interest_participant_sees(
        rig.peer_id(0),
        Ref<netw::NetwEntity>()
    ));
}

} // namespace TestSceneClientVisibilityLaws

#endif

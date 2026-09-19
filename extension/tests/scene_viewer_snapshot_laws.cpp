#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/session/frames.hpp"
#include "support/joined_peer.h"

namespace TestSceneViewerSnapshotLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::session::SceneViewersHead;
using netw_test::LoopbackRig;

LocalVector<uint64_t> roster(std::initializer_list<uint64_t> p_members) {
    LocalVector<uint64_t> out;
    for (const uint64_t member : p_members) {
        out.push_back(member);
    }
    return out;
}

SceneViewersHead head_for(
    NetwMultiplayer *p_api,
    int64_t p_route,
    uint64_t p_revision
) {
    SceneViewersHead head;
    head.route = p_route;
    head.epoch = uint64_t(p_api->liveness_route_epoch(p_route));
    head.generation = p_api->session_plane().get_generation();
    head.revision = p_revision;
    return head;
}

TEST_CASE(
    "[Networked][Scene] SV1 a viewer snapshot names one scene and says nothing "
    "about any other, so a peer holding two worlds does not lose the first "
    "when the second publishes its roster"
) {
    LoopbackRig rig(2);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    const RID annex = rig.declare_scene(StringName("Annex"));
    NetwMultiplayer *host = rig.server();

    const int first = rig.peer_id(0);
    const int second = rig.peer_id(1);
    netw_test::seated_peer(host, first, StringName("ana"));
    netw_test::seated_peer(host, second, StringName("bo"));

    REQUIRE(host->scene_admit_peer(arena, first));
    REQUIRE(host->scene_admit_peer(annex, first));
    REQUIRE(host->scene_admit_peer(annex, second));

    CHECK(host->scene_watches(arena, first));
    CHECK(host->scene_watches(annex, first));
    CHECK(host->scene_watches(annex, second));
    CHECK_FALSE(host->scene_watches(arena, second));

    host->scene_release_peer(annex, first);

    CHECK(host->scene_watches(arena, first));
    CHECK_FALSE(host->scene_watches(annex, first));
    CHECK(host->scene_watches(annex, second));
}

TEST_CASE(
    "[Networked][Scene] SV2 a snapshot for a scene this peer does not hold yet "
    "is parked rather than dropped, and the drain seats it when that scene "
    "arrives, because membership and the metadata explaining it are separate "
    "frames and either order has to work"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    NetwMultiplayer *host = rig.server();
    const int peer = rig.peer_id(0);
    netw_test::seated_peer(host, peer, StringName("ana"));
    const int64_t membership = host->player_incarnation(peer);
    const int64_t route = host->scene_route_of(arena);
    REQUIRE(route > 0);

    host->scene_forget_viewers(arena);
    REQUIRE_FALSE(host->scene_watches(arena, peer));

    const int64_t absent = route + 4096;
    host->scene_apply_viewers(
        head_for(host, absent, 1),
        roster({uint64_t(membership)})
    );

    CHECK_FALSE(host->scene_watches(arena, peer));

    host->scene_drain_parked_viewers(arena);

    CHECK_FALSE(host->scene_watches(arena, peer));

    SUBCASE("a snapshot parked at this scene's own route lands on the drain") {
        host->scene_park_viewers_for_test(
            head_for(host, route, 9),
            roster({uint64_t(membership)})
        );
        CHECK_FALSE(host->scene_watches(arena, peer));

        host->scene_drain_parked_viewers(arena);

        CHECK(host->scene_watches(arena, peer));
    }
}

TEST_CASE(
    "[Networked][Scene] SV3 a snapshot no newer than the one already applied "
    "changes nothing, so a pair that arrives out of order settles on the "
    "newer rather than on whichever landed last"
) {
    LoopbackRig rig(2);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    NetwMultiplayer *host = rig.server();

    const int first = rig.peer_id(0);
    const int second = rig.peer_id(1);
    netw_test::seated_peer(host, first, StringName("ana"));
    netw_test::seated_peer(host, second, StringName("bo"));
    const int64_t one = host->player_incarnation(first);
    const int64_t two = host->player_incarnation(second);
    const int64_t route = host->scene_route_of(arena);

    host->scene_apply_viewers(
        head_for(host, route, 7),
        roster({uint64_t(one), uint64_t(two)})
    );
    CHECK(host->scene_watches(arena, first));
    CHECK(host->scene_watches(arena, second));

    host->scene_apply_viewers(head_for(host, route, 6), roster({uint64_t(one)}));

    CHECK(host->scene_watches(arena, first));
    CHECK(host->scene_watches(arena, second));

    host->scene_apply_viewers(head_for(host, route, 8), roster({uint64_t(one)}));

    CHECK(host->scene_watches(arena, first));
    CHECK_FALSE(host->scene_watches(arena, second));
}

TEST_CASE(
    "[Networked][Scene] SV4 a snapshot minted in another session generation "
    "names memberships this session never issued, so it is refused whole "
    "rather than seating whoever happens to hold those numbers now"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    NetwMultiplayer *host = rig.server();
    const int peer = rig.peer_id(0);
    netw_test::seated_peer(host, peer, StringName("ana"));
    const int64_t membership = host->player_incarnation(peer);
    const int64_t route = host->scene_route_of(arena);

    SceneViewersHead stale = head_for(host, route, 1);
    stale.generation += 1;
    host->scene_apply_viewers(stale, roster({uint64_t(membership)}));

    CHECK_FALSE(host->scene_watches(arena, peer));

    host->scene_apply_viewers(
        head_for(host, route, 1),
        roster({uint64_t(membership)})
    );

    CHECK(host->scene_watches(arena, peer));
}

TEST_CASE(
    "[Networked][Scene] SV5 a membership this peer has not been told about is "
    "skipped rather than invented, because the accept that names it is "
    "reliable and a roster is not the place to learn who exists"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    NetwMultiplayer *host = rig.server();
    const int peer = rig.peer_id(0);
    netw_test::seated_peer(host, peer, StringName("ana"));
    const int64_t membership = host->player_incarnation(peer);
    const int64_t route = host->scene_route_of(arena);

    host->scene_apply_viewers(
        head_for(host, route, 1),
        roster({uint64_t(membership), uint64_t(membership) + 500})
    );

    CHECK(host->scene_watches(arena, peer));
    NETW_CHECK_EQ(int(host->scene_get_peers(arena).size()), 1);
}

TEST_CASE(
    "[Networked][Scene] SV6 a scene that retires forgets its roster, so a "
    "route reopened at the same number does not inherit the viewers the "
    "previous scene held"
) {
    LoopbackRig rig(1);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    NetwMultiplayer *host = rig.server();
    const int peer = rig.peer_id(0);
    netw_test::seated_peer(host, peer, StringName("ana"));

    REQUIRE(host->scene_admit_peer(arena, peer));
    CHECK(host->scene_watches(arena, peer));

    host->scene_forget_viewers(arena);

    CHECK_FALSE(host->scene_watches(arena, peer));
}

TEST_CASE(
    "[Networked][Scene] SV7 the roster a peer is handed when it begins holding "
    "a scene is the roster that scene already had, so a late joiner sees the "
    "players who were there rather than only the edges that follow it"
) {
    LoopbackRig rig(2);
    rig.mount();
    const RID arena = rig.declare_scene(StringName("Arena"));
    NetwMultiplayer *host = rig.server();

    const int early = rig.peer_id(0);
    const int late = rig.peer_id(1);
    netw_test::seated_peer(host, early, StringName("ana"));
    netw_test::seated_peer(host, late, StringName("bo"));

    REQUIRE(host->scene_admit_peer(arena, early));
    rig.pump(4);

    REQUIRE(host->scene_admit_peer(arena, late));
    rig.pump(4);

    const PackedInt32Array held = host->scene_get_peers(arena);
    NETW_CHECK_EQ(int(held.size()), 2);
    CHECK(held.has(early));
    CHECK(held.has(late));
}

} // namespace TestSceneViewerSnapshotLaws

#endif

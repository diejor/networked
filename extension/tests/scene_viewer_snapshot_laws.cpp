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

    host->scene_apply_viewers(
        head_for(host, route, 6),
        roster({uint64_t(one)})
    );

    CHECK(host->scene_watches(arena, first));
    CHECK(host->scene_watches(arena, second));

    host->scene_apply_viewers(
        head_for(host, route, 8),
        roster({uint64_t(one)})
    );

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

void cycle_online(NetwMultiplayer *p_api) {
    netw::SessionCore &core = p_api->session_plane();
    core.transition(netw::SessionCore::STATE_DISCONNECTING);
    core.transition(netw::SessionCore::STATE_OFFLINE);
    core.transition(netw::SessionCore::STATE_CONNECTING);
    core.resolve_online(p_api->get_unique_id());
}

uint64_t cycle_server(LoopbackRig &p_rig) {
    NetwMultiplayer *host = p_rig.server();
    const uint64_t first = host->session_plane().get_generation();
    cycle_online(host);
    REQUIRE(
        host->session_get_state() == NetwMultiplayer::SESSION_STATE_ONLINE
    );
    REQUIRE(host->session_plane().get_generation() > first);
    return first;
}

RID seat_fresh_client(LoopbackRig &p_rig) {
    const int fresh = p_rig.add_client();
    p_rig.pump(4);
    p_rig.declare_scene(StringName("Arena"));
    p_rig.pump(4);
    p_rig.enter_scene(StringName("Arena"));
    p_rig.join(fresh, StringName("alice"));
    const RID mirror = p_rig.mirror_scene(fresh, StringName("Arena"));
    p_rig.pump(4);
    REQUIRE(p_rig.server()->scene_admit_peer(
        p_rig.entity_of(StringName("Arena")),
        p_rig.peer_id(fresh)
    ));
    p_rig.pump(8);
    return mirror;
}

TEST_CASE(
    "[Networked][Scene] SV8 a server whose session went online, offline and "
    "online again publishes a roster that a fresh client applies, because "
    "the client checks the list against the server it joined and not "
    "against how often it went online itself"
) {
    LoopbackRig rig(0);
    cycle_server(rig);
    const RID mirror = seat_fresh_client(rig);

    CHECK(rig.client(0)->scene_watches(mirror, rig.peer_id(0)));
}

TEST_CASE(
    "[Networked][Scene] SV9 a roster the server minted in its previous "
    "session is refused by a client of the current one, even when it "
    "arrives late carrying a newer revision"
) {
    LoopbackRig rig(0);
    const uint64_t previous = cycle_server(rig);
    const RID mirror = seat_fresh_client(rig);
    NetwMultiplayer *client = rig.client(0);
    const int peer = rig.peer_id(0);
    REQUIRE(client->scene_watches(mirror, peer));

    SceneViewersHead stale = rig.server()->scene_viewers_head(
        rig.entity_of(StringName("Arena")),
        64
    );
    stale.generation = previous;
    client->scene_apply_viewers(stale, roster({}));

    CHECK(client->scene_watches(mirror, peer));
}

TEST_CASE(
    "[Networked][Scene] SV10 a roster parked before a client's session ended "
    "seats nobody when its scene arrives in the next session"
) {
    LoopbackRig rig(0);
    const int fresh = rig.add_client();
    rig.pump(4);
    const RID arena = rig.declare_scene(StringName("Arena"));
    rig.pump(4);
    rig.enter_scene(StringName("Arena"));
    rig.join(fresh, StringName("alice"));
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *client = rig.client(fresh);
    const int peer = rig.peer_id(fresh);

    client->scene_park_viewers_for_test(
        host->scene_viewers_head(arena, 1),
        roster({uint64_t(host->player_incarnation(peer))})
    );
    cycle_online(client);
    const RID mirror = rig.mirror_scene(fresh, StringName("Arena"));

    CHECK_FALSE(client->scene_watches(mirror, peer));
}

} // namespace TestSceneViewerSnapshotLaws

#endif

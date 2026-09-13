#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/packed_scene.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/promise.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"
#include "support/netw_recorder.h"

namespace TestNetwSceneNodeTierLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwSceneCore;
using netw_test::CallLog;
using netw_test::Recorder;

struct Mounted {
    Node *root = nullptr;
    Ref<netw::NetwEntity> entity;
    RID scene;
};

Ref<PackedScene> a_packed(const char *p_stem, const char *p_path) {
    Node *root = memnew(Node);
    root->set_name(p_stem);
    Ref<PackedScene> packed;
    packed.instantiate();
    packed->pack(root);
    if (p_path != nullptr) {
        packed->set_path(p_path);
    }
    memdelete(root);
    return packed;
}

Mounted mount(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_root,
    const StringName &p_stem,
    bool p_owns_its_world
) {
    Mounted made;
    made.root = p_root;
    if (!String(p_stem).is_empty()) {
        made.root->set_name(p_stem);
    }
    made.entity.instantiate();
    made.entity->attach_to(made.root);
    made.entity->set_scene_isolation(
        p_owns_its_world ? int64_t(NetwSceneCore::ISOLATION_OWN_WORLD)
                         : int64_t(NetwSceneCore::ISOLATION_NONE)
    );
    made.scene = p_core->get_liveness_core()->entity_create();
    made.entity->get_record()->adopt_handle(made.scene);
    REQUIRE(p_core->entity_of(made.root) == made.scene);
    p_core->get_scene_core()->scene_enter(made.scene, p_stem, p_owns_its_world);
    REQUIRE(p_core->scene_container(p_stem) == made.root);
    return made;
}

Mounted mount_plain(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const StringName &p_stem
) {
    Node *root = memnew(Node);
    if (p_parent != nullptr) {
        p_parent->add_child(root);
    }
    return mount(p_core, root, p_stem, false);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT1 an unwrapped scene root is its own outer "
    "node, so nothing above it competes for the scene's identity"
) {
    Node *arena = memnew(Node);
    arena->set_name(StringName("Arena"));

    NETW_CHECK_EQ(NetwMultiplayer::scene_outer_of(arena), arena);
    NETW_CHECK_EQ(NetwMultiplayer::scene_outer_of(nullptr), nullptr);
    NETW_CHECK_EQ(NetwMultiplayer::scene_world_of(arena), nullptr);
    NETW_CHECK_EQ(NetwMultiplayer::scene_inner_of(arena), arena);

    Node *plain = memnew(Node);
    plain->add_child(arena);
    NETW_CHECK_EQ(NetwMultiplayer::scene_outer_of(arena), arena);
    NETW_CHECK_EQ(NetwMultiplayer::scene_inner_of(plain), plain);

    memdelete(plain);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT2 scene_containing answers the container "
    "the session MOUNTED, and answers null for a node in no scene at all"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    Node *deep = memnew(Node);
    arena.root->add_child(deep);

    NETW_CHECK_EQ(core->scene_containing(deep), arena.root);
    NETW_CHECK_EQ(core->scene_containing(arena.root), arena.root);
    NETW_CHECK_EQ(core->scene_containing(arena.root), arena.root);

    Node *outside = memnew(Node);
    root->add_child(outside);
    NETW_CHECK_EQ(core->scene_containing(outside), nullptr);
    NETW_CHECK_EQ(core->scene_containing(root), nullptr);
    NETW_CHECK_EQ(core->scene_containing(nullptr), nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT3 a scene the book stopped holding live is "
    "no longer what scene_containing answers, so a retired container stops "
    "standing in for its own subtree"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    NETW_CHECK_EQ(core->scene_containing(arena.root), arena.root);

    core->get_scene_core()->scene_exit(arena.scene);

    NETW_CHECK_EQ(core->scene_containing(arena.root), nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] SNT7 destroying takes the "
    "container out of the tree now, and retiring at the same zero window "
    "leaves it mounted, so the two take-downs are not one verb at two "
    "linger values"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Mounted annex = mount_plain(core, root, StringName("Annex"));

    core->get_scene_core()->scene_retire(arena.scene, 0);
    core->scene_settle_refresh();
    NETW_CHECK_EQ(arena.root->get_parent(), root);
    CHECK_FALSE(arena.root->is_queued_for_deletion());
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 1);

    CHECK(core->scene_destroy(annex.scene));
    NETW_CHECK_EQ(annex.root->get_parent(), nullptr);
    CHECK(annex.root->is_queued_for_deletion());

    CHECK_FALSE(core->scene_destroy(RID()));

    root->remove_child(arena.root);
    arena.root->queue_free();
    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted][SceneTree] SNT8 a drain window is counted in "
    "pumps, and the pump that closes it is the one that frees the container "
    "the record plane names"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Mounted slow = mount(core, memnew(Node), StringName("Arena"), false);
    const Mounted quick = mount(core, memnew(Node), StringName("Annex"), false);

    core->get_scene_core()->scene_retire(slow.scene, 2);
    core->get_scene_core()->scene_retire(quick.scene, 1);
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 2);

    core->scene_pump_retired();

    CHECK(quick.root->is_queued_for_deletion());
    CHECK_FALSE(slow.root->is_queued_for_deletion());
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 1);

    core->scene_pump_retired();

    CHECK(slow.root->is_queued_for_deletion());
    NETW_CHECK_EQ(core->get_scene_core()->retiring_scenes().size(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT9 a scene edge takes the scene out of the "
    "live book and DEFERS the refresh to the settle, so a cascade of edges "
    "re-reads the presented scene once rather than once each"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Mounted annex = mount_plain(core, root, StringName("Annex"));
    const StringName refresh_key("scene-refresh-current");

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 2);
    core->get_scene_core()->set_current_scene(arena.scene);

    core->scene_forget(arena.root);
    core->get_scene_core()->scene_retire(annex.scene, 4);
    core->scene_settle_refresh();

    NETW_CHECK_EQ(core->get_scene_core()->live_count(), 0);
    NETW_CHECK_EQ(core->scene_container(StringName("Arena")), nullptr);
    NETW_CHECK_EQ(arena.root->get_parent(), root);
    NETW_CHECK_EQ(annex.root->get_parent(), root);
    CHECK(core->settle_has_key(refresh_key));
    CHECK(core->get_scene_core()->get_current_scene() == arena.scene);

    core->settle_drain();

    CHECK_FALSE(core->settle_has_key(refresh_key));
    CHECK_FALSE(core->get_scene_core()->get_current_scene().is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT10 what a scene DECLARES about its world "
    "and what a viewport actually hosts around it are two readings, and the "
    "host view is owed the second one"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);

    CHECK_FALSE(core->scene_hosts_isolated_world());

    const Mounted declared
        = mount(core, memnew(Node), StringName("Arena"), true);
    root->add_child(declared.root);

    CHECK(core->scene_owns_its_world(declared.scene));
    CHECK_FALSE(core->scene_hosts_isolated_world());

    SubViewport *isolated = memnew(SubViewport);
    isolated->set_meta(NetwMultiplayer::scene_container_meta(), true);
    root->add_child(isolated);
    Node *annex = memnew(Node);
    isolated->add_child(annex);
    const Mounted hosted = mount(core, annex, StringName("Annex"), true);

    CHECK(core->scene_owns_its_world(hosted.scene));
    CHECK(core->scene_hosts_isolated_world());

    CHECK_FALSE(core->scene_owns_its_world(RID()));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT11 an activation announces scene_activated "
    "with the container it made current, exactly once whichever door was "
    "used, and a refusal announces nothing at all"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Ref<PackedScene> live = a_packed("Arena", "res://levels/arena.tscn");
    const Ref<PackedScene> absent = a_packed("Annex", nullptr);
    Recorder activations(
        core.ptr(),
        Vector<StringName>{StringName("scene_activated")}
    );

    NETW_CHECK_EQ(core->scene_activate(live), arena.root);
    NETW_CHECK_EQ(activations.count(StringName("scene_activated")), 1);
    const Array announced = activations.args(StringName("scene_activated"), 0);
    NETW_CHECK_EQ(announced.size(), 1);
    NETW_CHECK_EQ(
        announced.is_empty() ? nullptr : Object::cast_to<Node>(announced[0]),
        arena.root
    );

    NETW_CHECK_EQ(core->scene_activate(live), arena.root);
    NETW_CHECK_EQ(activations.count(StringName("scene_activated")), 2);

    NETW_CHECK_EQ(core->scene_activate(absent), nullptr);
    NETW_CHECK_EQ(core->scene_activate(StringName("Annex")), nullptr);
    NETW_CHECK_EQ(core->scene_activate(Variant()), nullptr);
    NETW_CHECK_EQ(core->scene_activate(root), nullptr);
    NETW_CHECK_EQ(activations.count(StringName("scene_activated")), 2);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT12 resolving a destination answers what is "
    "already live WITHOUT activating it, so a session pointed at the scene it "
    "already presents announces nothing"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    Recorder activations(core.ptr(), Vector<StringName>({"scene_activated"}));

    NETW_CHECK_EQ(
        core->scene_resolve_destination(StringName("Arena")),
        arena.root
    );
    NETW_CHECK_EQ(core->scene_resolve_destination(arena.root), arena.root);
    NETW_CHECK_EQ(activations.count(StringName("scene_activated")), 0);

    NETW_CHECK_EQ(
        core->scene_resolve_destination(StringName("Annex")),
        nullptr
    );
    NETW_CHECK_EQ(core->scene_resolve_destination(root), nullptr);
    NETW_CHECK_EQ(core->scene_resolve_destination(Variant()), nullptr);
    NETW_CHECK_EQ(activations.count(StringName("scene_activated")), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT13 a session change owing nobody a move "
    "lands on the spot, retiring every scene but the destination, and a "
    "destination nothing can bring up CLOSES the transition rather than "
    "leaving it armed against the next change"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Mounted annex = mount_plain(core, root, StringName("Annex"));

    const Ref<netw::NetwPromise> refused = core->scene_apply_change(
        Ref<netw::NetwParticipant>(),
        StringName("Nowhere"),
        NetwMultiplayer::SCENE_CHANGE_SESSION,
        nullptr
    );
    REQUIRE(refused.is_valid());
    CHECK(refused->get_is_failed());
    NETW_CHECK_EQ(arena.root->get_parent(), root);
    NETW_CHECK_EQ(annex.root->get_parent(), root);

    const Ref<netw::NetwPromise> changed = core->scene_apply_change(
        Ref<netw::NetwParticipant>(),
        StringName("Annex"),
        NetwMultiplayer::SCENE_CHANGE_SESSION,
        nullptr
    );
    REQUIRE(changed.is_valid());
    CHECK(changed->get_is_settled());
    NETW_CHECK_EQ(changed->get_code(), int(OK));
    NETW_CHECK_EQ(annex.root->get_parent(), root);
    NETW_CHECK_EQ(arena.root->get_parent(), nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT14 forgetting a container announces "
    "scene_despawned for it after the record plane has dropped it, a null "
    "container is not a departure, and a node the session never held still "
    "departs"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    Recorder departures(
        core.ptr(),
        Vector<StringName>{StringName("scene_despawned")}
    );

    core->scene_forget(nullptr);
    NETW_CHECK_EQ(departures.count(StringName("scene_despawned")), 0);

    core->scene_forget(arena.root);
    NETW_CHECK_EQ(departures.count(StringName("scene_despawned")), 1);
    const Array departed = departures.args(StringName("scene_despawned"), 0);
    NETW_CHECK_EQ(departed.size(), 1);
    NETW_CHECK_EQ(
        departed.is_empty() ? nullptr : Object::cast_to<Node>(departed[0]),
        arena.root
    );
    NETW_CHECK_EQ(core->scene_container(StringName("Arena")), nullptr);

    Node *stranger = memnew(Node);
    root->add_child(stranger);
    core->scene_forget(stranger);
    NETW_CHECK_EQ(departures.count(StringName("scene_despawned")), 2);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT15 server authority entering the session "
    "queues the startup announcement instead of raising it inline, so a "
    "listener installed by the same entry still hears it, and the "
    "announcement reports that startup finished rather than that anything "
    "spawned"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    REQUIRE(core->is_server());
    Recorder passes(
        core.ptr(),
        Vector<StringName>{StringName("scene_startup_spawned")}
    );

    core->scene_on_session_entered();
    NETW_CHECK_EQ(passes.count(StringName("scene_startup_spawned")), 0);

    core->scene_announce_startup();
    NETW_CHECK_EQ(passes.count(StringName("scene_startup_spawned")), 1);

    core->scene_announce_startup();
    NETW_CHECK_EQ(passes.count(StringName("scene_startup_spawned")), 2);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT16 remembering a scene root enters it "
    "under its own name and announces scene_spawned, and a node the session "
    "seated no entity on is refused rather than entered"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);

    Node *arena = memnew(Node);
    arena->set_name(StringName("Arena"));
    root->add_child(arena);
    Ref<netw::NetwEntity> seated;
    seated.instantiate();
    seated->attach_to(arena);
    seated->get_record()->adopt_handle(
        core->get_liveness_core()->entity_create()
    );

    Node *bare = memnew(Node);
    bare->set_name(StringName("Bare"));
    root->add_child(bare);

    Recorder entries(
        core.ptr(),
        Vector<StringName>{StringName("scene_spawned")}
    );

    CHECK_FALSE(core->scene_remember(nullptr));
    CHECK_FALSE(core->scene_remember(bare));
    NETW_CHECK_EQ(entries.count(StringName("scene_spawned")), 0);

    CHECK(core->scene_remember(arena));
    NETW_CHECK_EQ(core->scene_container(StringName("Arena")), arena);
    NETW_CHECK_EQ(entries.count(StringName("scene_spawned")), 1);
    const Array entered = entries.args(StringName("scene_spawned"), 0);
    NETW_CHECK_EQ(entered.size(), 1);
    NETW_CHECK_EQ(
        entered.is_empty() ? nullptr : Object::cast_to<Node>(entered[0]),
        arena
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT17 an arriving move announces "
    "scene_entity_moved with the mover and both ends, resolves the waiting "
    "promise with OK, and still arrives for a mover nobody is awaiting"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));
    const Mounted annex = mount_plain(core, root, StringName("Annex"));
    Recorder moves(
        core.ptr(),
        Vector<StringName>{StringName("scene_entity_moved")}
    );

    Node *body = memnew(Node);
    arena.root->add_child(body);
    Ref<netw::NetwEntity> mover;
    mover.instantiate();
    mover->attach_to(body);

    Ref<netw::NetwPromise> settled;
    settled.instantiate();
    core->scene_arrive(mover, arena.root, annex.root, settled);

    NETW_CHECK_EQ(moves.count(StringName("scene_entity_moved")), 1);
    const Array moved = moves.args(StringName("scene_entity_moved"), 0);
    NETW_CHECK_EQ(moved.size(), 3);
    if (moved.size() == 3) {
        NETW_CHECK_EQ(Object::cast_to<netw::NetwEntity>(moved[0]), mover.ptr());
        NETW_CHECK_EQ(Object::cast_to<Node>(moved[1]), arena.root);
        NETW_CHECK_EQ(Object::cast_to<Node>(moved[2]), annex.root);
    }
    CHECK(settled->get_is_settled());
    NETW_CHECK_EQ(settled->get_code(), int(OK));

    Node *prop = memnew(Node);
    annex.root->add_child(prop);
    Ref<netw::NetwEntity> stranger;
    stranger.instantiate();
    stranger->attach_to(prop);
    core->scene_arrive(stranger, annex.root, arena.root, nullptr);
    NETW_CHECK_EQ(moves.count(StringName("scene_entity_moved")), 2);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT18 a joining participant spends the "
    "admission parked for it against every live scene and seats it there, "
    "and a participant nothing parked for is seated nowhere"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    Ref<netw::NetwParticipant> waiting;
    waiting.instantiate();
    waiting->seat_at(core.ptr(), 9);
    core->participant_adopt(9, waiting);
    core->participant_admit(9);
    CHECK(core->get_scene_core()->admission_park(arena.scene, 9));
    CHECK(core->get_scene_core()->admission_is_parked(arena.scene, 9));

    core->scene_on_participant_joined(waiting);

    CHECK_FALSE(core->get_scene_core()->admission_is_parked(arena.scene, 9));
    CHECK(core->participant_seat(9) == arena.scene);

    Ref<netw::NetwParticipant> stranger;
    stranger.instantiate();
    stranger->seat_at(core.ptr(), 11);
    core->participant_adopt(11, stranger);
    core->participant_admit(11);

    core->scene_on_participant_joined(stranger);

    CHECK_FALSE(core->participant_seat(11).is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT19 opening a scene's admission on a server "
    "seats every peer its boundary already admits, opens the row once so a "
    "second open reports nothing, and closing it clears the seats it wrote"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);
    const Mounted arena = mount_plain(core, root, StringName("Arena"));

    REQUIRE(core->scene_admit_peer(arena.scene, 7));
    Ref<netw::NetwParticipant> seated;
    seated.instantiate();
    seated->seat_at(core.ptr(), 7);
    core->participant_adopt(7, seated);
    core->participant_admit(7);
    REQUIRE_FALSE(core->participant_seat(7).is_valid());

    core->scene_open_admission(arena.root);

    CHECK(core->participant_seat(7) == arena.scene);

    core->participant_seat_clear(7, arena.scene);
    core->scene_open_admission(arena.root);

    CHECK_FALSE(core->participant_seat(7).is_valid());

    core->scene_report_participant(arena.scene, 7, true);
    REQUIRE(core->participant_seat(7) == arena.scene);

    core->scene_close_admission(arena.root);

    CHECK_FALSE(core->participant_seat(7).is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SNT20 adopting a reparented entity admits its "
    "peer to the scene that now encloses it, and admits nobody for a prop, "
    "for an entity already its own scene, or on a peer that is not authority"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    const CallLog flushed;
    core->set_interest_flush(flushed.callable("flush"));
    Node *root = memnew(Node);

    Node *arena = memnew(Node);
    arena->set_name(StringName("Arena"));
    root->add_child(arena);
    Ref<netw::NetwEntity> arena_wrapper;
    arena_wrapper.instantiate();
    arena_wrapper->attach_to(arena);
    arena_wrapper->set_declares_scene(true);
    const RID scene = core->get_liveness_core()->entity_create();
    arena_wrapper->get_record()->adopt_handle(scene);
    REQUIRE(core->entity_of(arena) == scene);
    REQUIRE(core->liveness_bind(
        scene,
        61,
        arena_wrapper,
        arena_wrapper->get_record(),
        arena
    ));

    Node *body = memnew(Node);
    arena->add_child(body);
    Ref<netw::NetwEntity> player;
    player.instantiate();
    player->attach_to(body);
    const RID mover = core->get_liveness_core()->entity_create();
    player->get_record()->adopt_handle(mover);
    REQUIRE(core->entity_of(body) == mover);
    REQUIRE(core->liveness_bind(mover, 62, player, player->get_record(), body));
    const int64_t host = MultiplayerPeer::TARGET_PEER_SERVER;
    player->set_peer_id(host);

    REQUIRE(core->scene_of(mover) == scene);
    CHECK_FALSE(core->scene_admits(scene, host));

    core->scene_adopt_entity(mover);

    CHECK(core->scene_admits(scene, host));

    SUBCASE("a prop carrying no peer is admitted nowhere") {
        core->scene_release_peer(scene, host);
        player->set_peer_id(0);

        core->scene_adopt_entity(mover);

        CHECK_FALSE(core->scene_admits(scene, host));
    }

    SUBCASE("an entity that is its own scene is already where it belongs") {
        core->scene_release_peer(scene, host);
        arena_wrapper->set_peer_id(host);

        core->scene_adopt_entity(scene);

        CHECK_FALSE(core->scene_admits(scene, host));
    }

    memdelete(root);
}

#if defined(NETW_TIER_HOSTED)
#endif

} // namespace TestNetwSceneNodeTierLaws

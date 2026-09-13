#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/interest/engine.hpp"
#include "netw/scene_core.hpp"

namespace TestNetwSceneSeatAwarenessLaws {

using namespace godot;
using netw::NetwMultiplayer;

struct Mounted {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *container = nullptr;
};

Ref<netw::NetwParticipant> a_seat_announcing_row() {
    Ref<netw::NetwParticipant> row;
    row.instantiate();
    return row;
}

Mounted mount_scene(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const char *p_stem
) {
    Mounted made;
    made.container = memnew(Node);
    p_parent->add_child(made.container);
    Node *level = memnew(Node);
    level->set_name(p_stem);
    made.container->add_child(level);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record = made.wrapper->get_record();
    made.record->adopt_handle(made.handle);
    made.record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.container
    ));
    p_core->get_scene_core()
        ->scene_enter(made.handle, StringName(p_stem), false);
    return made;
}

void open_participant(const Ref<NetwMultiplayer> &p_core, int64_t p_peer) {
    p_core->participant_adopt(p_peer, a_seat_announcing_row());
    REQUIRE(p_core->participant_has(p_peer));
}

void make_aware(const Ref<NetwMultiplayer> &p_core, const RID &p_scene) {
    const StringName layer = p_core->scene_layer_id(p_scene);
    REQUIRE_FALSE(layer.is_empty());
    REQUIRE(
        p_core->interest_plane().roster_add(layer, int64_t(p_scene.get_id()))
    );
}

void make_unaware(const Ref<NetwMultiplayer> &p_core, const RID &p_scene) {
    const StringName layer = p_core->scene_layer_id(p_scene);
    REQUIRE_FALSE(layer.is_empty());
    REQUIRE(
        p_core->interest_plane().roster_remove(layer, int64_t(p_scene.get_id()))
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] SY1 the seat a client holds is the one its "
    "awareness of the scene container was told, so a peer that has been made "
    "aware of a live scene is seated in it and a peer aware of nothing is "
    "seated nowhere, which is what lets a client hold a seat it computes no "
    "admission for"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_scene(core, root, "Arena");
    const int64_t peer = 7;
    open_participant(core, peer);

    CHECK_FALSE(core->participant_seat(peer).is_valid());
    CHECK_FALSE(core->scene_seat_sync(peer).is_valid());
    CHECK_FALSE(core->participant_seat(peer).is_valid());

    make_aware(core, arena.handle);

    CHECK(core->scene_seat_sync(peer) == arena.handle);
    CHECK(core->participant_seat(peer) == arena.handle);

    CHECK(core->scene_seat_sync(peer) == arena.handle);
    CHECK(core->participant_seat(peer) == arena.handle);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SY2 a scene standing in the tree is not a "
    "seat, so a peer that lost its awareness of a container that is still "
    "live and still mounted infers nothing rather than falling back to "
    "whatever is on screen, and keeps the seat it already holds"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_scene(core, root, "Arena");
    const int64_t peer = 7;
    open_participant(core, peer);

    make_aware(core, arena.handle);
    REQUIRE(core->scene_seat_sync(peer) == arena.handle);

    make_unaware(core, arena.handle);

    const Node *mounted
        = Object::cast_to<Node>(core->wrapper_owner(arena.handle));
    REQUIRE(core->get_scene_core()->is_live(arena.handle));
    REQUIRE(mounted != nullptr);

    CHECK_FALSE(core->scene_seat_sync(peer).is_valid());
    CHECK(core->participant_seat(peer) == arena.handle);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SY3 awareness picks the scene among the live "
    "ones rather than the first, so a peer aware only of the second of two "
    "live scenes is seated there"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_scene(core, root, "Arena");
    const Mounted annex = mount_scene(core, root, "Annex");
    const int64_t peer = 7;
    open_participant(core, peer);

    make_aware(core, annex.handle);

    CHECK(core->scene_seat_sync(peer) == annex.handle);
    CHECK(core->participant_seat(peer) == annex.handle);

    make_aware(core, arena.handle);
    make_unaware(core, annex.handle);

    CHECK(core->scene_seat_sync(peer) == arena.handle);
    CHECK(core->participant_seat(peer) == arena.handle);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SY4 only the scene a stem currently answers "
    "with is read, so awareness of the shadowed sibling of a re-entered stem "
    "seats nobody and a session that rebuilt one level does not seat its "
    "peers in the container it replaced"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted first = mount_scene(core, root, "Arena");
    const Mounted second = mount_scene(core, root, "Arena");
    const int64_t peer = 7;
    open_participant(core, peer);

    const Ref<netw::NetwSceneCore> scenes = core->get_scene_core();
    REQUIRE(scenes->scene_named(StringName("Arena")) == second.handle);
    REQUIRE(
        core->scene_layer_id(first.handle)
        != core->scene_layer_id(second.handle)
    );

    make_aware(core, first.handle);

    CHECK_FALSE(core->scene_seat_sync(peer).is_valid());
    CHECK_FALSE(core->participant_seat(peer).is_valid());

    make_aware(core, second.handle);

    CHECK(core->scene_seat_sync(peer) == second.handle);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SY5 a seat belongs to a participant row, so a "
    "peer the roster never opened is seated nowhere however aware the "
    "session is, and the scene it would have been seated in is not read"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Mounted arena = mount_scene(core, root, "Arena");
    const int64_t stranger = 7;

    make_aware(core, arena.handle);

    CHECK_FALSE(core->participant_has(stranger));
    CHECK_FALSE(core->scene_seat_sync(stranger).is_valid());
    CHECK_FALSE(core->participant_seat(stranger).is_valid());

    memdelete(root);
}

} // namespace TestNetwSceneSeatAwarenessLaws

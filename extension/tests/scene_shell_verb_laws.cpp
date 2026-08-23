#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/wire/registry.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneShellVerbLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::NetwPromise;
using netw_test::CallLog;

struct DeclaredScene {
    Node *container = nullptr;
    Ref<netw::NetwEntity> entity;
    RID handle;
};

DeclaredScene declare_scene(
    const Ref<NetwMultiplayerCore> &p_core,
    const char *p_stem
) {
    DeclaredScene made;
    made.container = memnew(Node);
    Node *level = memnew(Node);
    level->set_name(p_stem);
    made.container->add_child(level);
    made.entity.instantiate();
    made.entity->attach_to(made.container);
    made.handle = p_core->get_liveness_core()->entity_create();
    made.entity->get_record()->adopt_handle(made.handle);
    REQUIRE(p_core->entity_of(made.container) == made.handle);
    return made;
}

struct Placed {
    Ref<RefCounted> wrapper;
    Ref<netw::NetwEntityRecord> record;
    RID handle;
    Node *owner = nullptr;
};

Placed place(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_parent,
    bool p_declares_scene
) {
    Placed made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record.instantiate();
    made.record->adopt_handle(made.handle);
    made.record->set_declares_scene(p_declares_scene);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.owner
    ));
    made.owner->set_meta(NetwMultiplayerCore::wrapper_meta(), made.wrapper);
    return made;
}

int64_t declared_channel_id(const char *p_name) {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    const netw::wire::ChannelDecl *decl
        = registry.find_channel_by_name(StringName(p_name));
    REQUIRE(decl != nullptr);
    return int64_t(decl->id);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV1 the boundary write reports its own "
    "participant edge, once per change and never for a repeat, so every path "
    "that admits somebody announces it without each caller remembering to"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    core->set_scene_participant_edge(seen.callable("edge"));
    const DeclaredScene arena = declare_scene(core, "Arena");

    REQUIRE(core->scene_admit_peer(arena.handle, 7));
    NETW_CHECK_EQ(seen.count("edge"), 1);
    CHECK(core->interest_flush_pending());
    const Array arrival = seen.args("edge", 0);
    NETW_CHECK_EQ(int(arrival.size()), 3);
    CHECK(RID(arrival[0]) == arena.handle);
    NETW_CHECK_EQ(int(arrival[1]), 7);
    CHECK(bool(arrival[2]));

    core->settle_drain();
    CHECK_FALSE(core->scene_admit_peer(arena.handle, 7));
    NETW_CHECK_EQ(seen.count("edge"), 1);

    REQUIRE(core->scene_release_peer(arena.handle, 7));
    NETW_CHECK_EQ(seen.count("edge"), 2);
    const Array departure = seen.args("edge", 1);
    CHECK(RID(departure[0]) == arena.handle);
    NETW_CHECK_EQ(int(departure[1]), 7);
    CHECK_FALSE(bool(departure[2]));

    CHECK_FALSE(core->scene_release_peer(arena.handle, 7));
    NETW_CHECK_EQ(seen.count("edge"), 2);

    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV2 a session with no participant edge "
    "installed still writes the boundary and still requests the flush, so the "
    "announcement is the only thing an uninstalled reporter costs"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    const DeclaredScene arena = declare_scene(core, "Arena");
    netw::InterestEngine &engine = core->interest_plane();

    CHECK(core->scene_admit_peer(arena.handle, 7));
    CHECK(engine.layer_has_viewer(StringName("scene:Arena"), 7));
    CHECK(core->interest_flush_pending());
    core->settle_drain();
    NETW_CHECK_EQ(seen.count("flush"), 1);
    NETW_CHECK_EQ(seen.count("edge"), 0);

    CHECK(core->scene_release_peer(arena.handle, 7));
    CHECK_FALSE(engine.layer_has_viewer(StringName("scene:Arena"), 7));
    CHECK(core->interest_flush_pending());
    core->settle_drain();
    NETW_CHECK_EQ(seen.count("flush"), 2);

    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV3 the release is told to a peer whose seat "
    "names the scene and to nobody else, and this peer is told through the "
    "channel's own protocol handler rather than over the wire"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog seen;
    const DeclaredScene arena = declare_scene(core, "Arena");
    const DeclaredScene annex = declare_scene(core, "Annex");
    core->get_channel_book()->register_protocol(
        declared_channel_id("SESSION_SCENE_RELEASED"),
        seen.callable("released")
    );
    Ref<RefCounted> local;
    local.instantiate();
    core->participant_adopt(int64_t(core->get_unique_id()), local);

    CHECK_FALSE(core->scene_notify_released(arena.handle, 9));
    NETW_CHECK_EQ(seen.count("released"), 0);

    CHECK_FALSE(
        core->scene_notify_released(arena.handle, core->get_unique_id())
    );
    NETW_CHECK_EQ(seen.count("released"), 0);

    REQUIRE(core->participant_take_seat(
        int64_t(core->get_unique_id()),
        annex.handle
    ));
    CHECK_FALSE(
        core->scene_notify_released(arena.handle, core->get_unique_id())
    );
    NETW_CHECK_EQ(seen.count("released"), 0);

    REQUIRE(core->participant_take_seat(
        int64_t(core->get_unique_id()),
        arena.handle
    ));
    CHECK(core->scene_notify_released(arena.handle, core->get_unique_id()));
    NETW_CHECK_EQ(seen.count("released"), 1);
    const Array told = seen.args("released", 0);
    NETW_CHECK_EQ(int(told.size()), 2);
    const PackedByteArray payload = told[0];
    CHECK(
        netw::gd::bytes_to_var(payload)
        == Variant(core->scene_layer_id(arena.handle))
    );
    NETW_CHECK_EQ(int(told[1]), 1);

    memdelete(annex.container);
    memdelete(arena.container);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SV4 a move answers a promise whether or not "
    "it can start, and a reachable one is handed to the installed carry with "
    "that same promise, so nothing about the outcome is left unanswered"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const CallLog seen;
    Node *root = memnew(Node);
    const Placed arena = place(core, root, true);
    const Placed pawn = place(core, root, false);

    const Ref<NetwPromise> unheld
        = core->scene_move_entity(pawn.handle, arena.handle, Variant());
    REQUIRE(unheld.is_valid());
    CHECK(unheld->get_is_failed());
    NETW_CHECK_EQ(unheld->get_code(), int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(seen.count("carry"), 0);

    core->set_scene_carry_move(seen.callable("carry"));

    const Ref<NetwPromise> nowhere
        = core->scene_move_entity(pawn.handle, RID(), Variant());
    REQUIRE(nowhere.is_valid());
    CHECK(nowhere->get_is_failed());
    NETW_CHECK_EQ(nowhere->get_code(), int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(seen.count("carry"), 0);

    const Ref<NetwPromise> moving
        = core->scene_move_entity(pawn.handle, arena.handle, Variant());
    REQUIRE(moving.is_valid());
    CHECK_FALSE(moving->get_is_settled());
    NETW_CHECK_EQ(seen.count("carry"), 1);
    const Array handed = seen.args("carry", 0);
    NETW_CHECK_EQ(int(handed.size()), 4);
    CHECK(Object::cast_to<Object>(handed[0]) == pawn.wrapper.ptr());
    CHECK(Object::cast_to<Node>(handed[1]) == arena.owner);
    CHECK(Object::cast_to<NetwPromise>(handed[3]) == moving.ptr());

    memdelete(root);
}

} // namespace TestNetwSceneShellVerbLaws

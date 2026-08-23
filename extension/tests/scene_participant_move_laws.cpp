#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/wire/registry.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSceneParticipantMoveLaws {

using namespace godot;
using netw::NetwGroupPromise;
using netw::NetwMultiplayerCore;
using netw_test::CallLog;

Ref<NetwMultiplayerCore> peered_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

Ref<RefCounted> a_seat_announcing_row() {
    Ref<RefCounted> row;
    row.instantiate();
    Array args;
    Dictionary from;
    from["name"] = "from";
    from["type"] = int(Variant::OBJECT);
    Dictionary to;
    to["name"] = "to";
    to["type"] = int(Variant::OBJECT);
    args.push_back(from);
    args.push_back(to);
    netw::gd::add_user_signal(row.ptr(), "scene_changed", args);
    return row;
}

Ref<RefCounted> adopt_participant(
    const Ref<NetwMultiplayerCore> &p_core,
    int64_t p_peer
) {
    const Ref<RefCounted> row = a_seat_announcing_row();
    p_core->participant_adopt(p_peer, row);
    p_core->participant_publish_joined(p_peer);
    REQUIRE(p_core->participant_has(p_peer));
    return row;
}

struct Declared {
    Ref<RefCounted> wrapper;
    Ref<netw::NetwEntityRecord> record;
    RID handle;
    Node *owner = nullptr;
};

Declared declare_scene(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_parent,
    const char *p_stem
) {
    Declared made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    Node *level = memnew(Node);
    level->set_name(p_stem);
    made.owner->add_child(level);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record.instantiate();
    made.record->adopt_handle(made.handle);
    made.record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.owner
    ));
    made.owner->set_meta(NetwMultiplayerCore::wrapper_meta(), made.wrapper);
    REQUIRE(!p_core->scene_layer_id(made.handle).is_empty());
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

PackedInt32Array peers_of(int64_t p_only) {
    PackedInt32Array out;
    out.push_back(int32_t(p_only));
    return out;
}

PackedInt32Array peers_of(int64_t p_first, int64_t p_second) {
    PackedInt32Array out = peers_of(p_first);
    out.push_back(int32_t(p_second));
    return out;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP1 moving a participant crosses the seat, "
    "the boundary it leaves and the boundary it reaches as one act, so "
    "nothing can observe a participant seated in one scene while still "
    "admitted to another"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const Declared annex = declare_scene(core, root, "Annex");
    const int64_t peer = 7;
    adopt_participant(core, peer);
    netw::InterestEngine &engine = core->interest_plane();
    const StringName arena_layer = core->scene_layer_id(arena.handle);
    const StringName annex_layer = core->scene_layer_id(annex.handle);

    CHECK_FALSE(engine.layer_has_viewer(arena_layer, peer));

    CHECK(core->participant_move_seat(peer, arena.handle));
    CHECK(core->participant_seat(peer) == arena.handle);
    CHECK(engine.layer_has_viewer(arena_layer, peer));
    CHECK_FALSE(engine.layer_has_viewer(annex_layer, peer));

    CHECK(core->participant_move_seat(peer, annex.handle));
    CHECK(core->participant_seat(peer) == annex.handle);
    CHECK_FALSE(engine.layer_has_viewer(arena_layer, peer));
    CHECK(engine.layer_has_viewer(annex_layer, peer));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP2 a move the session cannot make spends "
    "nothing: a peer the roster never opened, a destination naming no live "
    "container and the scene already held all answer false and leave both "
    "the seat and every boundary exactly as they were"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const int64_t peer = 7;
    const int64_t stranger = 9;
    adopt_participant(core, peer);
    netw::InterestEngine &engine = core->interest_plane();
    const StringName arena_layer = core->scene_layer_id(arena.handle);

    CHECK_FALSE(core->participant_move_seat(stranger, arena.handle));
    CHECK_FALSE(core->participant_seat(stranger).is_valid());
    CHECK_FALSE(engine.layer_has_viewer(arena_layer, stranger));

    CHECK_FALSE(core->participant_move_seat(peer, RID()));
    CHECK_FALSE(core->participant_seat(peer).is_valid());
    CHECK_FALSE(engine.layer_has_viewer(arena_layer, peer));

    CHECK(core->participant_move_seat(peer, arena.handle));
    CHECK(engine.layer_has_viewer(arena_layer, peer));

    CHECK_FALSE(core->participant_move_seat(peer, arena.handle));
    CHECK(core->participant_seat(peer) == arena.handle);
    CHECK(engine.layer_has_viewer(arena_layer, peer));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP3 a moved peer is never told it was "
    "released, because the seat names the destination before the older "
    "scene lets go, and a peer told it was released would drop the scene it "
    "has just been given"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    core->get_channel_book()->register_protocol(
        declared_channel_id("SESSION_SCENE_RELEASED"),
        seen.callable("released")
    );
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const Declared annex = declare_scene(core, root, "Annex");
    const int64_t local = int64_t(core->get_unique_id());
    adopt_participant(core, local);

    CHECK(core->participant_move_seat(local, arena.handle));
    NETW_CHECK_EQ(seen.count("released"), 0);

    CHECK(core->scene_notify_released(arena.handle, local));
    NETW_CHECK_EQ(seen.count("released"), 1);

    CHECK(core->participant_move_seat(local, annex.handle));
    CHECK(core->participant_seat(local) == annex.handle);
    NETW_CHECK_EQ(seen.count("released"), 1);

    CHECK_FALSE(core->scene_notify_released(arena.handle, local));
    NETW_CHECK_EQ(seen.count("released"), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP4 a group move answers a group nobody has "
    "settled yet and reports every peer that was asked for at the drain, "
    "the refused mover included, so a caller subscribes to what it was "
    "handed and hears about each peer it named"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const int64_t peer = 7;
    const int64_t stranger = 9;
    const Ref<RefCounted> row = adopt_participant(core, peer);

    const Ref<NetwGroupPromise> batch
        = core->scene_move_participants(arena.handle, peers_of(peer, stranger));
    REQUIRE(batch.is_valid());
    const CallLog heard;
    batch->connect("completed_single", heard.callable("arrived"));

    CHECK_FALSE(batch->get_is_settled());
    NETW_CHECK_EQ(heard.count("arrived"), 0);
    CHECK(core->participant_seat(peer) == arena.handle);
    CHECK_FALSE(core->participant_seat(stranger).is_valid());

    core->settle_drain();

    CHECK(batch->get_is_completed());
    NETW_CHECK_EQ(heard.count("arrived"), 2);
    const Array arrival = heard.args("arrived", 0);
    NETW_CHECK_EQ(int(arrival.size()), 2);
    NETW_CHECK_EQ(int(arrival[0]), int(peer));
    NETW_CHECK_EQ(Object::cast_to<Object>(arrival[1]), row.ptr());
    const Array refused = heard.args("arrived", 1);
    NETW_CHECK_EQ(int(refused[0]), int(stranger));
    CHECK(Object::cast_to<Object>(refused[1]) == nullptr);
    NETW_CHECK_EQ(int(batch->get_results().size()), 2);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP5 a move of nobody settles at the same "
    "drain every other move settles at, because the last arrival is what "
    "settles a group and a group with no arrivals has none to be last"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");

    const Ref<NetwGroupPromise> batch
        = core->scene_move_participants(arena.handle, PackedInt32Array());
    REQUIRE(batch.is_valid());
    const CallLog heard;
    batch->connect("completed", heard.callable("completed"));

    CHECK_FALSE(batch->get_is_settled());

    core->settle_drain();

    CHECK(batch->get_is_completed());
    CHECK_FALSE(batch->get_is_failed());
    NETW_CHECK_EQ(heard.count("completed"), 1);
    const Dictionary results = heard.args("completed", 0)[0];
    NETW_CHECK_EQ(int(results.size()), 0);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SP6 two moves in one cascade are two groups "
    "and both settle, because the report is scheduled unkeyed: one shared "
    "key would coalesce them and strand the first caller on a promise that "
    "never settles"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const CallLog seen;
    core->set_interest_flush(seen.callable("flush"));
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const Declared annex = declare_scene(core, root, "Annex");
    const int64_t peer = 7;
    adopt_participant(core, peer);
    const PackedInt32Array moving = peers_of(peer);
    const int quiet = core->settle_pending();

    const Ref<NetwGroupPromise> back
        = core->scene_move_participants(annex.handle, moving);
    const Ref<NetwGroupPromise> forward
        = core->scene_move_participants(arena.handle, moving);
    REQUIRE(back.is_valid());
    REQUIRE(forward.is_valid());
    REQUIRE(back.ptr() != forward.ptr());

    CHECK_FALSE(back->get_is_settled());
    CHECK_FALSE(forward->get_is_settled());
    CHECK(core->interest_flush_pending());
    NETW_CHECK_EQ(core->settle_pending() - quiet, 3);

    core->settle_drain();

    CHECK(back->get_is_completed());
    CHECK(forward->get_is_completed());
    NETW_CHECK_EQ(int(back->get_results().size()), 1);
    NETW_CHECK_EQ(int(forward->get_results().size()), 1);
    CHECK(core->participant_seat(peer) == arena.handle);

    memdelete(root);
}

} // namespace TestNetwSceneParticipantMoveLaws

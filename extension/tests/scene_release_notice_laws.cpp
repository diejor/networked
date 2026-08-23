#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSceneReleaseNoticeLaws {

using namespace godot;
using netw::NetwMultiplayerCore;

RID mount(const Ref<NetwMultiplayerCore> &p_core, Node *p_parent) {
    Node *container = memnew(Node);
    p_parent->add_child(container);
    Node *level = memnew(Node);
    level->set_name("Arena");
    container->add_child(level);
    const RID handle = p_core->get_liveness_core()->entity_create();
    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    Ref<netw::NetwEntityRecord> record;
    record.instantiate();
    record->adopt_handle(handle);
    record->set_declares_scene(true);
    REQUIRE(p_core->liveness_bind(
        handle,
        p_core->get_liveness_core()->reserve_route(),
        wrapper,
        record,
        container
    ));
    return handle;
}

PackedByteArray notice(const StringName &p_layer) {
    return netw::gd::var_to_bytes(Variant(p_layer));
}

Ref<NetwMultiplayerCore> seated_core(Node *p_root, RID &r_seat, RID &r_other) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    r_seat = mount(core, p_root);
    r_other = mount(core, p_root);
    Ref<RefCounted> row;
    row.instantiate();
    core->participant_adopt(core->get_unique_id(), row);
    REQUIRE(core->participant_admit(core->get_unique_id()));
    REQUIRE(core->participant_take_seat(core->get_unique_id(), r_seat));
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SL1 a release notice is matched against the "
    "layer of the seat this peer holds, so a session running two instances of "
    "one level releases the peer from the instance the server named and not "
    "from whichever one shares its stem"
) {
    Node *root = memnew(Node);
    RID seat;
    RID other;
    const Ref<NetwMultiplayerCore> core = seated_core(root, seat, other);

    const StringName held = core->scene_layer_id(seat);
    const StringName elsewhere = core->scene_layer_id(other);
    REQUIRE(held != StringName());
    REQUIRE(held != elsewhere);

    CHECK(core->scene_released_seat(notice(held), 1) == seat);
    CHECK_FALSE(core->scene_released_seat(notice(elsewhere), 1).is_valid());
    CHECK_FALSE(
        core->scene_released_seat(notice(StringName("scene:Arena")), 1)
            .is_valid()
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SL2 only the server releases a seat, so a peer "
    "forging a notice cannot evict another peer from the scene it is sitting in"
) {
    Node *root = memnew(Node);
    RID seat;
    RID other;
    const Ref<NetwMultiplayerCore> core = seated_core(root, seat, other);
    const PackedByteArray honest = notice(core->scene_layer_id(seat));

    CHECK_FALSE(core->scene_released_seat(honest, 7).is_valid());
    CHECK_FALSE(core->scene_released_seat(honest, 0).is_valid());
    CHECK(core->scene_released_seat(honest, 1) == seat);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SL3 a notice reaches nothing while this peer "
    "holds no admitted participant and nothing while that participant holds no "
    "seat, because a release is an edge on a membership that exists"
) {
    Node *root = memnew(Node);
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID arena = mount(core, root);
    const PackedByteArray honest = notice(core->scene_layer_id(arena));
    const int64_t mine = core->get_unique_id();

    CHECK_FALSE(core->scene_released_seat(honest, 1).is_valid());

    Ref<RefCounted> row;
    row.instantiate();
    core->participant_adopt(mine, row);

    CHECK_FALSE(core->scene_released_seat(honest, 1).is_valid());

    REQUIRE(core->participant_admit(mine));

    CHECK_FALSE(core->scene_released_seat(honest, 1).is_valid());

    REQUIRE(core->participant_take_seat(mine, arena));

    CHECK(core->scene_released_seat(honest, 1) == arena);

    memdelete(root);
}

} // namespace TestNetwSceneReleaseNoticeLaws

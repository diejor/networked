#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/session/frames.hpp"

namespace TestNetwSceneReleaseNoticeLaws {

using namespace godot;
using netw::NetwMultiplayer;

RID mount(const Ref<NetwMultiplayer> &p_core, Node *p_parent) {
    Node *container = memnew(Node);
    p_parent->add_child(container);
    Node *level = memnew(Node);
    level->set_name("Arena");
    container->add_child(level);
    const RID handle = p_core->get_liveness_core()->entity_create();
    Ref<netw::NetwEntity> wrapper;
    wrapper.instantiate();
    netw::NetwEntityRecord *const record = wrapper->get_record();
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

PackedByteArray notice(int64_t p_route) {
    return netw::session::frame_write(netw::session::SceneReleased{p_route});
}

int64_t route_of(const Ref<NetwMultiplayer> &p_core, const RID &p_scene) {
    return p_core->scene_route_of(p_scene);
}

Ref<NetwMultiplayer> seated_core(Node *p_root, RID &r_seat, RID &r_other) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    r_seat = mount(core, p_root);
    r_other = mount(core, p_root);
    Ref<netw::NetwParticipant> row;
    row.instantiate();
    core->participant_adopt(core->get_unique_id(), row);
    REQUIRE(core->participant_admit(core->get_unique_id()));
    REQUIRE(core->participant_take_seat(core->get_unique_id(), r_seat));
    return core;
}

TEST_CASE(
    "[Networked][Scene][Hosted] SL1 a release notice names the route of the "
    "seat this peer holds, so a session running two instances of one level "
    "releases the peer from the instance the server named, and a name-derived "
    "id cannot answer this because two peers spell one node's name apart"
) {
    Node *root = memnew(Node);
    RID seat;
    RID other;
    const Ref<NetwMultiplayer> core = seated_core(root, seat, other);

    const int64_t held = route_of(core, seat);
    const int64_t elsewhere = route_of(core, other);
    REQUIRE(held > 0);
    REQUIRE(held != elsewhere);

    CHECK(core->scene_released_seat(notice(held), 1) == seat);
    CHECK_FALSE(core->scene_released_seat(notice(elsewhere), 1).is_valid());
    CHECK_FALSE(core->scene_released_seat(notice(0), 1).is_valid());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SL2 only the server releases a seat, so a peer "
    "forging a notice cannot evict another peer from the scene it is sitting in"
) {
    Node *root = memnew(Node);
    RID seat;
    RID other;
    const Ref<NetwMultiplayer> core = seated_core(root, seat, other);
    const PackedByteArray honest = notice(route_of(core, seat));

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
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const RID arena = mount(core, root);
    const PackedByteArray honest = notice(route_of(core, arena));
    const int64_t mine = core->get_unique_id();

    CHECK_FALSE(core->scene_released_seat(honest, 1).is_valid());

    Ref<netw::NetwParticipant> row;
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

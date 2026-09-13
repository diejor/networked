#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestSceneMoveOptsLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwReparentOpts;
using netw_test::CallLog;

struct Placed {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Placed place(const Ref<NetwMultiplayer> &p_core, Node *p_parent) {
    Placed made;
    made.owner = memnew(Node);
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    made.record = made.wrapper->get_record();
    made.record->adopt_handle(made.handle);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        made.record,
        made.owner
    ));
    made.owner->set_meta(NetwMultiplayer::wrapper_meta(), made.wrapper);
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] MO1 a move whose caller named no reason is "
    "stamped with the scene move's own, so a reparent record can tell a "
    "scene move from every other reparent that reaches the same plane"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog carried;
    core->scene_set_carry_move(carried.callable("carry"));
    Node *root = memnew(Node);
    const Placed mover = place(core, root);
    const Placed arena = place(core, root);
    Ref<NetwReparentOpts> opts;
    opts.instantiate();

    REQUIRE(opts->get_reason() == StringName());

    core->scene_move_entity(mover.handle, arena.handle, opts);

    NETW_CHECK_EQ(carried.count("carry"), 1);
    CHECK(opts->get_reason() == NetwMultiplayer::scene_move_reason());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] MO2 a reason the caller did name survives, "
    "because the stamp fills a gap rather than deciding what a move is for"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog carried;
    core->scene_set_carry_move(carried.callable("carry"));
    Node *root = memnew(Node);
    const Placed mover = place(core, root);
    const Placed arena = place(core, root);
    Ref<NetwReparentOpts> opts;
    opts.instantiate();
    opts->set_reason(StringName("round_start"));

    core->scene_move_entity(mover.handle, arena.handle, opts);

    CHECK(opts->get_reason() == StringName("round_start"));
    CHECK(opts->get_reason() != NetwMultiplayer::scene_move_reason());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] MO3 a move with no options stays optionless, "
    "so the entity plane's own defaults are what a caller naming nothing gets"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog carried;
    core->scene_set_carry_move(carried.callable("carry"));
    Node *root = memnew(Node);
    const Placed mover = place(core, root);
    const Placed arena = place(core, root);

    const Ref<NetwPromise> answered
        = core->scene_move_entity(mover.handle, arena.handle, Variant());

    REQUIRE(answered.is_valid());
    NETW_CHECK_EQ(carried.count("carry"), 1);
    NETW_CHECK_EQ(int(answered->get_code()), int(OK));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Scene][Hosted] MO4 an unreachable destination is refused "
    "before any stamp is written, because a move that never enters the carry "
    "has no record to be attributable in"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const CallLog carried;
    core->scene_set_carry_move(carried.callable("carry"));
    Node *root = memnew(Node);
    const Placed mover = place(core, root);
    Ref<NetwReparentOpts> opts;
    opts.instantiate();

    const Ref<NetwPromise> answered
        = core->scene_move_entity(mover.handle, RID(), opts);

    REQUIRE(answered.is_valid());
    NETW_CHECK_EQ(int(answered->get_code()), int(ERR_UNAVAILABLE));
    NETW_CHECK_EQ(carried.count("carry"), 0);
    CHECK(opts->get_reason() == StringName());

    memdelete(root);
}

} // namespace TestSceneMoveOptsLaws

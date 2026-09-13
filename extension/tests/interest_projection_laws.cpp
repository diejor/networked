#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/entity_decl.h"
#include "support/loopback_rig.h"

namespace TestNetwInterestProjection {

using namespace godot;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;

constexpr int64_t SERVER_PEER = 1;

Array named_ids(const TypedArray<Object> &p_entities) {
    Array out;
    for (int at = 0; at < p_entities.size(); ++at) {
        const Ref<NetwEntity> row
            = Ref<NetwEntity>(Object::cast_to<NetwEntity>(p_entities[at]));
        if (row.is_valid()) {
            out.push_back(row->get_entity_id());
        }
    }
    return out;
}

TEST_CASE(
    "[Networked][Interest] IJ1 the labels an entity resolves to are "
    "answered in name order rather than the order they were joined in, so two "
    "peers that reached one scope by different routes read one list"
) {
    LoopbackRig rig;
    NetwMultiplayer *core = rig.server();
    const RID handle
        = rig.declare_entity(EntityDecl().named("OrderSubject").on_route(71));
    const Ref<NetwEntity> entity = core->entity_get_view(handle);
    REQUIRE(entity.is_valid());

    core->interest_layer(StringName("sight"))->add_entity(entity);
    core->interest_layer(StringName("audio"))->add_entity(entity);
    core->interest_layer(StringName("radar"))->add_entity(entity);

    const Array resolved = core->interest_resolved_layer_ids(entity);

    NETW_CHECK_EQ(resolved.size(), 3);
    if (resolved.size() != 3) {
        return;
    }
    CHECK(StringName(resolved[0]) == StringName("audio"));
    CHECK(StringName(resolved[1]) == StringName("radar"));
    CHECK(StringName(resolved[2]) == StringName("sight"));
}

TEST_CASE(
    "[Networked][Interest] IJ2 the server peer is admitted by the wire "
    "unconditionally while its own participant row is computed like anyone "
    "else's, because a host that cannot see a body still has to be the one "
    "that sends it"
) {
    LoopbackRig rig;
    NetwMultiplayer *core = rig.server();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    const RID handle
        = rig.declare_entity(EntityDecl().named("WireSubject").on_route(72));
    const Ref<NetwEntity> entity = core->entity_get_view(handle);
    REQUIRE(entity.is_valid());
    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("stealth"));
    REQUIRE(layer.is_valid());

    layer->add_entity(entity);
    rig.flush_interest();

    NETW_CHECK_EQ(core->interest_wire_admits(SERVER_PEER, entity), true);
    NETW_CHECK_EQ(core->interest_participant_sees(SERVER_PEER, entity), false);

    layer->add_viewer(SERVER_PEER);
    rig.flush_interest();

    NETW_CHECK_EQ(core->interest_wire_admits(SERVER_PEER, entity), true);
    NETW_CHECK_EQ(core->interest_participant_sees(SERVER_PEER, entity), true);
}

TEST_CASE(
    "[Networked][Interest] IJ3 a peer that computes no admission still "
    "answers who shares its scope, by walking the routes it already holds and "
    "keeping the ones whose own declaration intersects its own, so a consumer "
    "running on the owner reaches the roster the server committed without "
    "carrying any part of another peer's row"
) {
    LoopbackRig rig;
    NetwMultiplayer *core = rig.server();
    NetwMultiplayer *owner = rig.client(0);
    REQUIRE(owner != nullptr);

    const RID mine
        = rig.declare_entity(EntityDecl().named("OwnCar").on_route(73));
    const RID theirs
        = rig.declare_entity(EntityDecl().named("OtherCar").on_route(74));
    const RID elsewhere
        = rig.declare_entity(EntityDecl().named("Spectator").on_route(75));
    REQUIRE(core->entity_get_view(mine).is_valid());
    REQUIRE(core->entity_get_view(theirs).is_valid());
    REQUIRE(core->entity_get_view(elsewhere).is_valid());
    core->entity_get_view(mine)->set_entity_id(StringName("own_car"));
    core->entity_get_view(theirs)->set_entity_id(StringName("other_car"));
    core->entity_get_view(elsewhere)->set_entity_id(StringName("spectator"));
    core->interest_layer(StringName("arena"))
        ->add_entity(core->entity_get_view(mine));
    core->interest_layer(StringName("arena"))
        ->add_entity(core->entity_get_view(theirs));
    core->interest_layer(StringName("lobby"))
        ->add_entity(core->entity_get_view(elsewhere));
    rig.flush_interest();

    const Ref<NetwEntity> local_mine = owner->entity_get_view(
        rig.declare_mirror(0, EntityDecl().named("OwnCar").on_route(73))
    );
    const Ref<NetwEntity> local_theirs = owner->entity_get_view(
        rig.declare_mirror(0, EntityDecl().named("OtherCar").on_route(74))
    );
    const Ref<NetwEntity> local_elsewhere = owner->entity_get_view(
        rig.declare_mirror(0, EntityDecl().named("Spectator").on_route(75))
    );
    REQUIRE(local_mine.is_valid());
    REQUIRE(local_theirs.is_valid());
    REQUIRE(local_elsewhere.is_valid());
    local_mine->set_entity_id(StringName("own_car"));
    local_theirs->set_entity_id(StringName("other_car"));
    local_elsewhere->set_entity_id(StringName("spectator"));
    local_mine->get_interest()->join(StringName("arena"));
    local_theirs->get_interest()->join(StringName("arena"));
    local_elsewhere->get_interest()->join(StringName("lobby"));

    const Array committed = named_ids(core->interest_shared_entities(
        core->entity_get_view(mine),
        StringName("arena")
    ));
    const Array derived = named_ids(
        owner->interest_shared_entities(local_mine, StringName("arena"))
    );

    NETW_CHECK_EQ(derived.size(), committed.size());
    NETW_CHECK_EQ(derived.size(), 1);
    if (derived.size() != 1) {
        return;
    }
    CHECK(StringName(derived[0]) == StringName("other_car"));

    const TypedArray<Object> live = owner->liveness_get_entities();
    CHECK(live.has(Variant(local_theirs)));
}

} // namespace TestNetwInterestProjection

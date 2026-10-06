#include "support/netw_test.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include "support/value_flow_stand.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/decl.hpp"

namespace TestNetwDisplayRoleEdgeLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::FlowPair;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr const char *STEERED_SOURCE = R"(extends Node2D

var shown_position := Vector2()

func _init() -> void:
	var smoothing := NetwInterpolate.new().lerp().smooth(0.0) \
			.to(&"shown_position")
	Netw.configure_property(self, &"position").broadcast() \
			.interpolate(smoothing)
)";

constexpr int SETTLE = 12;
constexpr int STEADY = 30;

int64_t role_of(NetwMultiplayer *p_core, const RID &p_entity) {
    return int64_t(p_core->display_get_track_stat(
        p_entity,
        StringName(),
        StringName("role")
    ));
}

int64_t resolved_role_of(NetwMultiplayer *p_core, const RID &p_entity) {
    p_core->display_mark_dirty(p_entity, netw::display::DIRT_ROLE);
    p_core->session_flush_deferred();
    return role_of(p_core, p_entity);
}

void author_ticks(LoopbackRig &p_rig, const FlowPair &p_pair, int p_ticks) {
    for (int at = 0; at < p_ticks; ++at) {
        p_pair.authored->set_position(
            netw_test::authored_value(netw_test::clock_tick(p_rig, -1))
        );
        p_rig.step_ticks(1);
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a steady stream of received rows "
    "resolves the display role once, and a control change resolves it again "
    "with no row arriving"
) {
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
    const FlowPair pair
        = netw_test::stand_flow_pair(rig, STEERED_SOURCE, "Steered");
    netw_test::steer(pair, 0);
    const Ref<NetwEntity> mirror = NetwEntity::of(pair.mirror(0));
    REQUIRE(mirror.is_valid());
    const RID rid = mirror->get_rid_handle();

    author_ticks(rig, pair, SETTLE);
    const int64_t remote = role_of(client, rid);
    NETW_CHECK_EQ(remote, int64_t(NetwMultiplayer::DISPLAY_ROLE_REMOTE));

    const NetwMultiplayer::IdleWork opened = client->idle_work;
    author_ticks(rig, pair, STEADY);
    const int64_t received
        = client->idle_work.received_rows - opened.received_rows;
    const int64_t resolves
        = client->idle_work.role_resolves - opened.role_resolves;
    NETW_CHECK_GT(received, int64_t(STEADY / 2));
    NETW_CHECK_EQ(resolves, int64_t(0));
    NETW_CHECK_EQ(role_of(client, rid), remote);

    netw_test::steer(pair, int(rig.peer_id(0)));
    const int64_t steered = role_of(client, rid);
    const bool moved = steered != remote;
    CHECK(moved);
    NETW_CHECK_EQ(steered, resolved_role_of(client, rid));
}

} // namespace TestNetwDisplayRoleEdgeLaws

#endif

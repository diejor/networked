#include "support/netw_test.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include "support/value_flow_stand.h"

#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"

namespace TestNetwDisplayLateInterpolatorLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::FlowPair;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr const char *PLAIN_SOURCE = R"(extends Node2D

func _init() -> void:
	Netw.configure_property(self, &"position").broadcast()
)";

constexpr int TICKS = 12;

void author_ticks(LoopbackRig &p_rig, const FlowPair &p_pair, int p_ticks) {
    for (int at = 0; at < p_ticks; ++at) {
        p_pair.authored->set_position(
            netw_test::authored_value(netw_test::clock_tick(p_rig, -1))
        );
        p_rig.step_ticks(1);
    }
}

int64_t stat_of(
    NetwMultiplayer *p_core,
    const RID &p_entity,
    const char *p_stat
) {
    const Variant read = p_core->display_get_track_stat(
        p_entity,
        StringName("position"),
        StringName(p_stat)
    );
    return read.get_type() == Variant::INT ? int64_t(read) : int64_t(-1);
}

TEST_CASE(
    "[Networked][Display][SceneTree] an interpolator a receiving node "
    "declares after its rows began arriving draws from the next row"
) {
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
    const FlowPair pair = netw_test::stand_flow_pair(rig, PLAIN_SOURCE, "Late");
    netw_test::steer(pair, 0);
    const Ref<NetwEntity> mirror = NetwEntity::of(pair.mirror(0));
    REQUIRE(mirror.is_valid());
    const RID rid = mirror->get_rid_handle();

    const NetwMultiplayer::IdleWork opened = client->idle_work;
    author_ticks(rig, pair, TICKS);
    NETW_CHECK_GT(
        client->idle_work.received_rows - opened.received_rows,
        int64_t(TICKS / 2)
    );
    NETW_CHECK_LE(stat_of(client, rid, "channels"), int64_t(0));

    Ref<netw::NetwInterpolate> spec;
    spec.instantiate();
    netw::Netw::configure_property(
        pair.mirror(0),
        StringName("position"),
        false
    )
        ->interpolate(netw::gd::array_of(spec->lerp()->smooth(0.0)));
    const int64_t declared_at = netw_test::clock_tick(rig, 0);
    author_ticks(rig, pair, TICKS);
    NETW_CHECK_EQ(stat_of(client, rid, "channels"), int64_t(1));
    NETW_CHECK_GT(stat_of(client, rid, "newest_tick"), declared_at);
}

} // namespace TestNetwDisplayLateInterpolatorLaws

#endif

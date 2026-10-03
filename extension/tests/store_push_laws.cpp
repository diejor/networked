#include "support/netw_test.h"

#include "support/declared_nodes.h"
#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include "support/alloc_probe.h"
#include "support/netw_call_log.h"
#include "support/value_flow_stand.h"

#include "godot/spatial_node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/display/history.hpp"
#include "netw/store/world.hpp"

namespace TestNetwStorePush {

using namespace godot;
using netw::NetwMultiplayer;
using netw::repl::PropertySet;
using netw::store::World;
using netw_test::FlowPair;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr int WARM_TICKS = 12;
constexpr int STEADY_TICKS = 20;

void author_next(LoopbackRig &p_rig, const FlowPair &p_pair) {
    const int64_t tick = netw_test::clock_tick(p_rig, -1) + 1;
    netw_test::author_at(
        p_pair.authored,
        StringName("position"),
        Vector2(double(tick), -double(tick)),
        PropertySet::RECORD_STATE,
        tick
    );
    p_rig.step_ticks(1);
}

TEST_CASE(
    "[Networked][Store] SP1 a steady received row allocates nothing while it "
    "decodes into the store and while the push writes it to the node"
) {
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    const FlowPair pair = netw_test::stand_flow_pair(
        rig,
        netw_test::gdsrc::STATE_ONLY,
        "SteadyRow"
    );
    NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
    for (int at = 0; at < WARM_TICKS; ++at) {
        author_next(rig, pair);
    }
    World &world = client->store();
    const World::ReceiveWork received = world.receive_work;
    const World::PushWork pushed = world.push_work;
    netw_test::AllocProbe probe;
    probe.arm();
    for (int at = 0; at < STEADY_TICKS; ++at) {
        author_next(rig, pair);
    }
    probe.disarm();

    const int64_t landed = world.receive_work.landed - received.landed;
    const int64_t pushes = world.push_work.pushes - pushed.pushes;
    NETW_CHECK_GE(landed, int64_t(STEADY_TICKS - 2));
    NETW_CHECK_GE(pushes, landed);
    NETW_CHECK_EQ(
        world.receive_work.allocations - received.allocations,
        int64_t(0)
    );
    NETW_CHECK_EQ(world.push_work.allocations - pushed.allocations, int64_t(0));
    const Vector2 held = pair.mirror(0)->get_position();
    const Vector2 authored = pair.authored->get_position();
    NETW_CHECK_CLOSE(held.x, authored.x, 1.5);
}

const char *INTERPOLATED_ROW = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var smoothing := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(smoothing)
)";

TEST_CASE(
    "[Networked][Store] SP2 a steady received row allocates nothing while "
    "the display samples its history"
) {
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    const FlowPair pair
        = netw_test::stand_flow_pair(rig, INTERPOLATED_ROW, "SampledRow");
    NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
    for (int at = 0; at < WARM_TICKS; ++at) {
        author_next(rig, pair);
        client->display_pump(0.0);
    }
    const netw::display::SampleWork opened = netw::display::sample_work();
    netw_test::AllocProbe probe;
    probe.arm();
    for (int at = 0; at < STEADY_TICKS; ++at) {
        author_next(rig, pair);
        client->display_pump(0.0);
    }
    probe.disarm();

    const netw::display::SampleWork &closed = netw::display::sample_work();
    NETW_CHECK_GE(closed.samples - opened.samples, int64_t(STEADY_TICKS));
    NETW_CHECK_EQ(closed.allocations - opened.allocations, int64_t(0));
}

} // namespace TestNetwStorePush

#endif

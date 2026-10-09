#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/fault_log.h"
#include "support/value_flow_stand.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/runtime.hpp"

namespace TestNetwInterpolateExampleLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw_test::FlowPair;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

const char *DOCUMENTED_BODY = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = "Visual"
	add_child(visual)
	var entity := Netw.configure_entity(self)
	entity.display.visual_root = ^"Visual"
	Netw.configure_property(self, &"position").interpolate(
		NetwInterpolate.new().lerp().smooth(0.05)
	)
)";

struct ExampleRun {
    int64_t self_feedback_warnings = 0;
    int64_t server_channels = 0;
    int64_t server_channels_in_place = 0;
};

ExampleRun run_example(const char *p_body) {
    ExampleRun out;
    LoopbackRig rig(1);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    const netw_test::FaultWatch faults;
    const FlowPair pair = netw_test::stand_flow_pair(rig, p_body, "Example");
    rig.step_ticks(6);
    NetwMultiplayer *server = netw_test::flow_core(rig.server());
    netw::display::Runtime *shown = NetwNativeTests::display_runtime_of(
        server,
        NetwEntity::of(pair.authored)->get_rid_handle()
    );
    REQUIRE(shown != nullptr);
    for (netw::display::Channel *channel : shown->channels()) {
        out.server_channels += 1;
        out.server_channels_in_place += channel->get_self_feedback() ? 1 : 0;
    }
    out.self_feedback_warnings
        = faults.warnings_containing("is simulated on this peer");
    return out;
}

TEST_CASE(
    "[Networked][Display][SceneTree] IE1 the NetwInterpolate example smooths "
    "a body onto its visual node, so the server that simulates it keeps a "
    "channel it can smooth and no peer warns about feedback"
) {
    const ExampleRun run = run_example(DOCUMENTED_BODY);
    NETW_CHECK_EQ(run.server_channels, 1);
    NETW_CHECK_EQ(run.server_channels_in_place, 0);
    NETW_CHECK_EQ(run.self_feedback_warnings, 0);
}

} // namespace TestNetwInterpolateExampleLaws

#endif

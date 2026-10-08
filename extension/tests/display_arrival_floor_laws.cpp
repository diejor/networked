#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/value_flow_stand.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"
#include "netw/display/timing.hpp"
#include "netw/object_port.hpp"

using namespace godot;

namespace TestDisplayArrivalFloorLaws {

using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw_test::LoopbackRig;

constexpr int SERVER = 1;
constexpr double LINK_MS = 200.0;
constexpr double FRAME = 1.0 / 30.0;

const char *GLIDER = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.display.visual_root = ^"Visual"
	var glide := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(glide)
)";

struct DelayedGlide {
    LoopbackRig rig;
    netw_test::FlowPair pair;
    double x = 0.0;

    DelayedGlide() : rig(1) {
        rig.declare_world(netw_test::WorldDecl().clocked(30));
        rig.mount();
        pair = netw_test::stand_flow_pair(rig, GLIDER, "Glider");
        rig.clock_of(client()).set_tick_factor_override(0.0);
        const Ref<netw::LocalLinkConditions> late
            = netw::LocalLinkConditions::create(1);
        late->set_latency_ms(LINK_MS);
        rig.conditions(0, late, SERVER);
        rig.step_ticks(4);
    }

    DelayedGlide(const DelayedGlide &) = delete;
    DelayedGlide &operator=(const DelayedGlide &) = delete;

    NetwMultiplayer *client() const {
        return netw_test::flow_core(rig.client(0));
    }

    netw::display::Runtime &runtime() const {
        netw::display::Runtime *found
            = NetwNativeTests::display_runtime_at(client(), pair.route);
        REQUIRE_MESSAGE(found != nullptr, "the mirror has no runtime");
        return *found;
    }

    bool starves_after_glide() {
        x += 10.0;
        netw::port_set(pair.authored, StringName("position"), Vector2(x, 0.0));
        rig.step_ticks(1);
        netw::display::Runtime &held = runtime();
        NetwNativeTests::display_pump_runtime(
            client(),
            &held,
            netw::display::capture_timing(&rig.clock_of(client()), FRAME)
        );
        const int64_t newest
            = held.channels()[0]->display_history().newest_tick();
        return held.display_playhead().get_display_tick() >= newest;
    }
};

TEST_CASE(
    "[Networked][Display] FX6 a remote behind a link longer than the clock "
    "assumes stops starving once its arrival ages settle"
) {
    DelayedGlide glide;
    int64_t early = 0;
    for (int step = 0; step < 30; ++step) {
        early += glide.starves_after_glide() ? 1 : 0;
    }
    for (int step = 0; step < 120; ++step) {
        glide.starves_after_glide();
    }
    int64_t settled = 0;
    for (int step = 0; step < 60; ++step) {
        settled += glide.starves_after_glide() ? 1 : 0;
    }
    NETW_CHECK_GT(early, int64_t(0));
    NETW_CHECK_EQ(settled, int64_t(0));
    CHECK(glide.runtime().display_playhead().hears_arrivals());
}

} // namespace TestDisplayArrivalFloorLaws

#endif

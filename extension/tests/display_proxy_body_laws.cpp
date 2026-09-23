#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "godot/physics_body.hpp"
#include "netw/api/context.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwDisplayProxyBodyLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw::NetwPropertySet;
using netw_test::FlowPair;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

struct WriterScenario {
    String label;
    int ticks = 10;
};

WriterScenario a_proxy_rigid_body() {
    WriterScenario scenario;
    scenario.label = "a-proxy-rigid-body";
    return scenario;
}

struct WriterEvidence {
    int64_t mode = -1;
    int64_t pump = -1;
    bool frozen = false;
    double held_x = 0.0;
    double after_rows_x = 0.0;
    double newest_row_x = 0.0;
    double drawn_x = 0.0;
};

class WriterRun {
    WriterScenario declared;
    WriterEvidence seen;

public:
    explicit WriterRun(const WriterScenario &p_scenario)
        : declared(p_scenario) {
        LoopbackRig rig(1);
        rig.declare_world(WorldDecl().clocked(30));
        rig.mount();
        NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
        const FlowPair pair = netw_test::stand_flow_pair(
            rig,
            netw_test::gdsrc::STATE_BODY,
            "ProxyBody"
        );
        rig.step_ticks(4);

        RigidBody2D *body = Object::cast_to<RigidBody2D>(pair.mirror(0));
        REQUIRE(body != nullptr);
        const RID rid = NetwEntity::of(body)->get_rid_handle();
        netw::display::Runtime *runtime
            = NetwNativeTests::display_runtime_of(client, rid);
        REQUIRE(runtime != nullptr);
        const netw::sim::Row *row = client->sim_row_of(rid);
        seen.mode = row != nullptr ? int64_t(row->mode) : -1;
        seen.pump = runtime->get_pump_mode();
        seen.frozen = body->is_freeze_enabled();

        runtime->set_disabled(true);
        seen.held_x = body->get_position().x;
        for (int step = 1; step <= declared.ticks; ++step) {
            const int64_t tick = netw_test::clock_tick(rig, -1) + 1;
            netw_test::author_at(
                pair.authored,
                StringName("position"),
                Vector2(100.0 + 10.0 * step, 0.0),
                NetwPropertySet::RECORD_STATE,
                tick
            );
            rig.step_ticks(1);
        }
        seen.after_rows_x = body->get_position().x;
        netw::display::Channel *channel
            = runtime->channel_named(StringName("position"));
        REQUIRE(channel != nullptr);
        const netw::display::History &history = channel->display_history();
        REQUIRE_FALSE(history.is_empty());
        seen.newest_row_x = Vector2(history.get_at(history.newest_tick())).x;

        runtime->set_disabled(false);
        client->display_pump(0.0);
        seen.drawn_x = body->get_position().x;
    }

    WriterRun(const WriterRun &) = delete;
    WriterRun &operator=(const WriterRun &) = delete;

    const WriterScenario &scenario() const {
        return declared;
    }

    const WriterEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<WriterRun> WriterLaw;

LawVerdict law_proxy_world(const WriterRun &p_run) {
    const WriterEvidence &seen = p_run.evidence();
    if (seen.mode != int64_t(netw::sim::Mode::PROXY)) {
        return law_broken(
            "the copy resolved mode %d, not a proxy",
            int(seen.mode)
        );
    }
    if (seen.pump != netw::display::PUMP_REMOTE || !seen.frozen) {
        return law_broken(
            "the proxy pumps %d and frozen reads %d",
            int(seen.pump),
            int(seen.frozen)
        );
    }
    if (Math::is_equal_approx(seen.newest_row_x, seen.held_x)) {
        return law_broken("no row moved the column, so nothing was tested");
    }
    return law_held();
}

LawVerdict law_one_writer(const WriterRun &p_run) {
    const WriterEvidence &seen = p_run.evidence();
    if (!Math::is_equal_approx(seen.after_rows_x, seen.held_x)) {
        return law_broken(
            "rows moved the frozen body from %.1f to %.1f while the display "
            "was held",
            seen.held_x,
            seen.after_rows_x
        );
    }
    return law_held();
}

LawVerdict law_drawn_on_body(const WriterRun &p_run) {
    const WriterEvidence &seen = p_run.evidence();
    if (Math::is_equal_approx(seen.drawn_x, seen.held_x)) {
        return law_broken("the pump drew nothing onto the body");
    }
    if (seen.drawn_x > seen.newest_row_x + 0.001) {
        return law_broken(
            "the body sits at %.1f, past the newest row at %.1f",
            seen.drawn_x,
            seen.newest_row_x
        );
    }
    return law_held();
}

const WriterLaw L_PROXY_WORLD = {
    "proxy-world",
    "the copy under test is a frozen proxy drawn by the remote pump, and rows "
    "carrying a new position reached it",
    &law_proxy_world,
};

const WriterLaw L_ONE_WRITER = {
    "one-writer",
    "an interpolated column on a proxy body feeds history only, so no row "
    "arrival writes the body",
    &law_one_writer,
};

const WriterLaw L_DRAWN_ON_BODY = {
    "drawn-on-body",
    "the remote pump draws the column onto the body itself, so the body is "
    "where it is seen",
    &law_drawn_on_body,
};

const WriterLaw WRITER_LAWS[] = {L_PROXY_WORLD, L_ONE_WRITER, L_DRAWN_ON_BODY};

TEST_CASE(
    "[Networked][Display][SceneTree] a proxy rigid body has one writer per "
    "interpolated column, and it is the pump"
) {
    const WriterScenario scenario = a_proxy_rigid_body();
    const WriterRun run(scenario);
    for (const WriterLaw &law : WRITER_LAWS) {
        NETW_CELL(law, scenario);
        NETW_LAW_HOLDS(law, run);
    }
}

const char *ALIASED_BODY = R"(extends RigidBody2D

func _init() -> void:
	freeze = false
	var smoothing := NetwInterpolate.new().lerp().smooth(0.0).to(&"global_position")
	Netw.configure_property(self, &"position").state().interpolate(smoothing)
)";

TEST_CASE(
    "[Networked][Display][SceneTree] a channel that writes a simulated body "
    "under another name is refused at build, and the same channel written "
    "in place is kept"
) {
    LoopbackRig rig(0);
    rig.declare_world(WorldDecl().clocked(30));
    rig.mount();
    NetwMultiplayer *server = netw_test::flow_core(rig.server());

    const FlowPair aliased
        = netw_test::stand_flow_pair(rig, ALIASED_BODY, "AliasedBody");
    const FlowPair in_place = netw_test::stand_flow_pair(
        rig,
        netw_test::gdsrc::STATE_BODY,
        "InPlaceBody"
    );
    rig.step_ticks(2);

    netw::display::Runtime *refused = NetwNativeTests::display_runtime_of(
        server,
        NetwEntity::of(aliased.authored)->get_rid_handle()
    );
    netw::display::Runtime *kept = NetwNativeTests::display_runtime_of(
        server,
        NetwEntity::of(in_place.authored)->get_rid_handle()
    );
    REQUIRE(refused != nullptr);
    REQUIRE(kept != nullptr);
    NETW_CHECK_EQ(int(refused->channels().size()), 0);
    NETW_CHECK_EQ(int(kept->channels().size()), 1);
}

} // namespace TestNetwDisplayProxyBodyLaws

#endif

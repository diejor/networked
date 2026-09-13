#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "netw/api/display_handle.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/ring_buffer.hpp"

#include <godot_cpp/classes/multiplayer_synchronizer.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/scene_replication_config.hpp>

namespace TestDerivedConsumedDisplayLaws {

using namespace godot;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int SETTLE_TICKS = 24;

const char *PROBE_ID = "consumed_interp_probe";

Node *build_interp_probe(const Variant &p_name) {
    Node2D *root = memnew(Node2D);
    root->set_name(String(p_name));

    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    sync->set_name("Sync");
    sync->set_root_path(NodePath(".."));
    Ref<SceneReplicationConfig> config;
    config.instantiate();
    const NodePath tracked(".:position");
    config->add_property(tracked);
    config->property_set_replication_mode(
        tracked,
        SceneReplicationConfig::REPLICATION_MODE_ALWAYS
    );
    sync->set_replication_config(config);
    Dictionary interpolated;
    interpolated[StringName("position")]
        = int(netw::NetwInterpolate::MODE_LERP);
    sync->set_meta(StringName("netw_interpolate"), interpolated);
    root->add_child(sync);
    return root;
}

Array one_string() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

enum Plant {
    PLANT_NONE,
    PLANT_THE_VIEWER_NEVER_RETURNS,
    PLANT_THE_VIEWER_IS_NEVER_ADMITTED,
    PLANT_THE_VIEWER_NEVER_LEAVES,
};

struct RevivalScenario {
    String label;
    int settle = SETTLE_TICKS;
};

RevivalScenario interest_flap() {
    RevivalScenario scenario;
    scenario.label = "interest-flap";
    return scenario;
}

struct Evidence {
    int64_t first_state = -1;
    int64_t dark_state = -1;
    int64_t revived_state = -1;
    int64_t first_tick = -1;
    int64_t revived_tick = -1;
};

class RevivalRun {
    RevivalScenario declared;
    Plant planted = PLANT_NONE;
    Evidence seen;
    int64_t authored = 0;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    void author(LoopbackRig &p_rig, Node2D *p_node, int p_ticks) {
        for (int step = 0; step < p_ticks; ++step) {
            authored += 1;
            p_node->set_position(netw_test::authored_value(authored));
            p_rig.step_ticks(1);
        }
    }

    int64_t newest_of(LoopbackRig &p_rig, int p_route) const {
        Node *node = p_rig.route_node(p_route, 0);
        if (node == nullptr) {
            return -1;
        }
        const Ref<netw::NetwEntity> entity = netw::NetwEntity::of(node);
        if (entity.is_null()) {
            return -1;
        }
        const Ref<netw::NetwDisplayHandle> display
            = entity->get_interpolation();
        if (display.is_null()) {
            return -1;
        }
        const Ref<netw::NetwRingBuffer> buffer
            = display->get_buffer(StringName("position"));
        return buffer.is_null() ? -1 : buffer->newest_tick();
    }

    int64_t state_of(LoopbackRig &p_rig, int p_route) const {
        netw::NetwMultiplayer *core = netw_test::flow_core(p_rig.client(0));
        return int64_t(
            core->entity_get_state(core->entity_from_route(p_route))
        );
    }

public:
    explicit RevivalRun(
        const RevivalScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        rig.join(0, StringName("watcher"));
        const int route = rig.spawn_registered(
            StringName(PROBE_ID),
            callable_mp_static(&build_interp_probe),
            named("InterpProbe"),
            one_string()
        );
        REQUIRE_MESSAGE(route > 0, "the spawn minted no route");
        Node2D *body = Object::cast_to<Node2D>(rig.route_node(route, -1));
        REQUIRE_MESSAGE(body != nullptr, "the spawn built no authority body");

        netw::NetwMultiplayer *server = netw_test::flow_core(rig.server());
        const Ref<netw::NetwInterestLayer> layer
            = server->interest_layer(StringName("interp_flap"));
        REQUIRE_MESSAGE(layer.is_valid(), "the session minted no layer");
        layer->add_entity(netw::NetwEntity::of(body));
        if (!plant_is(PLANT_THE_VIEWER_IS_NEVER_ADMITTED)) {
            layer->add_viewer(rig.peer_id(0));
        }
        server->interest_flush_now();
        author(rig, body, p_scenario.settle);
        seen.first_state = state_of(rig, route);
        seen.first_tick = newest_of(rig, route);

        if (!plant_is(PLANT_THE_VIEWER_NEVER_LEAVES)) {
            layer->remove_viewer(rig.peer_id(0));
            server->interest_flush_now();
            rig.step_ticks(4);
        }
        seen.dark_state = state_of(rig, route);

        if (!plant_is(PLANT_THE_VIEWER_NEVER_RETURNS)) {
            layer->add_viewer(rig.peer_id(0));
            server->interest_flush_now();
            rig.step_ticks(4);
        }
        seen.revived_state = state_of(rig, route);
        author(rig, body, p_scenario.settle);
        seen.revived_tick = newest_of(rig, route);
    }

    const RevivalScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<RevivalRun> RevivalLaw;

LawVerdict law_fed(const RevivalRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.first_state != int64_t(netw::NetwMultiplayer::ENTITY_STATE_LIVE)) {
        return law_broken(
            "an admitted route reads state %d",
            int(seen.first_state)
        );
    }
    if (seen.first_tick <= 0) {
        return law_broken(
            "the receiver's buffer holds %d after the settle",
            int(seen.first_tick)
        );
    }
    return law_held();
}

const RevivalLaw L_FED = {
    "fed",
    "a consumed synchronizer's samples reach the receiver's interpolation "
    "buffer through the pipeline's apply hook",
    &law_fed,
};

LawVerdict law_darkens(const RevivalRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.dark_state
        != int64_t(netw::NetwMultiplayer::ENTITY_STATE_ABSENT)) {
        return law_broken(
            "a route no viewer holds reads state %d",
            int(seen.dark_state)
        );
    }
    return law_held();
}

const RevivalLaw L_DARKENS = {
    "darkens",
    "interest loss darkens the route on the peer that lost it",
    &law_darkens,
};

LawVerdict law_revives(const RevivalRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.revived_state
        != int64_t(netw::NetwMultiplayer::ENTITY_STATE_LIVE)) {
        return law_broken(
            "a re-admitted route reads state %d",
            int(seen.revived_state)
        );
    }
    if (seen.revived_tick <= seen.first_tick) {
        return law_broken(
            "the rebuilt buffer holds %d against the %d it held before",
            int(seen.revived_tick),
            int(seen.first_tick)
        );
    }
    return law_held();
}

const RevivalLaw L_REVIVES = {
    "revives",
    "re-admission rebuilds the interpolation runtime and the rebuilt one "
    "accepts fresh consumed-sync samples again",
    &law_revives,
};

const RevivalLaw LAWS[] = {L_FED, L_DARKENS, L_REVIVES};

TEST_CASE("[Networked][Sync][SceneTree] the consumed display laws hold") {
    const RevivalScenario CORPUS[] = {interest_flap()};
    for (const RevivalScenario &scenario : CORPUS) {
        const RevivalRun run(scenario);
        for (const RevivalLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE("[Networked][Sync][SceneTree] a viewer never admitted reds fed") {
    const RevivalScenario scenario = interest_flap();
    const RevivalRun run(scenario, PLANT_THE_VIEWER_IS_NEVER_ADMITTED);
    NETW_CELL(L_FED, scenario);
    NETW_LAW_BREAKS(L_FED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a viewer that never leaves reds "
    "darkens"
) {
    const RevivalScenario scenario = interest_flap();
    const RevivalRun run(scenario, PLANT_THE_VIEWER_NEVER_LEAVES);
    NETW_CELL(L_DARKENS, scenario);
    NETW_LAW_BREAKS(L_DARKENS, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a viewer that never returns reds "
    "revives"
) {
    const RevivalScenario scenario = interest_flap();
    const RevivalRun run(scenario, PLANT_THE_VIEWER_NEVER_RETURNS);
    NETW_CELL(L_REVIVES, scenario);
    NETW_LAW_BREAKS(L_REVIVES, run);
}

} // namespace TestDerivedConsumedDisplayLaws

#endif

#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/event_ring.h"
#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "netw/api/event_plane.hpp"

namespace TestDerivedValueFlowLaws {

using namespace godot;
using netw::EventPlane;
using netw::NetwPropertySet;
using netw_test::authored_value;
using netw_test::EventRing;
using netw_test::FlowPair;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
constexpr int RETAINED_PHASE = 20;
constexpr int RETAINED_FLIP = 10;

const char *DERIVED_BODY = netw_test::gdsrc::STATE_AND_INPUT;
const char *RETAINED_BODY = netw_test::gdsrc::STATE_VOLATILE_AND_RETAINED;
const char *BROADCAST_BODY = netw_test::gdsrc::BROADCAST_MASKED_AIM;

enum Plant {
    PLANT_NONE,
    PLANT_THE_MIRROR_NEVER_BOUND_THE_ROUTE,
    PLANT_THE_OBSERVER_NEVER_BOUND_THE_ROUTE,
    PLANT_NOBODY_STEERS,
    PLANT_THE_UPLINK_DROPS_EVERYTHING,
    PLANT_THE_RETAINED_FIELD_FLIPS_EVERY_TICK,
    PLANT_THE_BODY_DECLARES_ONLY_A_BROADCAST,
};

struct FlowScenario {
    String label;
    const char *body = DERIVED_BODY;
    int clients = 1;
    int ticks = 60;
    double delay_ticks = 0.0;
    int64_t gap_bound = 0;
    bool armed = false;
    bool retained = false;
    bool derived_sets = true;
    bool input_set = true;
};

FlowScenario instant_pair() {
    FlowScenario scenario;
    scenario.label = "instant-pair";
    return scenario;
}

FlowScenario delayed_pair() {
    FlowScenario scenario;
    scenario.label = "delayed-pair";
    scenario.delay_ticks = 4.0;
    scenario.gap_bound = 4;
    return scenario;
}

FlowScenario observed_pair() {
    FlowScenario scenario;
    scenario.label = "observed-pair";
    scenario.clients = 2;
    scenario.ticks = 40;
    scenario.armed = true;
    return scenario;
}

FlowScenario retained_pair() {
    FlowScenario scenario;
    scenario.label = "retained-pair";
    scenario.body = RETAINED_BODY;
    scenario.ticks = RETAINED_PHASE;
    scenario.retained = true;
    scenario.input_set = false;
    return scenario;
}

struct Evidence {
    double authority_state = 0.0;
    double mirror_state = 0.0;
    double observer_state = 0.0;
    double authority_input = 0.0;
    double controller_input = 0.0;
    double observer_input = 0.0;
    int64_t widest_gap = 0;
    int64_t authority_frames_in = 0;
    int64_t controller_frames_in = 0;
    int64_t window_frames_out = 0;
    int64_t window_samples_out = 0;
    int64_t gather_rows = 0;
    int64_t encode_rows = 0;
    int64_t decode_rows = 0;
    int64_t apply_rows = 0;
    int64_t state_record = -1;
    int64_t input_record = -1;
    bool state_over_body = false;
    int64_t retained_settled = 0;
    int64_t retained_quiet = 0;
    int64_t retained_flipped = 0;
    bool mirror_holds_flip = false;
};

int64_t rows_of(const Array &p_ring, int64_t p_event) {
    const EventRing ring(p_ring);
    int64_t seen = 0;
    for (int index = 0; index < ring.size(); index++) {
        seen += ring.event_at(index) == p_event ? 1 : 0;
    }
    return seen;
}

int64_t stat_of(godot::Object *p_core, const char *p_name) {
    return int64_t(
        netw_test::flow_core(p_core)->stats_snapshot().get(p_name, 0)
    );
}

class FlowRun {
    FlowScenario declared;
    Plant planted = PLANT_NONE;
    Evidence seen;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    const char *body_path() const {
        return plant_is(PLANT_THE_BODY_DECLARES_ONLY_A_BROADCAST)
            ? BROADCAST_BODY
            : declared.body;
    }

    void impair(LoopbackRig &p_rig) {
        if (declared.delay_ticks > 0.0) {
            const Ref<netw::LocalLinkConditions> delayed
                = netw::LocalLinkConditions::create(1);
            delayed->set_latency_ms(
                declared.delay_ticks * 1000.0 / double(TICKRATE)
            );
            p_rig.conditions(0, delayed, 1);
        }
        if (plant_is(PLANT_THE_UPLINK_DROPS_EVERYTHING)) {
            const Ref<netw::LocalLinkConditions> lost
                = netw::LocalLinkConditions::create(2);
            lost->set_packet_loss(1.0);
            p_rig.conditions(-1, lost, p_rig.peer_id(0));
        }
    }

    void drive_tick(LoopbackRig &p_rig, const FlowPair &p_pair) {
        const int64_t next = netw_test::clock_tick(p_rig, -1) + 1;
        netw_test::author_at(
            p_pair.authored,
            StringName("position"),
            authored_value(next),
            NetwPropertySet::RECORD_STATE,
            next
        );
        const int64_t controller_next = netw_test::clock_tick(p_rig, 0) + 1;
        netw_test::author_at(
            p_pair.mirror(0),
            StringName("rotation"),
            double(controller_next) * 0.01,
            NetwPropertySet::RECORD_INPUT,
            controller_next
        );
        if (plant_is(PLANT_THE_RETAINED_FIELD_FLIPS_EVERY_TICK)) {
            p_pair.authored->set(StringName("stunned"), (next % 2) == 0);
        }
        p_rig.step_ticks(1);
    }

    void record_gap(const FlowPair &p_pair) {
        const int64_t gap = int64_t(
            p_pair.authored->get_position().x
            - p_pair.mirror(0)->get_position().x
        );
        seen.widest_gap = gap > seen.widest_gap ? gap : seen.widest_gap;
    }

    void record_bindings(const FlowPair &p_pair) {
        const Ref<netw::NetwPropertySetBinding> state = netw_test::binding_of(
            p_pair.authored,
            NetwPropertySet::RECORD_STATE
        );
        const Ref<netw::NetwPropertySetBinding> input = netw_test::binding_of(
            p_pair.authored,
            NetwPropertySet::RECORD_INPUT
        );
        if (state.is_valid() && state->set.is_valid()) {
            seen.state_record = int64_t(state->set->get_record());
            seen.state_over_body = state->node() == p_pair.authored;
        }
        if (input.is_valid() && input->set.is_valid()) {
            seen.input_record = int64_t(input->set->get_record());
        }
    }

    void record_rings(LoopbackRig &p_rig, const FlowPair &p_pair) {
        if (!declared.armed) {
            return;
        }
        const Array server_pass
            = netw_test::flow_core(p_rig.server())->event_ring(0);
        seen.gather_rows = rows_of(server_pass, EventPlane::GATHER);
        seen.encode_rows = rows_of(server_pass, EventPlane::SYNC_ENCODE);
        const Array client_route
            = netw_test::flow_core(p_rig.client(0))->event_ring(p_pair.route);
        seen.decode_rows = rows_of(client_route, EventPlane::SYNC_DECODE);
        const Array client_pass
            = netw_test::flow_core(p_rig.client(0))->event_ring(0);
        seen.apply_rows = rows_of(client_pass, EventPlane::APPLY);
    }

    void record_values(LoopbackRig &p_rig, const FlowPair &p_pair) {
        seen.authority_state = p_pair.authored->get_position().x;
        seen.mirror_state = p_pair.mirror(0)->get_position().x;
        seen.authority_input = p_pair.authored->get_rotation();
        seen.controller_input = p_pair.mirror(0)->get_rotation();
        if (declared.clients > 1) {
            seen.observer_state = p_pair.mirror(1)->get_position().x;
            seen.observer_input = p_pair.mirror(1)->get_rotation();
        }
        seen.authority_frames_in = stat_of(p_rig.server(), "derived_frames_in");
        seen.controller_frames_in
            = stat_of(p_rig.client(0), "derived_frames_in");
        seen.window_frames_out = stat_of(p_rig.client(0), "window_frames_out");
        seen.window_samples_out
            = stat_of(p_rig.client(0), "window_samples_out");
    }

    void drive_retained(LoopbackRig &p_rig, const FlowPair &p_pair) {
        for (int step = 0; step < RETAINED_PHASE; ++step) {
            drive_tick(p_rig, p_pair);
        }
        seen.retained_settled = stat_of(p_rig.server(), "retained_frames_out");
        for (int step = 0; step < RETAINED_PHASE; ++step) {
            drive_tick(p_rig, p_pair);
        }
        seen.retained_quiet = stat_of(p_rig.server(), "retained_frames_out");
        p_pair.authored->set(StringName("stunned"), true);
        for (int step = 0; step < RETAINED_FLIP; ++step) {
            p_rig.step_ticks(1);
        }
        seen.retained_flipped = stat_of(p_rig.server(), "retained_frames_out");
        seen.mirror_holds_flip
            = bool(p_pair.mirror(0)->get(StringName("stunned")));
    }

public:
    explicit FlowRun(const FlowScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(p_scenario.clients);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        netw_test::MirrorBinding binding = netw_test::BIND_EVERY_MIRROR;
        if (plant_is(PLANT_THE_MIRROR_NEVER_BOUND_THE_ROUTE)) {
            binding = netw_test::BIND_NO_MIRROR;
        }
        if (plant_is(PLANT_THE_OBSERVER_NEVER_BOUND_THE_ROUTE)) {
            binding = netw_test::BIND_THE_FIRST_MIRROR;
        }
        const FlowPair pair
            = netw_test::stand_flow_pair(rig, body_path(), "FlowBody", binding);
        if (!plant_is(PLANT_NOBODY_STEERS)) {
            netw_test::steer(pair, rig.peer_id(0));
        }
        if (p_scenario.armed) {
            netw_test::flow_core(rig.server())->event_arm(true);
            netw_test::flow_core(rig.client(0))->event_arm(true);
        }
        impair(rig);

        if (p_scenario.retained) {
            drive_retained(rig, pair);
        } else {
            for (int step = 0; step < p_scenario.ticks; ++step) {
                drive_tick(rig, pair);
                if (step > p_scenario.ticks / 4) {
                    record_gap(pair);
                }
            }
        }
        record_values(rig, pair);
        record_bindings(pair);
        record_rings(rig, pair);
    }

    const FlowScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<FlowRun> FlowLaw;

LawVerdict law_state(const FlowRun &p_run) {
    const FlowScenario &scenario = p_run.scenario();
    const Evidence &seen = p_run.evidence();
    if (!scenario.derived_sets) {
        return law_held();
    }
    if (seen.mirror_state <= 0.0) {
        return law_broken("the mirror never received the authored state");
    }
    if (seen.mirror_state > seen.authority_state) {
        return law_broken(
            "the mirror reads %d against the authority's %d",
            int(seen.mirror_state),
            int(seen.authority_state)
        );
    }
    if (seen.widest_gap > scenario.gap_bound) {
        return law_broken(
            "the mirror trailed by %d, past the declared %d",
            int(seen.widest_gap),
            int(scenario.gap_bound)
        );
    }
    return law_held();
}

const FlowLaw L_STATE = {
    "state-reaches",
    "the authority's state field reaches its mirror, which never leads it and "
    "never trails past the link's declared delay",
    &law_state,
};

LawVerdict law_input(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().derived_sets || !p_run.scenario().input_set) {
        return law_held();
    }
    if (seen.authority_input <= 0.0) {
        return law_broken("the authority never read the controller's input");
    }
    if (seen.authority_input > seen.controller_input) {
        return law_broken(
            "the authority reads %d against the controller's %d",
            int(seen.authority_input * 1000.0),
            int(seen.controller_input * 1000.0)
        );
    }
    return law_held();
}

const FlowLaw L_INPUT = {
    "input-reaches",
    "the controller's input field reaches the authority, which never leads it",
    &law_input,
};

LawVerdict law_applied(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().derived_sets) {
        return law_held();
    }
    if (p_run.scenario().input_set && seen.authority_frames_in <= 0) {
        return law_broken("the authority applied no derived frame");
    }
    if (seen.controller_frames_in <= 0) {
        return law_broken("the controller applied no derived frame");
    }
    return law_held();
}

const FlowLaw L_APPLIED = {
    "applied",
    "both peers applied derived frames rather than holding coincident "
    "defaults",
    &law_applied,
};

LawVerdict law_audience(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (p_run.scenario().clients < 2) {
        return law_held();
    }
    if (seen.observer_state <= 0.0) {
        return law_broken("an observer never received the public state");
    }
    if (seen.observer_input != 0.0) {
        return law_broken(
            "an observer read %d of the server-only input",
            int(seen.observer_input * 1000.0)
        );
    }
    return law_held();
}

const FlowLaw L_AUDIENCE = {
    "audience",
    "a peer that neither authors nor controls receives the public state and "
    "none of the server-only input",
    &law_audience,
};

LawVerdict law_stages(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().armed) {
        return law_held();
    }
    if (seen.gather_rows <= 0) {
        return law_broken("the send pass recorded no gather");
    }
    if (seen.encode_rows <= 0) {
        return law_broken("the send pass gathered offers and encoded none");
    }
    if (seen.decode_rows <= 0) {
        return law_broken("the receiver decoded nothing on the route");
    }
    if (seen.apply_rows <= 0) {
        return law_broken("the receive pass recorded no apply");
    }
    return law_held();
}

const FlowLaw L_STAGES = {
    "stages",
    "a sync pass leaves a gather and an encode row on the sender and a decode "
    "row on the route it landed with an apply row on the receiver",
    &law_stages,
};

LawVerdict law_handles(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().derived_sets) {
        return law_held();
    }
    if (seen.state_record != int64_t(NetwPropertySet::RECORD_STATE)) {
        return law_broken(
            "the state binding carries record %d",
            int(seen.state_record)
        );
    }
    if (p_run.scenario().input_set
        && seen.input_record != int64_t(NetwPropertySet::RECORD_INPUT)) {
        return law_broken(
            "the input binding carries record %d",
            int(seen.input_record)
        );
    }
    if (!seen.state_over_body) {
        return law_broken("the state binding resolves another node");
    }
    return law_held();
}

const FlowLaw L_HANDLES = {
    "handles",
    "the record resolves one binding per declared kind, each over the body "
    "that declared it",
    &law_handles,
};

LawVerdict law_retained(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().retained) {
        return law_held();
    }
    if (seen.retained_settled <= 0) {
        return law_broken("the retained half never settled a first row");
    }
    if (seen.retained_quiet != seen.retained_settled) {
        return law_broken(
            "an unchanged retained field sent %d rows past the settled %d",
            int(seen.retained_quiet - seen.retained_settled),
            int(seen.retained_settled)
        );
    }
    if (seen.retained_flipped <= seen.retained_quiet) {
        return law_broken("a changed retained field sent nothing");
    }
    if (!seen.mirror_holds_flip) {
        return law_broken("the mirror never took the changed retained field");
    }
    if (seen.mirror_state <= 0.0) {
        return law_broken("the volatile half stopped with the retained one");
    }
    return law_held();
}

const FlowLaw L_RETAINED = {
    "retained",
    "the retained half of a set rides only when its field changes while the "
    "volatile half keeps sending",
    &law_retained,
};

LawVerdict law_windowed(const FlowRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().derived_sets || !p_run.scenario().input_set) {
        return law_held();
    }
    if (seen.window_frames_out <= 0) {
        return law_broken("the input lane sent no windowed frame");
    }
    if (seen.window_samples_out <= seen.window_frames_out) {
        return law_broken(
            "%d samples rode %d frames, so no redundancy was bought",
            int(seen.window_samples_out),
            int(seen.window_frames_out)
        );
    }
    return law_held();
}

const FlowLaw L_WINDOWED = {
    "windowed",
    "the input lane repeats recent ticks inside one frame rather than "
    "retransmitting them",
    &law_windowed,
};

const FlowLaw LAWS[] = {
    L_STATE,
    L_INPUT,
    L_APPLIED,
    L_AUDIENCE,
    L_STAGES,
    L_HANDLES,
    L_RETAINED,
    L_WINDOWED,
};

TEST_CASE("[Networked][Sync][SceneTree] the derived value-flow laws hold") {
    const FlowScenario CORPUS[] = {
        instant_pair(),
        delayed_pair(),
        observed_pair(),
        retained_pair(),
    };
    for (const FlowScenario &scenario : CORPUS) {
        const FlowRun run(scenario);
        for (const FlowLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a mirror that never bound the route "
    "reds state-reaches"
) {
    const FlowScenario scenario = instant_pair();
    const FlowRun run(scenario, PLANT_THE_MIRROR_NEVER_BOUND_THE_ROUTE);
    NETW_CELL(L_STATE, scenario);
    NETW_LAW_BREAKS(L_STATE, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an observer that never bound the route "
    "reds audience"
) {
    const FlowScenario scenario = observed_pair();
    const FlowRun run(scenario, PLANT_THE_OBSERVER_NEVER_BOUND_THE_ROUTE);
    NETW_CELL(L_AUDIENCE, scenario);
    NETW_LAW_BREAKS(L_AUDIENCE, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an entity nobody steers reds "
    "input-reaches"
) {
    const FlowScenario scenario = instant_pair();
    const FlowRun run(scenario, PLANT_NOBODY_STEERS);
    NETW_CELL(L_INPUT, scenario);
    NETW_LAW_BREAKS(L_INPUT, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an entity nobody steers reds "
    "windowed"
) {
    const FlowScenario scenario = instant_pair();
    const FlowRun run(scenario, PLANT_NOBODY_STEERS);
    NETW_CELL(L_WINDOWED, scenario);
    NETW_LAW_BREAKS(L_WINDOWED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an uplink that drops everything reds "
    "applied"
) {
    const FlowScenario scenario = instant_pair();
    const FlowRun run(scenario, PLANT_THE_UPLINK_DROPS_EVERYTHING);
    NETW_CELL(L_APPLIED, scenario);
    NETW_LAW_BREAKS(L_APPLIED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a retained field that changes every "
    "tick reds retained"
) {
    const FlowScenario scenario = retained_pair();
    const FlowRun run(scenario, PLANT_THE_RETAINED_FIELD_FLIPS_EVERY_TICK);
    NETW_CELL(L_RETAINED, scenario);
    NETW_LAW_BREAKS(L_RETAINED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a body that declares only a broadcast "
    "reds handles"
) {
    const FlowScenario scenario = instant_pair();
    const FlowRun run(scenario, PLANT_THE_BODY_DECLARES_ONLY_A_BROADCAST);
    NETW_CELL(L_HANDLES, scenario);
    NETW_LAW_BREAKS(L_HANDLES, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a mirror that never bound the route "
    "reds stages"
) {
    const FlowScenario scenario = observed_pair();
    const FlowRun run(scenario, PLANT_THE_MIRROR_NEVER_BOUND_THE_ROUTE);
    NETW_CELL(L_STAGES, scenario);
    NETW_LAW_BREAKS(L_STAGES, run);
}

} // namespace TestDerivedValueFlowLaws

#endif

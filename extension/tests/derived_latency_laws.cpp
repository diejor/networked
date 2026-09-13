#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

namespace TestDerivedLatencyLaws {

using namespace godot;
using netw::NetwPropertySet;
using netw_test::authored_value;
using netw_test::FlowCapture;
using netw_test::FlowPair;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;

constexpr int TICKRATE = 30;
const char *DERIVED_BODY = netw_test::gdsrc::STATE_AND_INPUT;

FlowCapture CAPTURE;

void on_state_applied(Dictionary p_header) {
    CAPTURE.note(p_header, StringName("position"));
}

enum Plant {
    PLANT_NONE,
    PLANT_THE_LINK_JITTERS,
    PLANT_THE_REPEAT_RUNS_ANOTHER_DELAY,
    PLANT_THE_REFERENCE_KEEPS_THE_SAME_DELAY,
};

struct LatencyScenario {
    String label;
    double delay_ticks = 2.0;
    double reference_ticks = 0.0;
    int warmup = 20;
    int measure = 60;
};

LatencyScenario exact_delay() {
    LatencyScenario scenario;
    scenario.label = "exact-delay";
    return scenario;
}

LatencyScenario wider_delay() {
    LatencyScenario scenario;
    scenario.label = "wider-delay";
    scenario.delay_ticks = 6.0;
    return scenario;
}

LatencyScenario scaled_delay() {
    LatencyScenario scenario;
    scenario.label = "scaled-delay";
    scenario.delay_ticks = 6.0;
    scenario.reference_ticks = 2.0;
    return scenario;
}

struct Reading {
    int64_t rows = 0;
    int64_t spread = 0;
    double mean = 0.0;
};

Reading drive(
    double p_delay_ticks,
    int p_warmup,
    int p_measure,
    bool p_jitters
) {
    LoopbackRig rig(1);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const FlowPair pair
        = netw_test::stand_flow_pair(rig, DERIVED_BODY, "LatencyBody");
    netw_test::steer(pair, rig.peer_id(0));

    const double period = 1000.0 / double(TICKRATE);
    const Ref<netw::LocalLinkConditions> conditions
        = netw::LocalLinkConditions::create(1);
    conditions->set_latency_ms(p_delay_ticks * period);
    if (p_jitters) {
        conditions->set_jitter_ms(6.0 * period);
    }
    rig.conditions(0, conditions, 1);

    const Ref<netw::NetwPropertySetBinding> mirror_state
        = netw_test::binding_of(pair.mirror(0), NetwPropertySet::RECORD_STATE);
    REQUIRE_MESSAGE(mirror_state.is_valid(), "the mirror holds no state set");
    CAPTURE.reset();
    CAPTURE.measuring = false;
    CAPTURE.receiver = &rig.clock_of(rig.client(0));
    mirror_state->on_applied = callable_mp_static(&on_state_applied);

    for (int step = 0; step < p_warmup + p_measure; ++step) {
        if (step == p_warmup) {
            CAPTURE.measuring = true;
        }
        const int64_t next = netw_test::clock_tick(rig, -1) + 1;
        netw_test::author_at(
            pair.authored,
            StringName("position"),
            authored_value(next),
            NetwPropertySet::RECORD_STATE,
            next
        );
        rig.step_ticks(1);
    }

    mirror_state->on_applied = Callable();
    Reading reading;
    reading.rows = CAPTURE.latency_rows;
    reading.spread = CAPTURE.spread();
    reading.mean = CAPTURE.mean();
    CAPTURE.reset();
    return reading;
}

class LatencyRun {
    LatencyScenario declared;
    Reading measured;
    Reading repeated;
    Reading referenced;

public:
    explicit LatencyRun(
        const LatencyScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario) {
        measured = drive(
            p_scenario.delay_ticks,
            p_scenario.warmup,
            p_scenario.measure,
            p_plant == PLANT_THE_LINK_JITTERS
        );
        const double repeat_delay
            = p_plant == PLANT_THE_REPEAT_RUNS_ANOTHER_DELAY
            ? p_scenario.delay_ticks + 4.0
            : p_scenario.delay_ticks;
        repeated
            = drive(repeat_delay, p_scenario.warmup, p_scenario.measure, false);
        if (p_scenario.reference_ticks > 0.0) {
            const double reference_delay
                = p_plant == PLANT_THE_REFERENCE_KEEPS_THE_SAME_DELAY
                ? p_scenario.delay_ticks
                : p_scenario.reference_ticks;
            referenced = drive(
                reference_delay,
                p_scenario.warmup,
                p_scenario.measure,
                false
            );
        }
    }

    const LatencyScenario &scenario() const {
        return declared;
    }

    const Reading &first() const {
        return measured;
    }

    const Reading &second() const {
        return repeated;
    }

    const Reading &reference() const {
        return referenced;
    }
};

typedef LawRowFor<LatencyRun> LatencyLaw;

LawVerdict law_sampled(const LatencyRun &p_run) {
    if (p_run.first().rows <= 10) {
        return law_broken(
            "%d frames landed in the measured window",
            int(p_run.first().rows)
        );
    }
    return law_held();
}

const LatencyLaw L_SAMPLED = {
    "sampled",
    "enough of the stream lands after the warmup for the window to be a "
    "measurement rather than an anecdote",
    &law_sampled,
};

LawVerdict law_flat(const LatencyRun &p_run) {
    if (p_run.first().spread != 0) {
        return law_broken(
            "the in-flight tick count spread over %d ticks",
            int(p_run.first().spread)
        );
    }
    return law_held();
}

const LatencyLaw L_FLAT = {
    "flat",
    "an exact delay under lockstep gives every frame the same in-flight tick "
    "count, so the spread inside one run collapses to zero",
    &law_flat,
};

LawVerdict law_reproducible(const LatencyRun &p_run) {
    if (p_run.first().mean != p_run.second().mean) {
        return law_broken(
            "two runs of one scenario read %d and %d in-flight hundredths",
            int(p_run.first().mean * 100.0),
            int(p_run.second().mean * 100.0)
        );
    }
    return law_held();
}

const LatencyLaw L_REPRODUCIBLE = {
    "reproducible",
    "two runs of one scenario in one process measure the same latency, which "
    "is what makes the number a property of the link rather than of the run",
    &law_reproducible,
};

LawVerdict law_scales(const LatencyRun &p_run) {
    const LatencyScenario &scenario = p_run.scenario();
    if (scenario.reference_ticks <= 0.0) {
        return law_held();
    }
    const double moved = p_run.first().mean - p_run.reference().mean;
    const double declared = scenario.delay_ticks - scenario.reference_ticks;
    if (moved != declared) {
        return law_broken(
            "%d configured hundredths of a tick moved the measurement by %d",
            int(declared * 100.0),
            int(moved * 100.0)
        );
    }
    return law_held();
}

const LatencyLaw L_SCALES = {
    "scales",
    "a configured delay maps tick for tick onto the measured in-flight count",
    &law_scales,
};

const LatencyLaw LAWS[] = {L_SAMPLED, L_FLAT, L_REPRODUCIBLE, L_SCALES};

TEST_CASE("[Networked][Sync][SceneTree] the derived latency laws hold") {
    const LatencyScenario CORPUS[]
        = {exact_delay(), wider_delay(), scaled_delay()};
    for (const LatencyScenario &scenario : CORPUS) {
        const LatencyRun run(scenario);
        for (const LatencyLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE("[Networked][Sync][SceneTree] a jittering link reds flat") {
    const LatencyScenario scenario = exact_delay();
    const LatencyRun run(scenario, PLANT_THE_LINK_JITTERS);
    NETW_CELL(L_FLAT, scenario);
    NETW_LAW_BREAKS(L_FLAT, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a repeat run over another delay reds "
    "reproducible"
) {
    const LatencyScenario scenario = exact_delay();
    const LatencyRun run(scenario, PLANT_THE_REPEAT_RUNS_ANOTHER_DELAY);
    NETW_CELL(L_REPRODUCIBLE, scenario);
    NETW_LAW_BREAKS(L_REPRODUCIBLE, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a reference arm that never changed the "
    "delay reds scales"
) {
    const LatencyScenario scenario = scaled_delay();
    const LatencyRun run(scenario, PLANT_THE_REFERENCE_KEEPS_THE_SAME_DELAY);
    NETW_CELL(L_SCALES, scenario);
    NETW_LAW_BREAKS(L_SCALES, run);
}

} // namespace TestDerivedLatencyLaws

#endif

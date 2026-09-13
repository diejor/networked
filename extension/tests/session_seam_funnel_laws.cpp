#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/event_ring.h"
#include "support/netw_cells.h"

#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/declared_seams.h"
#include "support/minted_script.h"

namespace TestNetwSessionSeamFunnelLaws {

using namespace godot;
using netw::EventPlane;
using netw::NetwMultiplayer;
using netw_test::EventRing;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

const char *ANSWER_FIXTURE = netw_test::gdsrc::SEAM_ANSWER_SHAPES;
const char *SEAM = "_sync_admit_frame";
constexpr int64_t ROUTE = 7;

enum Plant {
    PLANT_NONE,
    PLANT_AN_ANSWER_THAT_SKIPS_THE_FUNNEL,
};

struct FunnelScenario {
    String label;
    const char *answers = "plain";
    bool refused = false;
};

FunnelScenario a_seam_that_awaits() {
    FunnelScenario scenario;
    scenario.label = "a-seam-that-awaits";
    scenario.answers = "awaiting";
    scenario.refused = true;
    return scenario;
}

FunnelScenario a_seam_that_answers_a_string() {
    FunnelScenario scenario;
    scenario.label = "a-seam-that-answers-a-string";
    scenario.answers = "mistyped";
    scenario.refused = true;
    return scenario;
}

FunnelScenario a_seam_that_answers_the_contract() {
    FunnelScenario scenario;
    scenario.label = "a-seam-that-answers-the-contract";
    scenario.answers = "plain";
    return scenario;
}

class FunnelRun {
    FunnelScenario declared;
    Plant planted = PLANT_NONE;
    Variant settled;
    int misuses = 0;
    int stages = 0;

public:
    FunnelRun(const FunnelScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        const Ref<Script> script = netw_test::script_from(ANSWER_FIXTURE);
        REQUIRE(script.is_valid());
        const Ref<RefCounted> fixture = script->call("new");
        REQUIRE(fixture.is_valid());

        Ref<NetwMultiplayer> session;
        session.instantiate();
        session->event_arm(true);

        const Variant raw = fixture->call(StringName(declared.answers));
        settled = planted == PLANT_AN_ANSWER_THAT_SKIPS_THE_FUNNEL
            ? raw
            : session->seam_settled(
                  StringName(SEAM),
                  EventPlane::GATE_SYNC,
                  ROUTE,
                  raw,
                  ERR_UNAVAILABLE
              );

        const EventRing ring(session->event_ring(ROUTE));
        for (int index = 0; index < ring.size(); index++) {
            misuses += ring.event_at(index) == EventPlane::SEAM_MISUSE ? 1 : 0;
            stages += ring.event_at(index) == EventPlane::GATE_SYNC ? 1 : 0;
        }
    }

    const FunnelScenario &scenario() const {
        return declared;
    }

    const Variant &read() const {
        return settled;
    }

    int refusals() const {
        return misuses;
    }

    int stage_rows() const {
        return stages;
    }
};

typedef LawRowFor<FunnelRun> FunnelLaw;

LawVerdict law_typed(const FunnelRun &p_run) {
    if (p_run.read().get_type() == Variant::OBJECT) {
        return law_broken(
            "the session read an object where the contract names an int"
        );
    }
    if (p_run.read().get_type() != Variant::INT) {
        return law_broken(
            "the session read a %d where the contract names an int",
            int(p_run.read().get_type())
        );
    }
    return law_held();
}

LawVerdict law_stands(const FunnelRun &p_run) {
    const int64_t expected = p_run.scenario().refused
        ? int64_t(ERR_UNAVAILABLE)
        : int64_t(ERR_UNAUTHORIZED);
    if (p_run.read().get_type() != Variant::INT
        || int64_t(p_run.read()) != expected) {
        return law_broken(
            "the session settled on %d where the contract names %d",
            int(p_run.read().get_type() == Variant::INT ? int64_t(p_run.read())
                                                        : -1),
            int(expected)
        );
    }
    return law_held();
}

LawVerdict law_reported(const FunnelRun &p_run) {
    const int expected_misuses = p_run.scenario().refused ? 1 : 0;
    if (p_run.refusals() != expected_misuses) {
        return law_broken(
            "the funnel reported %d misuses against %d",
            p_run.refusals(),
            expected_misuses
        );
    }
    const int expected_stages = p_run.scenario().refused ? 0 : 1;
    if (p_run.stage_rows() != expected_stages) {
        return law_broken(
            "the funnel reported %d stage rows against %d",
            p_run.stage_rows(),
            expected_stages
        );
    }
    return law_held();
}

const FunnelLaw L_TYPED = {
    "typed",
    "what the session reads is a value of the contract's own type",
    &law_typed,
};

const FunnelLaw L_STANDS = {
    "stands",
    "a refused answer is replaced by the default, and no other answer is",
    &law_stands,
};

const FunnelLaw L_REPORTED = {
    "reported",
    "a refusal is named as a misuse and an accepted answer as its own stage",
    &law_reported,
};

const FunnelLaw LAWS[] = {L_TYPED, L_STANDS, L_REPORTED};

TEST_CASE("[Networked][Session] the seam funnel laws hold") {
    const FunnelScenario CORPUS[] = {
        a_seam_that_awaits(),
        a_seam_that_answers_a_string(),
        a_seam_that_answers_the_contract(),
    };
    for (const FunnelScenario &scenario : CORPUS) {
        const FunnelRun run(scenario);
        for (const FunnelLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE("[Networked][Session] a coroutine taken as the verdict reds typed") {
    const FunnelScenario scenario = a_seam_that_awaits();
    const FunnelRun run(scenario, PLANT_AN_ANSWER_THAT_SKIPS_THE_FUNNEL);
    NETW_CELL(L_TYPED, scenario);
    NETW_LAW_BREAKS(L_TYPED, run);
}

TEST_CASE("[Networked][Session] a string taken as the verdict reds typed") {
    const FunnelScenario scenario = a_seam_that_answers_a_string();
    const FunnelRun run(scenario, PLANT_AN_ANSWER_THAT_SKIPS_THE_FUNNEL);
    NETW_CELL(L_TYPED, scenario);
    NETW_LAW_BREAKS(L_TYPED, run);
}

TEST_CASE(
    "[Networked][Session] an answer that skips the funnel reds "
    "reported"
) {
    const FunnelScenario scenario = a_seam_that_awaits();
    const FunnelRun run(scenario, PLANT_AN_ANSWER_THAT_SKIPS_THE_FUNNEL);
    NETW_CELL(L_REPORTED, scenario);
    NETW_LAW_BREAKS(L_REPORTED, run);
}

} // namespace TestNetwSessionSeamFunnelLaws

#endif

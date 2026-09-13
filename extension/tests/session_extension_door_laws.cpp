#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"

#include "godot/multiplayer.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/wire/registry.hpp"
#include "support/declared_seams.h"
#include "support/minted_script.h"

namespace TestNetwSessionExtensionDoorLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

const char *CALLTHROUGH = netw_test::gdsrc::GATE_THAT_CALLS_THROUGH;
const char *FLIPPING = netw_test::gdsrc::GATE_THAT_REFUSES_WHAT_STOCK_ADMITS;
constexpr int64_t ROUTE = 7;
constexpr int64_t ABSENT_ROUTE = 99;
constexpr int64_t CLIENT_PEER = 2;

enum Plant {
    PLANT_NONE,
    PLANT_A_STOCK_SESSION_WEARING_THE_SCRIPT,
    PLANT_AN_INNER_NOBODY_ADOPTED,
};

struct DoorScenario {
    String label;
    const char *implementation = nullptr;
    bool binds_the_route = false;
    Error answers = OK;
    NetwMultiplayer::Stat lands_on = NetwMultiplayer::STAT_VERDICT_SKIP;
    int64_t gate_calls = 0;
};

DoorScenario no_implementation_at_all() {
    DoorScenario scenario;
    scenario.label = "no-implementation-at-all";
    scenario.answers = ERR_DOES_NOT_EXIST;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_DOES_NOT_EXIST;
    return scenario;
}

DoorScenario a_gate_that_calls_through() {
    DoorScenario scenario;
    scenario.label = "a-gate-that-calls-through";
    scenario.implementation = CALLTHROUGH;
    scenario.answers = ERR_DOES_NOT_EXIST;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_DOES_NOT_EXIST;
    scenario.gate_calls = 1;
    return scenario;
}

DoorScenario a_gate_that_refuses_what_stock_admits() {
    DoorScenario scenario;
    scenario.label = "a-gate-that-refuses-what-stock-admits";
    scenario.implementation = FLIPPING;
    scenario.binds_the_route = true;
    scenario.answers = ERR_UNAUTHORIZED;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_UNAUTHORIZED;
    scenario.gate_calls = 1;
    return scenario;
}

DoorScenario a_call_through_on_a_bound_route() {
    DoorScenario scenario;
    scenario.label = "a-call-through-on-a-bound-route";
    scenario.implementation = CALLTHROUGH;
    scenario.binds_the_route = true;
    scenario.answers = OK;
    scenario.gate_calls = 1;
    return scenario;
}

const NetwMultiplayer::Stat VERDICT_STATS[] = {
    NetwMultiplayer::STAT_VERDICT_DOES_NOT_EXIST,
    NetwMultiplayer::STAT_VERDICT_SKIP,
    NetwMultiplayer::STAT_VERDICT_UNAVAILABLE,
    NetwMultiplayer::STAT_VERDICT_UNAUTHORIZED,
    NetwMultiplayer::STAT_VERDICT_INVALID_DATA,
    NetwMultiplayer::STAT_VERDICT_BUSY,
};

class DoorRun {
    DoorScenario declared;
    Plant planted = PLANT_NONE;
    bool script_is_the_one_named = false;
    bool inner_is_the_one_given = false;
    bool inner_is_the_one_adopted = false;
    int64_t calls = 0;
    Error answered = OK;
    int64_t stats[6] = {0, 0, 0, 0, 0, 0};
    int entity_state = -1;

public:
    DoorRun(const DoorScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<Script> implementation;
        if (declared.implementation != nullptr) {
            implementation = netw_test::script_from(declared.implementation);
            REQUIRE(implementation.is_valid());
        }
        if (planted == PLANT_A_STOCK_SESSION_WEARING_THE_SCRIPT) {
            implementation = netw_test::script_from(CALLTHROUGH);
        }

        Ref<SceneMultiplayer> inner;
        inner.instantiate();
        const bool constructs_with_a_script = declared.implementation != nullptr
            || planted == PLANT_A_STOCK_SESSION_WEARING_THE_SCRIPT;
        Ref<NetwMultiplayer> session = NetwMultiplayer::make(
            inner,
            constructs_with_a_script ? implementation : Ref<Script>()
        );
        REQUIRE(session.is_valid());
        session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);

        const Ref<Script> worn = session->get_script();
        script_is_the_one_named = declared.implementation == nullptr
            ? worn.is_null()
            : worn == implementation;
        inner_is_the_one_given = session->session_get_inner() == inner;

        Node3D *body = memnew(Node3D);
        netw::gd::scene_root()->add_child(body);
        const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
        const RID entity = session->entity_of(body);
        if (declared.binds_the_route) {
            session->liveness_bind_route(ROUTE, wrapper.ptr());
        }

        PackedByteArray payload;
        payload.push_back(0);
        payload.push_back(0);
        answered = session->receive_carrier(
            netw::NetwMultiplayer::frame_pack(
                declared.binds_the_route ? ROUTE : ABSENT_ROUTE,
                0,
                netw::wire::builtin_channel("SYNC"),
                payload,
                String()
            ),
            CLIENT_PEER,
            true,
            -1,
            -1
        );
        if (implementation.is_valid()) {
            calls = int64_t(session->get(StringName("sync_calls")));
        }
        for (int index = 0; index < 6; index++) {
            stats[index] = session->stats_get(VERDICT_STATS[index]);
        }
        entity_state = session->entity_get_state(entity);

        Ref<SceneMultiplayer> second;
        second.instantiate();
        if (planted != PLANT_AN_INNER_NOBODY_ADOPTED) {
            session->embed_adopt_inner(second);
        }
        inner_is_the_one_adopted = session->session_get_inner() == second;

        session->embed_dispose();
        body->queue_free();
    }

    const DoorScenario &scenario() const {
        return declared;
    }

    bool wears_what_was_named() const {
        return script_is_the_one_named;
    }

    bool holds_what_was_given() const {
        return inner_is_the_one_given;
    }

    bool holds_what_was_adopted() const {
        return inner_is_the_one_adopted;
    }

    int64_t gate_calls() const {
        return calls;
    }

    Error verdict() const {
        return answered;
    }

    int64_t stat_at(int p_index) const {
        return stats[p_index];
    }

    int state() const {
        return entity_state;
    }
};

typedef LawRowFor<DoorRun> DoorLaw;

LawVerdict law_owned(const DoorRun &p_run) {
    if (!p_run.wears_what_was_named()) {
        return law_broken(
            "the session runs a script the caller did not name, or none"
        );
    }
    if (!p_run.holds_what_was_given()) {
        return law_broken("the session holds a transport nobody handed it");
    }
    return law_held();
}

LawVerdict law_consulted(const DoorRun &p_run) {
    if (p_run.gate_calls() != p_run.scenario().gate_calls) {
        return law_broken(
            "the installed gate answered %d frames against %d",
            int(p_run.gate_calls()),
            int(p_run.scenario().gate_calls)
        );
    }
    if (p_run.verdict() != p_run.scenario().answers) {
        return law_broken(
            "the carrier answered %d where the door names %d",
            int(p_run.verdict()),
            int(p_run.scenario().answers)
        );
    }
    return law_held();
}

LawVerdict law_counted(const DoorRun &p_run) {
    for (int index = 0; index < 6; index++) {
        const int64_t expected = p_run.scenario().answers != OK
                && VERDICT_STATS[index] == p_run.scenario().lands_on
            ? 1
            : 0;
        if (p_run.stat_at(index) != expected) {
            return law_broken(
                "verdict stat %d reads %d against %d",
                index,
                int(p_run.stat_at(index)),
                int(expected)
            );
        }
    }
    return law_held();
}

LawVerdict law_unmutated(const DoorRun &p_run) {
    if (!p_run.scenario().binds_the_route) {
        return law_held();
    }
    if (p_run.state() != int(NetwMultiplayer::ENTITY_STATE_LIVE)) {
        return law_broken(
            "a refused frame left the entity in state %d",
            p_run.state()
        );
    }
    return law_held();
}

LawVerdict law_reseated(const DoorRun &p_run) {
    if (!p_run.holds_what_was_adopted()) {
        return law_broken("adopting a transport left the session on the old");
    }
    return law_held();
}

const DoorLaw L_OWNED = {
    "owned",
    "the session runs the implementation it was constructed with and no other",
    &law_owned,
};

const DoorLaw L_CONSULTED = {
    "consulted",
    "an installed gate is the one that answers, and its answer is the verdict",
    &law_consulted,
};

const DoorLaw L_COUNTED = {
    "counted",
    "a gate's own refusal is counted the way the stock gate's would be",
    &law_counted,
};

const DoorLaw L_UNMUTATED = {
    "unmutated",
    "a gate that refuses stops the frame before anything it names changes",
    &law_unmutated,
};

const DoorLaw L_RESEATED = {
    "reseated",
    "adopting a transport re-seats the core, so nothing reads a stale one",
    &law_reseated,
};

const DoorLaw LAWS[]
    = {L_OWNED, L_CONSULTED, L_COUNTED, L_UNMUTATED, L_RESEATED};

TEST_CASE("[Networked][Session] the extension door laws hold") {
    const DoorScenario CORPUS[] = {
        no_implementation_at_all(),
        a_gate_that_calls_through(),
        a_gate_that_refuses_what_stock_admits(),
        a_call_through_on_a_bound_route(),
    };
    for (const DoorScenario &scenario : CORPUS) {
        const DoorRun run(scenario);
        for (const DoorLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE("[Networked][Session] a stock session wearing a script reds owned") {
    const DoorScenario scenario = no_implementation_at_all();
    const DoorRun run(scenario, PLANT_A_STOCK_SESSION_WEARING_THE_SCRIPT);
    NETW_CELL(L_OWNED, scenario);
    NETW_LAW_BREAKS(L_OWNED, run);
}

TEST_CASE("[Networked][Session] a transport nobody adopted reds reseated") {
    const DoorScenario scenario = a_gate_that_calls_through();
    const DoorRun run(scenario, PLANT_AN_INNER_NOBODY_ADOPTED);
    NETW_CELL(L_RESEATED, scenario);
    NETW_LAW_BREAKS(L_RESEATED, run);
}

} // namespace TestNetwSessionExtensionDoorLaws

#endif

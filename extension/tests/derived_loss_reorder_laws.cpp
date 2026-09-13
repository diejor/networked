#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "netw/api/context.hpp"

namespace TestDerivedLossReorderLaws {

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
constexpr double POLL_PERIOD_MS = 1000.0 / 60.0;
constexpr int HEAL_TICKS = 6;
constexpr int CHATTY_TICKS = 8;

const char *PLAIN_BODY = "res://tests/support/chains/state_base.gd";
const char *MASKED_BODY = netw_test::gdsrc::STATE_MASKED;
const char *SCORE_BODY = "res://tests/support/chains/state_leaf.gd";

FlowCapture CAPTURE;

void on_state_applied(Dictionary p_header) {
    CAPTURE.note(p_header, StringName("position"));
}

enum Plant {
    PLANT_NONE,
    PLANT_THE_LINK_STAYS_ORDERED,
    PLANT_THE_AUTHOR_REWINDS,
    PLANT_THE_LINK_GOES_DARK,
    PLANT_ONLY_THE_SERVER_TALKS,
};

struct LossScenario {
    String label;
    const char *body = PLAIN_BODY;
    int64_t seed = 120;
    int ticks = 120;
    bool property_door = false;
};

LossScenario reordered_state() {
    LossScenario scenario;
    scenario.label = "reordered-state";
    return scenario;
}

LossScenario masked_state() {
    LossScenario scenario;
    scenario.label = "masked-state";
    scenario.body = MASKED_BODY;
    scenario.seed = 53;
    return scenario;
}

LossScenario property_door() {
    LossScenario scenario;
    scenario.label = "property-door";
    scenario.body = SCORE_BODY;
    scenario.seed = 77;
    scenario.property_door = true;
    return scenario;
}

struct Evidence {
    int64_t applied = 0;
    int64_t regressions = 0;
    int64_t repeats = 0;
    int64_t torn = 0;
    int64_t stale_drops = 0;
    double healed_mirror = 0.0;
    double healed_authority = 0.0;
    int64_t score_regressions = 0;
    int64_t score_samples = 0;
    int64_t healed_score = 0;
    int64_t final_score = 0;
    int64_t server_standalone_spent = 0;
    int64_t client_standalone_spent = 0;
    int64_t server_acks_out = 0;
    int64_t client_acks_out = 0;
    int64_t server_acks_in = 0;
    int64_t client_acks_in = 0;
    int64_t server_peer_ack = -1;
    int64_t client_peer_ack = -1;
};

int64_t stat_of(godot::Object *p_core, const char *p_name) {
    return int64_t(
        netw_test::flow_core(p_core)->stats_snapshot().get(p_name, 0)
    );
}

class LossRun {
    LossScenario declared;
    Plant planted = PLANT_NONE;
    Evidence seen;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    void impair(LoopbackRig &p_rig) {
        if (plant_is(PLANT_THE_LINK_STAYS_ORDERED)) {
            return;
        }
        if (plant_is(PLANT_THE_LINK_GOES_DARK)) {
            const Ref<netw::LocalLinkConditions> dark
                = netw::LocalLinkConditions::create(declared.seed);
            dark->set_packet_loss(1.0);
            p_rig.conditions(0, dark, 1);
            return;
        }
        p_rig.conditions(
            0,
            netw_test::impairment(declared.seed, POLL_PERIOD_MS),
            1
        );
    }

    void heal(LoopbackRig &p_rig) {
        if (plant_is(PLANT_THE_LINK_GOES_DARK)) {
            return;
        }
        p_rig.conditions(0, netw::LocalLinkConditions::perfect(), 1);
        p_rig.conditions(-1, netw::LocalLinkConditions::perfect(), 1);
    }

    int64_t authored_tick_for(int p_step, int p_ticks) const {
        return plant_is(PLANT_THE_AUTHOR_REWINDS) ? int64_t(p_ticks - p_step)
                                                  : int64_t(p_step + 1);
    }

    void drive_state(LoopbackRig &p_rig, const FlowPair &p_pair) {
        for (int step = 0; step < declared.ticks; ++step) {
            const int64_t tick = authored_tick_for(step, declared.ticks);
            netw_test::author_at(
                p_pair.authored,
                StringName("position"),
                authored_value(tick),
                NetwPropertySet::RECORD_STATE,
                tick
            );
            p_rig.step_ticks(1);
        }
        const int64_t settled
            = authored_tick_for(declared.ticks - 1, declared.ticks);
        heal(p_rig);
        p_rig.step_ticks(HEAL_TICKS);
        seen.healed_mirror = p_pair.mirror(0)->get_position().x;
        seen.healed_authority = authored_value(settled).x;
    }

    void drive_property(LoopbackRig &p_rig, const FlowPair &p_pair) {
        int64_t held = 0;
        for (int step = 0; step < declared.ticks; ++step) {
            const int64_t value = plant_is(PLANT_THE_AUTHOR_REWINDS)
                ? declared.ticks - step
                : step + 1;
            p_pair.authored->set(StringName("score"), value);
            netw::Netw::sync_property(p_pair.authored, StringName("score"));
            p_rig.step_ticks(1);
            const int64_t read
                = int64_t(p_pair.mirror(0)->get(StringName("score")));
            seen.score_samples += 1;
            seen.score_regressions += read < held ? 1 : 0;
            held = read;
        }
        heal(p_rig);
        netw::Netw::sync_property(p_pair.authored, StringName("score"));
        p_rig.step_ticks(4);
        seen.healed_score = int64_t(p_pair.mirror(0)->get(StringName("score")));
        seen.final_score = int64_t(p_pair.authored->get(StringName("score")));

        const int64_t server_before
            = stat_of(p_rig.server(), "standalone_acks_out");
        const int64_t client_before
            = stat_of(p_rig.client(0), "standalone_acks_out");
        for (int step = 0; step < CHATTY_TICKS; ++step) {
            if (!plant_is(PLANT_ONLY_THE_SERVER_TALKS)) {
                p_pair.mirror(0)->set(StringName("score"), 200 + step);
                netw::Netw::sync_property(
                    p_pair.mirror(0),
                    StringName("score")
                );
            }
            netw::Netw::sync_property(p_pair.authored, StringName("score"));
            p_rig.step_ticks(1);
        }
        seen.server_standalone_spent
            = stat_of(p_rig.server(), "standalone_acks_out") - server_before;
        seen.client_standalone_spent
            = stat_of(p_rig.client(0), "standalone_acks_out") - client_before;
        seen.server_acks_out = stat_of(p_rig.server(), "state_acks_out");
        seen.client_acks_out = stat_of(p_rig.client(0), "state_acks_out");
        seen.server_acks_in = stat_of(p_rig.server(), "state_acks_in");
        seen.client_acks_in = stat_of(p_rig.client(0), "state_acks_in");
        seen.server_peer_ack
            = netw_test::flow_core(p_rig.server())->peer_ack(p_rig.peer_id(0));
        seen.client_peer_ack
            = netw_test::flow_core(p_rig.client(0))->peer_ack(1);
    }

public:
    explicit LossRun(const LossScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        const FlowPair pair
            = netw_test::stand_flow_pair(rig, p_scenario.body, "LossBody");
        netw_test::steer(pair, rig.peer_id(0));
        if (p_scenario.property_door) {
            netw::Netw::configure_property(
                pair.authored,
                StringName("score"),
                false
            )
                ->unreliable();
        }

        const Ref<netw::NetwPropertySetBinding> mirror_state
            = netw_test::binding_of(
                pair.mirror(0),
                NetwPropertySet::RECORD_STATE
            );
        REQUIRE_MESSAGE(
            mirror_state.is_valid(),
            "the mirror holds no state set"
        );
        CAPTURE.reset();
        mirror_state->on_applied = callable_mp_static(&on_state_applied);
        impair(rig);

        if (p_scenario.property_door) {
            drive_property(rig, pair);
        } else {
            drive_state(rig, pair);
        }

        mirror_state->on_applied = Callable();
        seen.applied = CAPTURE.applied;
        seen.regressions = CAPTURE.regressions;
        seen.repeats = CAPTURE.repeats;
        seen.torn = CAPTURE.torn;
        CAPTURE.reset();
        seen.stale_drops = stat_of(rig.client(0), "sync_drops_stale");
    }

    const LossScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<LossRun> LossLaw;

LawVerdict law_forward(const LossRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (p_run.scenario().property_door) {
        return law_held();
    }
    if (seen.applied <= 10) {
        return law_broken(
            "%d frames survived the impairment, too few to read",
            int(seen.applied)
        );
    }
    if (seen.regressions != 0) {
        return law_broken(
            "%d applied frames rewound the stream",
            int(seen.regressions)
        );
    }
    if (seen.torn != 0) {
        return law_broken(
            "%d merged rows carried a value the authority never held",
            int(seen.torn)
        );
    }
    return law_held();
}

const LossLaw L_FORWARD = {
    "forward",
    "applied state moves strictly forward however the link reorders, "
    "duplicates or drops, and no merged row carries a value the authority "
    "never held",
    &law_forward,
};

LawVerdict law_gated(const LossRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.stale_drops <= 0) {
        return law_broken(
            "the receiver dropped no stale datagram, so the order held by "
            "luck rather than by the gate"
        );
    }
    return law_held();
}

const LossLaw L_GATED = {
    "gated",
    "the order held because the sequence gate dropped superseded datagrams, "
    "not because the link happened to stay ordered",
    &law_gated,
};

LawVerdict law_converges(const LossRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (p_run.scenario().property_door) {
        if (seen.healed_score != seen.final_score) {
            return law_broken(
                "a healed link left the mirror on %d against the authority's "
                "%d",
                int(seen.healed_score),
                int(seen.final_score)
            );
        }
        return law_held();
    }
    if (seen.healed_mirror != seen.healed_authority) {
        return law_broken(
            "a healed link left the mirror on %d against the authority's %d",
            int(seen.healed_mirror),
            int(seen.healed_authority)
        );
    }
    return law_held();
}

const LossLaw L_CONVERGES = {
    "converges",
    "once the link heals the receiver lands on the exact final authored "
    "value rather than staying stuck on a stale row",
    &law_converges,
};

LawVerdict law_monotonic(const LossRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().property_door) {
        return law_held();
    }
    if (seen.score_samples <= 10) {
        return law_broken("%d samples is too few", int(seen.score_samples));
    }
    if (seen.score_regressions != 0) {
        return law_broken(
            "the observed value moved backward %d times",
            int(seen.score_regressions)
        );
    }
    return law_held();
}

const LossLaw L_MONOTONIC = {
    "monotonic",
    "an on-demand unreliable property never moves backward at the receiver "
    "however the link reordered or duplicated the sends",
    &law_monotonic,
};

LawVerdict law_piggyback(const LossRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!p_run.scenario().property_door) {
        return law_held();
    }
    if (seen.server_standalone_spent != 0
        || seen.client_standalone_spent != 0) {
        return law_broken(
            "a chatty pair still spent %d and %d standalone acks",
            int(seen.server_standalone_spent),
            int(seen.client_standalone_spent)
        );
    }
    if (seen.server_acks_out <= 0 || seen.client_acks_out <= 0) {
        return law_broken(
            "%d and %d acked datagrams left the pair",
            int(seen.server_acks_out),
            int(seen.client_acks_out)
        );
    }
    if (seen.server_acks_in <= 0 || seen.client_acks_in <= 0) {
        return law_broken(
            "%d and %d acked datagrams reached the pair",
            int(seen.server_acks_in),
            int(seen.client_acks_in)
        );
    }
    if (seen.server_peer_ack < 0 || seen.client_peer_ack < 0) {
        return law_broken(
            "the pair recorded %d and %d as the other's confirmed baseline",
            int(seen.server_peer_ack),
            int(seen.client_peer_ack)
        );
    }
    return law_held();
}

const LossLaw L_PIGGYBACK = {
    "piggyback",
    "a pair each side of which sends carries every ack on a datagram it was "
    "sending anyway, and each records the other's echo as a baseline",
    &law_piggyback,
};

const LossLaw LAWS[]
    = {L_FORWARD, L_GATED, L_CONVERGES, L_MONOTONIC, L_PIGGYBACK};

TEST_CASE("[Networked][Sync][SceneTree] the loss and reorder laws hold") {
    const LossScenario CORPUS[]
        = {reordered_state(), masked_state(), property_door()};
    for (const LossScenario &scenario : CORPUS) {
        const LossRun run(scenario);
        for (const LossLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a link that never reorders reds "
    "gated"
) {
    const LossScenario scenario = reordered_state();
    const LossRun run(scenario, PLANT_THE_LINK_STAYS_ORDERED);
    NETW_CELL(L_GATED, scenario);
    NETW_LAW_BREAKS(L_GATED, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an authority that rewinds its own "
    "ticks reds forward"
) {
    const LossScenario scenario = reordered_state();
    const LossRun run(scenario, PLANT_THE_AUTHOR_REWINDS);
    NETW_CELL(L_FORWARD, scenario);
    NETW_LAW_BREAKS(L_FORWARD, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a link that goes dark reds "
    "converges"
) {
    const LossScenario scenario = reordered_state();
    const LossRun run(scenario, PLANT_THE_LINK_GOES_DARK);
    NETW_CELL(L_CONVERGES, scenario);
    NETW_LAW_BREAKS(L_CONVERGES, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an authority that rewinds its property "
    "reds monotonic"
) {
    const LossScenario scenario = property_door();
    const LossRun run(scenario, PLANT_THE_AUTHOR_REWINDS);
    NETW_CELL(L_MONOTONIC, scenario);
    NETW_LAW_BREAKS(L_MONOTONIC, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a pair only the server talks on reds "
    "piggyback"
) {
    const LossScenario scenario = property_door();
    const LossRun run(scenario, PLANT_ONLY_THE_SERVER_TALKS);
    NETW_CELL(L_PIGGYBACK, scenario);
    NETW_LAW_BREAKS(L_PIGGYBACK, run);
}

} // namespace TestDerivedLossReorderLaws

#endif

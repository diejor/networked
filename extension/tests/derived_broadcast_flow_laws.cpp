#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

namespace TestDerivedBroadcastFlowLaws {

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
constexpr int CLEAN_TICKS = 40;
constexpr int LOSSY_TICKS = 80;
constexpr int HEAL_TICKS = 6;
constexpr int QUIET_TICKS = 6;

const char *BROADCAST_BODY = netw_test::gdsrc::BROADCAST_MASKED_AIM;
const char *STATE_BODY = "res://tests/support/chains/state_base.gd";

FlowCapture CAPTURE;

void on_broadcast_applied(Dictionary p_header) {
    CAPTURE.note(p_header, StringName("aim_dir"));
}

enum Plant {
    PLANT_NONE,
    PLANT_NOTHING_IS_AUTHORED,
    PLANT_THE_ECHO_NEVER_ARRIVES,
    PLANT_THE_STREAM_IS_A_STATE_SET,
    PLANT_THE_LINK_GOES_DARK,
    PLANT_THE_AUTHOR_NEVER_STOPS,
};

struct BroadcastScenario {
    String label;
    int64_t seed = 53;
};

BroadcastScenario silent_observer() {
    BroadcastScenario scenario;
    scenario.label = "silent-observer";
    return scenario;
}

BroadcastScenario other_seed() {
    BroadcastScenario scenario;
    scenario.label = "other-seed";
    scenario.seed = 17;
    return scenario;
}

struct Evidence {
    int64_t author_rows_out = 0;
    int64_t observer_frames_in = 0;
    Vector2 clean_observed;
    int64_t observer_standalone_acks = 0;
    int64_t author_peer_ack = -1;
    bool observer_holds_broadcast = false;
    bool observer_holds_timeline = false;
    int64_t torn = 0;
    Vector2 healed;
    Vector2 settled;
    int64_t rows_before_quiet = 0;
    int64_t rows_after_quiet = 0;
};

int64_t stat_of(godot::Object *p_core, const char *p_name) {
    return int64_t(
        netw_test::flow_core(p_core)->stats_snapshot().get(p_name, 0)
    );
}

class BroadcastRun {
    BroadcastScenario declared;
    Plant planted = PLANT_NONE;
    Evidence seen;
    int64_t last_authored = 0;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    NetwPropertySet::Record record() const {
        return plant_is(PLANT_THE_STREAM_IS_A_STATE_SET)
            ? NetwPropertySet::RECORD_STATE
            : NetwPropertySet::RECORD_BROADCAST;
    }

    StringName field() const {
        return plant_is(PLANT_THE_STREAM_IS_A_STATE_SET)
            ? StringName("position")
            : StringName("aim_dir");
    }

    void author(LoopbackRig &p_rig, const FlowPair &p_pair, int p_ticks) {
        for (int step = 0; step < p_ticks; ++step) {
            if (!plant_is(PLANT_NOTHING_IS_AUTHORED)) {
                last_authored = netw_test::clock_tick(p_rig, 0) + 1;
                netw_test::author_at(
                    p_pair.mirror(0),
                    field(),
                    authored_value(last_authored),
                    record(),
                    last_authored
                );
            }
            p_rig.step_ticks(1);
        }
    }

public:
    explicit BroadcastRun(
        const BroadcastScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(1);
        rig.mount();
        netw_test::flow_clocks(rig, TICKRATE);
        const char *body = plant_is(PLANT_THE_STREAM_IS_A_STATE_SET)
            ? STATE_BODY
            : BROADCAST_BODY;
        const FlowPair pair
            = netw_test::stand_flow_pair(rig, body, "BroadcastBody");
        netw_test::steer(pair, rig.peer_id(0));

        const Ref<netw::NetwPropertySetBinding> observed
            = netw_test::binding_of(pair.authored, record());
        REQUIRE_MESSAGE(observed.is_valid(), "the observer holds no set");
        CAPTURE.reset();
        observed->on_applied = callable_mp_static(&on_broadcast_applied);

        if (plant_is(PLANT_THE_ECHO_NEVER_ARRIVES)) {
            rig.hold(0);
        }
        author(rig, pair, CLEAN_TICKS);
        seen.author_rows_out = stat_of(rig.client(0), "row_frames_out");
        seen.observer_frames_in = stat_of(rig.server(), "derived_frames_in");
        seen.clean_observed = pair.authored->get(field());
        seen.observer_standalone_acks
            = stat_of(rig.server(), "standalone_acks_out");
        seen.author_peer_ack = netw_test::flow_core(rig.client(0))->peer_ack(1);
        const Ref<netw::NetwEntity> observer
            = netw::NetwEntity::of(pair.authored);
        seen.observer_holds_broadcast
            = observer->get_broadcast_binding().is_valid();
        seen.observer_holds_timeline = observer->get_timeline().is_valid();

        if (!plant_is(PLANT_THE_LINK_GOES_DARK)) {
            rig.conditions(
                -1,
                netw_test::impairment(p_scenario.seed, POLL_PERIOD_MS),
                rig.peer_id(0)
            );
        } else {
            const Ref<netw::LocalLinkConditions> dark
                = netw::LocalLinkConditions::create(p_scenario.seed);
            dark->set_packet_loss(1.0);
            rig.conditions(-1, dark, rig.peer_id(0));
        }
        author(rig, pair, LOSSY_TICKS);
        seen.torn = CAPTURE.torn;

        if (!plant_is(PLANT_THE_LINK_GOES_DARK)) {
            rig.conditions(
                -1,
                netw::LocalLinkConditions::perfect(),
                rig.peer_id(0)
            );
        }
        if (plant_is(PLANT_THE_ECHO_NEVER_ARRIVES)) {
            rig.release(0);
        }
        rig.step_ticks(HEAL_TICKS);
        seen.healed = pair.authored->get(field());
        seen.settled = authored_value(last_authored);

        seen.rows_before_quiet = stat_of(rig.client(0), "row_frames_out");
        if (plant_is(PLANT_THE_AUTHOR_NEVER_STOPS)) {
            author(rig, pair, QUIET_TICKS);
        } else {
            rig.step_ticks(QUIET_TICKS);
        }
        seen.rows_after_quiet = stat_of(rig.client(0), "row_frames_out");

        observed->on_applied = Callable();
        CAPTURE.reset();
    }

    const BroadcastScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<BroadcastRun> BroadcastLaw;

LawVerdict law_flowing(const BroadcastRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.author_rows_out <= 0) {
        return law_broken("the author sent no row frame");
    }
    if (seen.observer_frames_in <= 0) {
        return law_broken("the observer applied no derived frame");
    }
    if (seen.clean_observed.x <= 0.0) {
        return law_broken(
            "the observer holds %d, so no authored value ever crossed",
            int(seen.clean_observed.x)
        );
    }
    return law_held();
}

const BroadcastLaw L_FLOWING = {
    "flowing",
    "a controller-authored broadcast reaches an observer that authors nothing "
    "back",
    &law_flowing,
};

LawVerdict law_echo(const BroadcastRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.observer_standalone_acks <= 0) {
        return law_broken(
            "an observer with nothing of its own to send spent no standalone "
            "ack"
        );
    }
    if (seen.author_peer_ack < 0) {
        return law_broken(
            "the author recorded %d as the observer's confirmed baseline",
            int(seen.author_peer_ack)
        );
    }
    return law_held();
}

const BroadcastLaw L_ECHO = {
    "echo",
    "an observer that sends the author nothing still promotes the author's "
    "per-recipient baseline, through the standalone ack it emits at "
    "end-of-tick",
    &law_echo,
};

LawVerdict law_untimelined(const BroadcastRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (!seen.observer_holds_broadcast) {
        return law_broken("the observer holds no broadcast binding");
    }
    if (seen.observer_holds_timeline) {
        return law_broken("a trusted stream grew a rewind timeline");
    }
    return law_held();
}

const BroadcastLaw L_UNTIMELINED = {
    "untimelined",
    "a broadcast is a trusted display stream, so the observer holds its "
    "binding and the entity grows no rewind timeline",
    &law_untimelined,
};

LawVerdict law_converges(const BroadcastRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.torn != 0) {
        return law_broken(
            "%d merged rows carried a value the author never held",
            int(seen.torn)
        );
    }
    if (!seen.healed.is_equal_approx(seen.settled)) {
        return law_broken(
            "a healed link left the observer on %d against the author's %d",
            int(seen.healed.x),
            int(seen.settled.x)
        );
    }
    return law_held();
}

const BroadcastLaw L_CONVERGES = {
    "converges",
    "no merge under loss ever produces a value the author never held, and "
    "once the link recovers the baseline catches up on the exact final one",
    &law_converges,
};

LawVerdict law_quiet(const BroadcastRun &p_run) {
    const Evidence &seen = p_run.evidence();
    if (seen.rows_after_quiet != seen.rows_before_quiet) {
        return law_broken(
            "a settled stream sent %d further rows",
            int(seen.rows_after_quiet - seen.rows_before_quiet)
        );
    }
    return law_held();
}

const BroadcastLaw L_QUIET = {
    "quiet",
    "a stream whose baseline promoted and whose author stopped has nothing "
    "left to diff, so it goes quiet rather than resending forever",
    &law_quiet,
};

const BroadcastLaw LAWS[]
    = {L_FLOWING, L_ECHO, L_UNTIMELINED, L_CONVERGES, L_QUIET};

TEST_CASE("[Networked][Sync][SceneTree] the masked broadcast flow laws hold") {
    const BroadcastScenario CORPUS[] = {silent_observer(), other_seed()};
    for (const BroadcastScenario &scenario : CORPUS) {
        const BroadcastRun run(scenario);
        for (const BroadcastLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an author that never authors reds "
    "flowing"
) {
    const BroadcastScenario scenario = silent_observer();
    const BroadcastRun run(scenario, PLANT_NOTHING_IS_AUTHORED);
    NETW_CELL(L_FLOWING, scenario);
    NETW_LAW_BREAKS(L_FLOWING, run);
}

TEST_CASE("[Networked][Sync][SceneTree] an echo that never arrives reds echo") {
    const BroadcastScenario scenario = silent_observer();
    const BroadcastRun run(scenario, PLANT_THE_ECHO_NEVER_ARRIVES);
    NETW_CELL(L_ECHO, scenario);
    NETW_LAW_BREAKS(L_ECHO, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] a state set in place of a broadcast "
    "reds untimelined"
) {
    const BroadcastScenario scenario = silent_observer();
    const BroadcastRun run(scenario, PLANT_THE_STREAM_IS_A_STATE_SET);
    NETW_CELL(L_UNTIMELINED, scenario);
    NETW_LAW_BREAKS(L_UNTIMELINED, run);
}

TEST_CASE("[Networked][Sync][SceneTree] a link that goes dark reds converges") {
    const BroadcastScenario scenario = silent_observer();
    const BroadcastRun run(scenario, PLANT_THE_LINK_GOES_DARK);
    NETW_CELL(L_CONVERGES, scenario);
    NETW_LAW_BREAKS(L_CONVERGES, run);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] an author that never stops reds "
    "quiet"
) {
    const BroadcastScenario scenario = silent_observer();
    const BroadcastRun run(scenario, PLANT_THE_AUTHOR_NEVER_STOPS);
    NETW_CELL(L_QUIET, scenario);
    NETW_LAW_BREAKS(L_QUIET, run);
}

struct FanOut {
    Vector2 peer_holds;
    int64_t author_ack_of_peer = -1;
    int64_t author_ack_of_server = -1;
};

FanOut fan_out_to_a_peer_client() {
    LoopbackRig rig(2);
    rig.mount();
    netw_test::flow_clocks(rig, TICKRATE);
    const FlowPair pair
        = netw_test::stand_flow_pair(rig, BROADCAST_BODY, "BroadcastBody");
    netw_test::steer(pair, rig.peer_id(0));

    const StringName field("aim_dir");
    for (int step = 0; step < CLEAN_TICKS; ++step) {
        const int64_t authored = netw_test::clock_tick(rig, 0) + 1;
        netw_test::author_at(
            pair.mirror(0),
            field,
            authored_value(authored),
            NetwPropertySet::RECORD_BROADCAST,
            authored
        );
        rig.step_ticks(1);
    }

    FanOut seen;
    seen.peer_holds = pair.mirror(1)->get(field);
    netw::NetwMultiplayer *author = netw_test::flow_core(rig.client(0));
    seen.author_ack_of_peer = author->peer_ack(rig.peer_id(1));
    seen.author_ack_of_server = author->peer_ack(1);
    return seen;
}

TEST_CASE(
    "[Networked][Sync][SceneTree] A2 a client-authored broadcast reaches the "
    "OTHER client, because the recipient roster a non-server peer answers is "
    "the peers it can see rather than the server alone"
) {
    const FanOut seen = fan_out_to_a_peer_client();
    NETW_CHECK_GT(seen.peer_holds.x, 0.0);
}

TEST_CASE(
    "[Networked][Sync][SceneTree] A3 a peer client's ack promotes the "
    "author's baseline for that peer, so a masked stream is delta-coded "
    "against what the recipient confirmed rather than against the server's "
    "confirmation"
) {
    const FanOut seen = fan_out_to_a_peer_client();
    NETW_CHECK_GE(seen.author_ack_of_peer, 0);
}

} // namespace TestDerivedBroadcastFlowLaws

#endif

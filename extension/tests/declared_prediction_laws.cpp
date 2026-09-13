#include "support/netw_test.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/drive.hpp"
#include "netw/predict/engine.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwDeclaredPredictionLaws {

using namespace netw_test;

constexpr int GAP_FRAME = 15;

EntityDecl predicted_player(netw::Schedule p_schedule) {
    return EntityDecl()
        .named("P")
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(godot::Vector2())
        .predicted(0.01)
        .scheduled(p_schedule);
}

godot::PackedInt32Array held_twice() {
    godot::PackedInt32Array ticks;
    ticks.push_back(1);
    ticks.push_back(0);
    ticks.push_back(0);
    for (int index = 0; index < 9; ++index) {
        ticks.push_back(1);
    }
    return ticks;
}

Scenario declared_lane(
    const char *p_label,
    netw::Schedule p_schedule = netw::Schedule::TICK,
    bool p_delayed = true,
    int p_tickrate = 30
) {
    Scenario scenario;
    scenario.label = p_label;
    scenario.epsilon = 0.01;
    scenario.world.clocked(p_tickrate, 3)
        .lag_compensated()
        .player(predicted_player(p_schedule), 0);
    if (p_delayed) {
        scenario.conditions(netw::LocalLinkConditions::polls(4));
    }
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    return scenario;
}

Scenario clean_lane() {
    return declared_lane("clean-lane").until(100);
}

Scenario perturbed_lane() {
    Scenario scenario = declared_lane("perturbed-lane");
    scenario.perturb(30, "P", godot::Vector2(60.0, -40.0));
    return scenario.until(100);
}

Scenario snap_lane() {
    Scenario scenario;
    scenario.label = "snap-lane";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        EntityDecl(predicted_player(netw::Schedule::TICK))
            .corrected_by(netw::CorrectionMode::SNAP),
        0
    );
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    scenario.perturb(30, "P", godot::Vector2(60.0, -40.0));
    return scenario.until(100);
}

Scenario joint_lane() {
    Scenario scenario;
    scenario.label = "joint-lane";
    scenario.epsilon = 0.01;
    godot::Vector<godot::StringName> members;
    members.push_back(godot::StringName("Q"));
    scenario.world.clocked(30, 3)
        .lag_compensated()
        .player(predicted_player(netw::Schedule::TICK), 0)
        .player(
            EntityDecl(predicted_player(netw::Schedule::TICK)).named("Q"),
            1
        )
        .island(godot::StringName("P"), members)
        .simulating(godot::StringName("Q"));
    scenario.clients = 2;
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    scenario.hold_input(1, "Q", godot::Vector2(1.0, 0.0));
    scenario.perturb(30, "P", godot::Vector2(60.0, -40.0));
    return scenario.until(100);
}

Scenario fresh_side_effect_lane() {
    Scenario scenario = declared_lane("fresh-side-effect-lane");
    godot::Dictionary command;
    command[godot::StringName("motion")] = godot::Vector2(1.0, 0.0);
    command[godot::StringName("bombing")] = true;
    scenario.input_at(27, "P", command);
    scenario.perturb(28, "P", godot::Vector2(50.0, 50.0));
    return scenario.until(78);
}

Scenario held_frame_lane() {
    Scenario scenario
        = declared_lane("held-frame", netw::Schedule::FRAME, false);
    scenario.schedule(held_twice());
    return scenario.until(scenario.frame_ticks.size());
}

Scenario hosted_frame_lane() {
    Scenario scenario;
    scenario.label = "hosted-frame";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().hosted(
        predicted_player(netw::Schedule::FRAME)
    );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    scenario.schedule(held_twice());
    return scenario.until(scenario.frame_ticks.size());
}

Scenario buffered_frame_lane() {
    Scenario scenario;
    scenario.label = "buffered-frame";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        EntityDecl(predicted_player(netw::Schedule::FRAME))
            .missing_input(netw::MissingInput::REPEAT_LAST)
            .buffered(1),
        0
    );
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    godot::PackedInt32Array ticks;
    for (int index = 0; index < 30; ++index) {
        ticks.push_back(1);
    }
    scenario.schedule(ticks);
    return scenario.until(ticks.size());
}

Scenario gapped_frame_lane() {
    Scenario scenario = buffered_frame_lane();
    scenario.label = "gapped-frame";
    godot::PackedInt32Array ticks = scenario.frame_ticks;
    ticks.set(GAP_FRAME, 0);
    scenario.schedule(ticks);
    return scenario;
}

Scenario varied_frame_lane() {
    Scenario scenario = buffered_frame_lane();
    scenario.label = "varied-frame";
    godot::PackedInt32Array ticks;
    for (int round = 0; round < 4; ++round) {
        ticks.push_back(1);
        ticks.push_back(0);
        ticks.push_back(2);
        ticks.push_back(1);
        ticks.push_back(1);
        ticks.push_back(0);
        ticks.push_back(1);
    }
    scenario.schedule(ticks);
    return scenario.until(ticks.size());
}

Scenario deaf_frame_lane() {
    Scenario scenario = buffered_frame_lane();
    scenario.label = "deaf-frame";
    godot::Ref<netw::LocalLinkConditions> deaf
        = netw::LocalLinkConditions::create(1);
    deaf->set_packet_loss(1.0);
    scenario.inbound(deaf);
    godot::PackedInt32Array ticks;
    for (int index = 0; index < netw::predict::ACK_AGE_MAX + 16; ++index) {
        ticks.push_back(1);
    }
    scenario.schedule(ticks);
    return scenario.until(ticks.size());
}

Scenario stranded_frame_lane() {
    Scenario scenario;
    scenario.label = "stranded-frame";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().player(
        EntityDecl(predicted_player(netw::Schedule::FRAME))
            .missing_input(netw::MissingInput::REPEAT_LAST)
            .buffered(1)
            .consume_lag(2),
        0
    );
    scenario.hold_input(1, "P", godot::Vector2(1.0, 0.0));
    godot::PackedInt32Array owner_ticks;
    godot::PackedInt32Array authority_ticks;
    for (int index = 0; index < 24; ++index) {
        owner_ticks.push_back(1);
        authority_ticks.push_back(index < 6 ? 0 : 1);
    }
    scenario.schedule(owner_ticks);
    scenario.authority_schedule(authority_ticks);
    return scenario.until(owner_ticks.size());
}

Scenario held_quantum_lane() {
    Scenario scenario
        = declared_lane("held-quantum", netw::Schedule::FRAME, false, 60);
    godot::PackedInt32Array ticks;
    ticks.push_back(1);
    ticks.push_back(1);
    ticks.push_back(1);
    ticks.push_back(0);
    ticks.push_back(1);
    scenario.schedule(ticks);
    return scenario.until(ticks.size());
}

LawVerdict law_clean_agrees(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.consumed() <= 10 || p_run.decisions() <= 5) {
        return law_broken(
            "the lane consumed %d input(s) across %d verdict(s)",
            lane.consumed(),
            p_run.decisions()
        );
    }
    if (lane.corrections() != 0) {
        return law_broken(
            "a clean lane applied %d correction(s)",
            lane.corrections()
        );
    }
    const double client = lane.position_x();
    const double server = lane.authority_position_x();
    if (client <= 0.0 || server <= 0.0) {
        return law_broken(
            "the client reached %g across %d drive(s), and authority reached "
            "%g",
            client,
            lane.drives(),
            server
        );
    }
    return law_held();
}

LawVerdict law_reconverges(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.corrections() < 1) {
        return law_broken(
            "a declared perturbation produced %d correction(s)",
            lane.corrections()
        );
    }
    const double tail = lane.tail_divergence(5);
    if (tail >= lane.epsilon()) {
        return law_broken(
            "the tail diverges by %g at an epsilon of %g",
            tail,
            lane.epsilon()
        );
    }
    return law_held();
}

const LawRow L_CONV = {
    "L-CONV",
    "after its last stimulus a declared lane reconverges under epsilon",
    law_reconverges,
};

LawVerdict law_snap_adopts_without_replay(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.corrections() < 1) {
        return law_broken(
            "a declared perturbation produced %d correction(s)",
            lane.corrections()
        );
    }
    if (lane.max_replay_depth() != 0) {
        return law_broken(
            "a snapping lane walked %d tick(s) of replay",
            lane.max_replay_depth()
        );
    }
    return law_held();
}

const LawRow L_SNAP = {
    "L-SNAP",
    "under SNAP a correction lands and the replay window is never walked",
    law_snap_adopts_without_replay,
};

LawVerdict law_replay_is_bounded(const ScenarioRun &p_run) {
    const int depth = p_run.lane("P").max_replay_depth();
    if (depth <= 0 || depth >= 30) {
        return law_broken("the deepest replay was %d tick(s)", depth);
    }
    return law_held();
}

LawVerdict law_side_effect_is_fresh(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.fresh_effects() != 1 || lane.authority_fresh_effects() != 1) {
        return law_broken(
            "the client fired %d time(s) and authority fired %d",
            lane.fresh_effects(),
            lane.authority_fresh_effects()
        );
    }
    if (lane.corrections() < 1) {
        return law_broken("the replay stimulus produced no correction");
    }
    return law_held();
}

LawVerdict law_frame_authors_once(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.authoring_clamped() != 2) {
        return law_broken(
            "the owner clamped %d held frame(s)",
            lane.authoring_clamped()
        );
    }
    if (lane.journal_rows() != lane.frames() - lane.authoring_clamped()) {
        return law_broken(
            "%d frame(s) filed %d journal row(s) after %d clamp(s)",
            lane.frames(),
            lane.journal_rows(),
            lane.authoring_clamped()
        );
    }
    return law_held();
}

LawVerdict law_held_frame_charges_next_transition(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.quantum_declared() != 1) {
        return law_broken(
            "the declared cadence was %d physics step(s)",
            lane.quantum_declared()
        );
    }
    if (lane.quantum_steps() != 2 || lane.quantum_faults() != 1) {
        return law_broken(
            "the closing transition covered %d step(s) with %d fault(s)",
            lane.quantum_steps(),
            lane.quantum_faults()
        );
    }
    return law_held();
}

LawVerdict law_peers_close_alike(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.fp_verified() <= 0) {
        return law_broken(
            "authority answered for %d transition(s), so agreement is vacuous",
            lane.fp_verified()
        );
    }
    if (lane.fp_mismatches() != 0) {
        return law_broken(
            "%d of %d verified transition(s) disagreed, first at %d",
            lane.fp_mismatches(),
            lane.fp_verified(),
            lane.first_divergence()
        );
    }
    if (lane.first_divergence() != -1) {
        return law_broken(
            "no transition was counted divergent, yet %d is named as the "
            "first",
            lane.first_divergence()
        );
    }
    return law_held();
}

const LawRow L_AGREE = {
    "L-AGREE",
    "every transition authority answers for closed the same way on both peers",
    law_peers_close_alike,
};

LawVerdict law_dry_frame_claims_nothing(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.held() <= 0) {
        return law_broken(
            "authority held %d frame(s), so no dry frame was judged",
            lane.held()
        );
    }
    if (lane.authority_journal_rows() != lane.consumed()) {
        return law_broken(
            "authority filed %d row(s) for %d consumed transition(s)",
            lane.authority_journal_rows(),
            lane.consumed()
        );
    }
    const int accounted = lane.consumed() + lane.held() + lane.starved();
    if (accounted != lane.frames()) {
        return law_broken(
            "%d frame(s) account for %d consumed, %d held and %d starved",
            lane.frames(),
            lane.consumed(),
            lane.held(),
            lane.starved()
        );
    }
    return law_held();
}

LawVerdict law_holds_once_per_dry_frame(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.consumed() <= 0) {
        return law_broken("the lane consumed nothing to buffer");
    }
    const int owed = lane.buffer_declared() + lane.authoring_clamped();
    if (lane.held() != owed) {
        return law_broken(
            "a depth of %d and %d unauthored frame(s) cost %d held frame(s)",
            lane.buffer_declared(),
            lane.authoring_clamped(),
            lane.held()
        );
    }
    return law_held();
}

const LawRow L_DRY = {
    "L-DRY",
    "every authority frame consumed, held or starved, and only a consuming "
    "frame filed a row",
    law_dry_frame_claims_nothing,
};

const LawRow L_BUFFER = {
    "L-BUFFER",
    "authority holds once per declared buffer depth and once per frame the "
    "owner never authored",
    law_holds_once_per_dry_frame,
};

LawVerdict law_stranded_authority_jumps(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.drives() <= lane.consumed()) {
        return law_broken(
            "the owner authored %d transition(s) against %d consumed, so "
            "nothing ever stood ahead of authority",
            lane.drives(),
            lane.consumed()
        );
    }
    if (lane.resyncs() != 1) {
        return law_broken(
            "authority jumped to the live edge %d time(s)",
            lane.resyncs()
        );
    }
    if (lane.skipped() <= 0) {
        return law_broken(
            "authority jumped while declaring %d skipped transition(s)",
            lane.skipped()
        );
    }
    if (lane.consumed() + lane.skipped() > lane.drives()) {
        return law_broken(
            "authority accounts for %d transition(s) of the %d authored",
            lane.consumed() + lane.skipped(),
            lane.drives()
        );
    }
    if (lane.corrections() < 1) {
        return law_broken(
            "the owner applied %d correction(s) for transitions authority "
            "never ran",
            lane.corrections()
        );
    }
    return law_held();
}

const LawRow L_RESYNC = {
    "L-RESYNC",
    "authority stranded past its declared consume lag jumps to the live edge "
    "once and declares every transition it skipped",
    law_stranded_authority_jumps,
};

LawVerdict law_horizon_bounds_speculation(const ScenarioRun &p_run) {
    const Lane lane = p_run.lane("P");
    if (lane.fp_verified() != 0) {
        return law_broken(
            "authority answered for %d transition(s), so nothing bounded the "
            "horizon",
            lane.fp_verified()
        );
    }
    if (lane.drives() != netw::predict::ACK_AGE_MAX) {
        return law_broken(
            "the owner authored %d transition(s) against a ceiling of %d",
            lane.drives(),
            netw::predict::ACK_AGE_MAX
        );
    }
    if (lane.drives() + lane.speculation_held() != lane.frames()) {
        return law_broken(
            "%d frame(s) authored %d transition(s) and held %d",
            lane.frames(),
            lane.drives(),
            lane.speculation_held()
        );
    }
    return law_held();
}

const LawRow L_HORIZON = {
    "L-HORIZON",
    "an owner speculates to its acknowledgement ceiling and no further",
    law_horizon_bounds_speculation,
};

const LawRow L_CLEAN = {
    "L-CLEAN",
    "a clean declared lane advances on both peers without correcting",
    law_clean_agrees,
};

const LawRow L_REPLAY = {
    "L-REPLAY",
    "reconciliation replays a non-empty bounded in-flight window",
    law_replay_is_bounded,
};

const LawRow L_FRESH = {
    "L-FRESH",
    "a fresh side effect runs once on each peer and never during replay",
    law_side_effect_is_fresh,
};

const LawRow L_FRAME_ONCE = {
    "L-FRAME-ONCE",
    "a frame authors at most one transition and a held frame authors none",
    law_frame_authors_once,
};

LawVerdict law_joint_group_replays(const ScenarioRun &p_run) {
    const Lane owner = p_run.lane("P");
    if (owner.joint_passes() < 1) {
        return law_broken(
            "a joint island ran %d pass(es) over %d correction(s) and %d "
            "verdict(s)",
            owner.joint_passes(),
            owner.corrections(),
            p_run.decisions()
        );
    }
    if (owner.joint_members() < 2) {
        return law_broken(
            "a joint pass stepped %d member(s), so the group was not whole",
            owner.joint_members()
        );
    }
    return law_held();
}

const LawRow L_JOINT = {
    "L-JOINT",
    "a declared joint island replays its whole group",
    law_joint_group_replays,
};

const LawRow L_FRAME_QUANTUM = {
    "L-FRAME-QUANTUM",
    "a held physics frame is charged to the next transition",
    law_held_frame_charges_next_transition,
};

TEST_CASE("[Networked][Predict][Declared][Law] a clean session agrees") {
    const Scenario scenario = clean_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_CLEAN, scenario);
    NETW_LAW_HOLDS(L_CLEAN, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] clean agreement breaks under an "
    "undeclared perturbation"
) {
    const Scenario scenario = clean_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_PHANTOM_PERTURB);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_CLEAN, scenario);
    NETW_LAW_BREAKS(L_CLEAN, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a joint island replays its group"
) {
    const Scenario scenario = joint_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_JOINT, scenario);
    NETW_LAW_HOLDS(L_JOINT, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] JW1 a joint owner that corrects "
    "moves its group's STATE FLOOR, which is the only witness that the pass "
    "was handed something to replay"
) {
    const Scenario scenario = joint_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    netw::NetwMultiplayer *owner = rig.client(0);
    REQUIRE(owner != nullptr);
    netw::NetwPredictionEngine *const pool = owner->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const godot::RID seated = rig.entity_of(godot::StringName("P"), 0);
    const godot::Ref<godot::RefCounted> wrapper
        = owner->entity_get_view(seated);
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const godot::PackedInt64Array stats = pool->joint_stats(slot);
    NETW_CHECK_GE(
        stats[netw::NetwPredictionEngine::STAT_JOINT_FLOOR_STATE_MOVES],
        int64_t(1)
    );

    SUBCASE(
        "and the corrections counter stays zero, because a joint owner "
        "takes the basis branch instead of the recovery ladder"
    ) {
        NETW_CHECK_GE(
            stats[netw::NetwPredictionEngine::STAT_JOINT_PASSES],
            int64_t(1)
        );
    }
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a perturbed session reconverges"
) {
    const Scenario scenario = perturbed_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    REQUIRE(run.decisions() > 0);
    NETW_CELL(L_CONV, scenario);
    NETW_LAW_HOLDS(L_CONV, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] reconciliation breaks without "
    "recovery"
) {
    const Scenario scenario = perturbed_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_NO_RECOVER);
    REQUIRE(run.regime_reached());
    REQUIRE(run.decisions() > 0);
    NETW_CELL(L_CONV, scenario);
    NETW_LAW_BREAKS(L_CONV, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] replay stays inside the in-flight "
    "window"
) {
    const Scenario scenario = perturbed_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REPLAY, scenario);
    NETW_LAW_HOLDS(L_REPLAY, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] replay bounds break against whole-run "
    "depth"
) {
    const Scenario scenario = perturbed_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_FORGE_REPLAY_DEPTH);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_REPLAY, scenario);
    NETW_LAW_BREAKS(L_REPLAY, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] replay does not repeat a fresh side "
    "effect"
) {
    const Scenario scenario = fresh_side_effect_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRESH, scenario);
    NETW_LAW_HOLDS(L_FRESH, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] fresh-only effects break when replay "
    "is fresh"
) {
    const Scenario scenario = fresh_side_effect_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_REPLAY_FRESH);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRESH, scenario);
    NETW_LAW_BREAKS(L_FRESH, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a tick lane closes every acked "
    "transition as authority did"
) {
    const Scenario scenario = clean_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_AGREE, scenario);
    NETW_LAW_HOLDS(L_AGREE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] agreement breaks under an "
    "authority-only impulse"
) {
    const Scenario scenario = clean_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_PHANTOM_PERTURB);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_AGREE, scenario);
    NETW_LAW_BREAKS(L_AGREE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a frame lane closes every acked "
    "transition as authority did"
) {
    const Scenario scenario = held_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_AGREE, scenario);
    NETW_LAW_HOLDS(L_AGREE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a frame authors at most one "
    "transition"
) {
    const Scenario scenario = held_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRAME_ONCE, scenario);
    NETW_LAW_HOLDS(L_FRAME_ONCE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a host-controlled frame authors at "
    "most one transition"
) {
    const Scenario scenario = hosted_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRAME_ONCE, scenario);
    NETW_LAW_HOLDS(L_FRAME_ONCE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a host-controlled held frame breaks "
    "when driven"
) {
    const Scenario scenario = hosted_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_DRIVE_HELD_FRAME);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRAME_ONCE, scenario);
    NETW_LAW_BREAKS(L_FRAME_ONCE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a held frame breaks when driven"
) {
    const Scenario scenario = held_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_DRIVE_HELD_FRAME);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRAME_ONCE, scenario);
    NETW_LAW_BREAKS(L_FRAME_ONCE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a dry authority frame claims no "
    "transition"
) {
    const Scenario scenario = buffered_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_DRY, scenario);
    NETW_LAW_HOLDS(L_DRY, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a dry frame goes unjudged when "
    "authority never holds"
) {
    const Scenario scenario = buffered_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_UNBUFFERED);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_DRY, scenario);
    NETW_LAW_BREAKS(L_DRY, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a declared buffer costs its depth"
) {
    const Scenario scenario = buffered_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_BUFFER, scenario);
    NETW_LAW_HOLDS(L_BUFFER, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a buffer authority ignores costs "
    "nothing"
) {
    const Scenario scenario = buffered_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_UNBUFFERED);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_BUFFER, scenario);
    NETW_LAW_BREAKS(L_BUFFER, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] an unauthored frame costs one more "
    "hold"
) {
    const Scenario scenario = gapped_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_BUFFER, scenario);
    NETW_LAW_HOLDS(L_BUFFER, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] an unauthored frame starves an "
    "unbuffered lane instead"
) {
    const Scenario scenario = gapped_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_UNBUFFERED);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_BUFFER, scenario);
    NETW_LAW_BREAKS(L_BUFFER, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a gapped authority frame claims no "
    "transition"
) {
    const Scenario scenario = gapped_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_DRY, scenario);
    NETW_LAW_HOLDS(L_DRY, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] every frame shape replays entry for "
    "entry"
) {
    const Scenario scenario = varied_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_AGREE, scenario);
    NETW_LAW_HOLDS(L_AGREE, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a varied clock accounts for every "
    "authority frame"
) {
    const Scenario scenario = varied_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_DRY, scenario);
    NETW_LAW_HOLDS(L_DRY, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] an unanswered owner stops at its "
    "horizon"
) {
    const Scenario scenario = deaf_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_HORIZON, scenario);
    NETW_LAW_HOLDS(L_HORIZON, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] an answered owner never fills its "
    "horizon"
) {
    const Scenario scenario = deaf_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_ALWAYS_ACK);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_HORIZON, scenario);
    NETW_LAW_BREAKS(L_HORIZON, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a held frame charges the next "
    "transition"
) {
    const Scenario scenario = held_quantum_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRAME_QUANTUM, scenario);
    NETW_LAW_HOLDS(L_FRAME_QUANTUM, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] an unheld frame hides its quantum"
) {
    const Scenario scenario = held_quantum_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_DRIVE_HELD_FRAME);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_FRAME_QUANTUM, scenario);
    NETW_LAW_BREAKS(L_FRAME_QUANTUM, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] stranded authority jumps to the live "
    "edge"
) {
    const Scenario scenario = stranded_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_RESYNC, scenario);
    NETW_LAW_HOLDS(L_RESYNC, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] authority granted the owner's clock "
    "never strands"
) {
    const Scenario scenario = stranded_frame_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_LOCKSTEP_AUTHORITY);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_RESYNC, scenario);
    NETW_LAW_BREAKS(L_RESYNC, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a snapping lane adopts authority "
    "without walking its replay window"
) {
    const Scenario scenario = snap_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_SNAP, scenario);
    NETW_LAW_HOLDS(L_SNAP, run);
}

TEST_CASE(
    "[Networked][Predict][Declared][Law] a lane corrected by replay walks the "
    "window its declaration said it would not"
) {
    const Scenario scenario = snap_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run
        = ScenarioRun::session(rig, scenario, PLANT_REPLAY_UNDER_SNAP);
    REQUIRE(run.regime_reached());
    NETW_CELL(L_SNAP, scenario);
    NETW_LAW_BREAKS(L_SNAP, run);
}

} // namespace TestNetwDeclaredPredictionLaws

#endif

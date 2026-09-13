#pragma once

#include "netw_test.h"

#include "lane.h"
#include "membership.h"
#include "occupancy.h"
#include "scenario.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/predict/drive.hpp"
#include "netw/prediction_core.hpp"

#if defined(NETW_TIER_HOSTED)
#include <memory>
#include <vector>

#include "loopback_rig.h"
#include "netw_recorder.h"
#endif

namespace netw_test {

enum Plant {
    PLANT_NONE,
    PLANT_NO_RECOVER,
    PLANT_DRIVE_EVERY_TICK,
    PLANT_CARRY_STREAK,
    PLANT_PHANTOM_PERTURB,
    PLANT_SKIP_CLOSE,
    PLANT_FORGE_PRE,
    PLANT_ALWAYS_ACK,
    PLANT_REPEAT_MISSING,
    PLANT_REPLAY_FRESH,
    PLANT_REPLAY_UNDER_SNAP,
    PLANT_FORGE_REPLAY_DEPTH,
    PLANT_DRIVE_HELD_FRAME,
    PLANT_UNBUFFERED,
    PLANT_NO_HISTORY,
    PLANT_KEEP_HISTORY,
    PLANT_RETAINED_REWIND,
    PLANT_RETAINED_SAMPLE,
    PLANT_PEER_READS_AUTHORITY,
    PLANT_LOCKSTEP_AUTHORITY,
    PLANT_ENROLLED_MEMBERSHIP,
    PLANT_TRANSPARENT_NESTING,
    PLANT_SHARED_ADMISSION,
};

class ScenarioRun {
    static constexpr int DOMAIN_IN = 0;
    static constexpr int VERDICT_UNJUDGED = 0;
    static constexpr int DRIVE_FRESH = 1;

    static constexpr int POLICY_RECOVER = 0;
    static constexpr int CORRECTION_SNAP = 0;
    static constexpr int RESTORE_EXACT = 0;

    static constexpr double DELTA = 1.0 / 60.0;
    static constexpr double SPEED = 60.0;

    static constexpr double CONVERGE_RATE = 0.5;

    static constexpr double TELEPORT_THRESHOLD = 1.0e6;

    static constexpr int PHANTOM_TICK = 12;

    struct Track {
        godot::StringName name;
        godot::StringName field;
        int client = 0;
        bool hosted = false;

        godot::Vector2 predicted;
        godot::Vector2 authority;
        godot::Vector2 input;

        int64_t latest_input_tick = -1;
        int64_t last_driven_input_tick = -1;

        int streak = 0;
        int last_sign = 0;
        double last_divergence = -1.0;

        netw::predict::Slot engine;

        Lane lane;
    };

    Scenario ran;
    godot::Vector<Track> tracks;
    godot::Vector<Membership> scene_rows;
    Occupancy held;
    bool reached_regime = false;
    int judged = 0;
    int64_t witnessed = 0;
    bool has_witness = false;

public:
    static ScenarioRun kernel(const Scenario &p_scenario) {
        return kernel(p_scenario, PLANT_NONE);
    }

    static ScenarioRun kernel(const Scenario &p_scenario, Plant p_plant);

    static ScenarioRun lanes(const Scenario &p_scenario) {
        return lanes(p_scenario, PLANT_NONE);
    }

    static ScenarioRun lanes(const Scenario &p_scenario, Plant p_plant);

    static ScenarioRun consume(const Scenario &p_scenario) {
        return consume(p_scenario, PLANT_NONE);
    }

    static ScenarioRun consume(const Scenario &p_scenario, Plant p_plant);

#if defined(NETW_TIER_HOSTED)
    static ScenarioRun session(LoopbackRig &p_rig, const Scenario &p_scenario) {
        return session(p_rig, p_scenario, PLANT_NONE);
    }

    static ScenarioRun session(
        LoopbackRig &p_rig,
        const Scenario &p_scenario,
        Plant p_plant
    );

    static ScenarioRun record(LoopbackRig &p_rig, const Scenario &p_scenario) {
        return record(p_rig, p_scenario, PLANT_NONE);
    }

    static ScenarioRun record(
        LoopbackRig &p_rig,
        const Scenario &p_scenario,
        Plant p_plant
    );

    static ScenarioRun scenes(LoopbackRig &p_rig, const Scenario &p_scenario) {
        return scenes(p_rig, p_scenario, PLANT_NONE);
    }

    static ScenarioRun scenes(
        LoopbackRig &p_rig,
        const Scenario &p_scenario,
        Plant p_plant
    );
#endif

    const Scenario &scenario() const {
        return ran;
    }

    bool regime_reached() const {
        return reached_regime;
    }

    int decisions() const {
        return judged;
    }

    const Occupancy &occupancy() const {
        return held;
    }

    int64_t digest() const {
        int64_t hash = 1469598103934665603LL;
        for (int index = 0; index < tracks.size(); ++index) {
            hash = (hash ^ tracks[index].lane.digest()) * 1099511628211LL;
        }
        return hash;
    }

    void witness(const ScenarioRun &p_replica) {
        witnessed = p_replica.digest();
        has_witness = true;
    }

    bool witnessed_a_replica() const {
        return has_witness;
    }

    int64_t replica_digest() const {
        return witnessed;
    }

    Membership membership(const godot::StringName &p_name) const {
        for (int index = 0; index < scene_rows.size(); ++index) {
            if (scene_rows[index].scene_name == p_name) {
                return scene_rows[index];
            }
        }
        return Membership();
    }

    Lane lane(const godot::StringName &p_name) const {
        REQUIRE_MESSAGE(
            reached_regime,
            "a lane was read from a run whose regime was never reached"
        );
        for (int index = 0; index < tracks.size(); ++index) {
            if (tracks[index].name == p_name) {
                return tracks[index].lane;
            }
        }
        FAIL("the scenario declares no lane under that name");
        return Lane();
    }

private:
    static godot::Dictionary wiring_for(
        const Scenario &p_scenario,
        const godot::StringName &p_field
    );
    void apply_steps(int p_tick, Plant p_plant);
    void drive(int p_tick, Plant p_plant);
    void engine_drive(int p_tick, Plant p_plant);
    void judge(int p_tick, Plant p_plant);
    void seed_tracks(const Scenario &p_scenario);
    void settle_regime(const Scenario &p_scenario);
#if defined(NETW_TIER_HOSTED)
    static void admit_declared(
        LoopbackRig &p_rig,
        const Scenario &p_scenario,
        const Scenario::Step &p_step,
        Plant p_plant
    );
    static godot::Vector<Membership> read_scene_rows(
        LoopbackRig &p_rig,
        const Scenario &p_scenario,
        const godot::Vector<godot::StringName> &p_entity_names,
        Plant p_plant
    );
#endif
    static godot::Vector2 motion_of(const godot::Variant &p_command);

    static int32_t stamp(const godot::Vector2 &p_value);
};

inline godot::Dictionary ScenarioRun::wiring_for(
    const Scenario &p_scenario,
    const godot::StringName &p_field
) {
    godot::Dictionary converge_rules;
    converge_rules[p_field] = CONVERGE_RATE;
    godot::Dictionary wiring;
    wiring[godot::StringName("epsilon")] = p_scenario.epsilon;
    wiring[godot::StringName("converge_rules")] = converge_rules;
    wiring[godot::StringName("teleport_threshold")] = TELEPORT_THRESHOLD;
    return wiring;
}

inline void ScenarioRun::apply_steps(int p_tick, Plant p_plant) {
    Track *rows = tracks.ptrw();
    for (int index = 0; index < ran.steps.size(); ++index) {
        const Scenario::Step &step = ran.steps[index];
        if (step.tick != p_tick) {
            continue;
        }
        for (int at = 0; at < tracks.size(); ++at) {
            Track &track = rows[at];
            if (track.name != step.subject) {
                continue;
            }
            if (step.verb == godot::StringName("hold_input")) {
                track.input = godot::Vector2(step.value);
                track.latest_input_tick = p_tick;
            } else if (step.verb == godot::StringName("release_input")) {
                track.input = godot::Vector2();
                track.latest_input_tick = p_tick;
            } else if (step.verb == godot::StringName("perturb")) {
                track.authority += godot::Vector2(step.value);
            }
        }
    }
    if (p_plant != PLANT_PHANTOM_PERTURB || p_tick != PHANTOM_TICK) {
        return;
    }
    for (int at = 0; at < tracks.size(); ++at) {
        rows[at].authority += godot::Vector2(5.0, 0.0);
    }
}

inline void ScenarioRun::drive(int p_tick, Plant p_plant) {
    Track *rows = tracks.ptrw();
    for (int at = 0; at < tracks.size(); ++at) {
        Track &track = rows[at];
        const godot::Ref<netw::NetwPredictFold> fold
            = netw::prediction_core::predict_fold(
                track.latest_input_tick,
                track.last_driven_input_tick,
                p_tick
            );
        const bool fresh
            = p_plant == PLANT_DRIVE_EVERY_TICK || fold->kind() == DRIVE_FRESH;
        if (fresh) {
            track.last_driven_input_tick = track.latest_input_tick;
            track.lane.lane_consumed += 1;
        } else {
            track.lane.lane_missing += 1;
        }
        const godot::Vector2 advance
            = track.input * godot::real_t(SPEED * DELTA);
        track.predicted += advance;
        track.authority += advance;
    }
}

inline void ScenarioRun::judge(int p_tick, Plant p_plant) {
    Track *rows = tracks.ptrw();
    for (int at = 0; at < tracks.size(); ++at) {
        Track &track = rows[at];
        godot::Dictionary predicted;
        predicted[track.field] = track.predicted;
        godot::Dictionary payload;
        payload[track.field] = track.authority;
        const godot::Dictionary wiring = wiring_for(ran, track.field);

        const int domain
            = netw::prediction_core::domain_of(true, false, p_tick, -1);
        godot::Dictionary pose_errors;
        const godot::Ref<netw::NetwPredictJudgement> judgement
            = netw::prediction_core::evaluate(
                domain,
                VERDICT_UNJUDGED,
                predicted,
                payload,
                wiring,
                pose_errors
            );
        judged += 1;
        const double divergence = judgement->divergence();
        track.lane.lane_divergence.push_back(divergence);
        if (!judgement->corrected()) {
            continue;
        }

        godot::Dictionary verdict;
        verdict[godot::StringName("domain")] = domain;
        const godot::Ref<netw::NetwPredictRecovery> plan
            = netw::prediction_core::recover(
                payload,
                POLICY_RECOVER,
                CORRECTION_SNAP,
                RESTORE_EXACT,
                godot::Dictionary(),
                predicted,
                pose_errors,
                wiring,
                verdict,
                DELTA
            );

        const double gap = track.authority.x - track.predicted.x;
        const int sign = gap > 0.0 ? 1 : (gap < 0.0 ? -1 : 0);
        const godot::Dictionary escalation
            = netw::prediction_core::escalation_after(
                track.streak,
                track.last_sign,
                track.last_divergence,
                divergence,
                sign
            );
        const bool escalated = bool(escalation[godot::StringName("escalate")]);
        track.lane.lane_escalations += escalated ? 1 : 0;
        track.streak = p_plant == PLANT_CARRY_STREAK
            ? track.streak + 1
            : int(escalation[godot::StringName("streak")]);
        track.last_sign = int(escalation[godot::StringName("sign")]);
        track.last_divergence
            = p_plant == PLANT_CARRY_STREAK ? 0.0 : divergence;

        if (plan->skip() || p_plant == PLANT_NO_RECOVER) {
            continue;
        }
        const godot::Dictionary restore = plan->restore();
        track.predicted = godot::Vector2(restore[track.field]);
        track.lane.lane_corrections += 1;
        track.lane.lane_max_replay_depth = 1;
    }
}

inline int32_t ScenarioRun::stamp(const godot::Vector2 &p_value) {
    const float pair[2] = {float(p_value.x), float(p_value.y)};
    return netw::predict::fnv1a(
        reinterpret_cast<const uint8_t *>(pair),
        int(sizeof(pair))
    );
}

inline godot::Vector2 ScenarioRun::motion_of(const godot::Variant &p_command) {
    if (p_command.get_type() == godot::Variant::VECTOR2) {
        return godot::Vector2(p_command);
    }
    if (p_command.get_type() != godot::Variant::DICTIONARY) {
        return godot::Vector2();
    }
    const godot::Dictionary command = p_command;
    const godot::Variant motion
        = command.get(godot::StringName("motion"), godot::Vector2());
    return motion.get_type() == godot::Variant::VECTOR2 ? godot::Vector2(motion)
                                                        : godot::Vector2();
}

inline void ScenarioRun::seed_tracks(const Scenario &p_scenario) {
    for (int index = 0; index < p_scenario.world.entity_count(); ++index) {
        const EntityDecl &decl = p_scenario.world.entity_at(index);
        REQUIRE_MESSAGE(
            decl.synced_columns().size() == 1,
            "this driver predicts exactly one declared field per entity"
        );
        Track track;
        track.name = decl.name();
        track.field = decl.synced_columns()[0];
        const int player_client = p_scenario.world.player_client_at(index);
        track.hosted = p_scenario.world.is_hosted_at(index);
        track.client = p_scenario.world.is_hosted_at(index)
            ? -1
            : (player_client >= 0 ? player_client : 0);
        track.predicted = godot::Vector2(decl.initial_pose());
        track.authority = track.predicted;
        track.lane.name = track.name;
        track.lane.lane_epsilon = p_scenario.epsilon;
        track.lane.lane_buffer_declared = decl.replay_buffer_depth();
        tracks.push_back(track);
    }
    REQUIRE_MESSAGE(
        !tracks.is_empty(),
        "the scenario declares no entity to drive"
    );
}

inline void ScenarioRun::settle_regime(const Scenario &p_scenario) {
    bool disturbed = !p_scenario.declares(godot::StringName("perturb"));
    for (int at = 0; at < tracks.size() && !disturbed; ++at) {
        disturbed = tracks[at].lane.tail_divergence(p_scenario.run_ticks) > 0.0;
    }
    const bool deafened = p_scenario.inbound_conditions.is_valid();
    bool evidenced = judged > 0;
    if (!evidenced) {
        evidenced = !tracks.is_empty();
        for (int at = 0; at < tracks.size(); ++at) {
            evidenced = evidenced && (tracks[at].hosted || deafened)
                && tracks[at].lane.drives() > 0;
        }
    }
    reached_regime = p_scenario.run_ticks > p_scenario.warmup_ticks && evidenced
        && disturbed;

    Track *rows = tracks.ptrw();
    for (int at = 0; at < tracks.size(); ++at) {
        rows[at].lane.lane_position = rows[at].predicted;
    }
}

inline ScenarioRun ScenarioRun::kernel(
    const Scenario &p_scenario,
    Plant p_plant
) {
    ScenarioRun run;
    run.ran = p_scenario;
    run.seed_tracks(p_scenario);

    for (int tick = 1; tick <= p_scenario.run_ticks; ++tick) {
        run.apply_steps(tick, p_plant);
        run.drive(tick, p_plant);
        if (tick > p_scenario.warmup_ticks) {
            run.judge(tick, p_plant);
        }
    }
    run.settle_regime(p_scenario);
    return run;
}

inline void ScenarioRun::engine_drive(int p_tick, Plant p_plant) {
    Track *rows = tracks.ptrw();
    for (int at = 0; at < tracks.size(); ++at) {
        Track &track = rows[at];
        netw::predict::Slot &engine = track.engine;

        engine.record_input(track.latest_input_tick, stamp(track.input));

        netw::predict::Timing timing;
        timing.tick = p_tick;
        timing.frame = p_tick;
        timing.delta = DELTA;
        timing.ticktime = DELTA;

        netw::predict::StateStamp pre;
        pre.fp = p_plant == PLANT_FORGE_PRE
            ? stamp(track.predicted + godot::Vector2(1.0, 0.0))
            : stamp(track.predicted);

        const netw::predict::DriveRecord drive
            = engine.open_drive(timing, godot::Dictionary(), pre);
        if (!drive.ran) {
            track.lane.lane_held += drive.held ? 1 : 0;
            track.lane.lane_missing += 1;
            continue;
        }
        if (drive.fresh) {
            track.lane.lane_consumed += 1;
        } else {
            track.lane.lane_missing += 1;
        }

        const godot::Vector2 advance
            = track.input * godot::real_t(SPEED * DELTA);
        track.predicted += advance;
        track.authority += advance;

        if (p_plant != PLANT_SKIP_CLOSE) {
            netw::predict::StateStamp post;
            post.fp = stamp(track.predicted);
            engine.close_drive(drive.transition, post);
        }
        if (p_plant == PLANT_ALWAYS_ACK) {
            engine.acknowledge(drive.transition, true);
        }
    }
}

inline ScenarioRun ScenarioRun::lanes(
    const Scenario &p_scenario,
    Plant p_plant
) {
    ScenarioRun run;
    run.ran = p_scenario;
    run.seed_tracks(p_scenario);

    Track *rows = run.tracks.ptrw();
    for (int at = 0; at < run.tracks.size(); ++at) {
        rows[at].engine.open = true;
        rows[at].engine.config.schedule = int(netw::Schedule::TICK);
        rows[at].engine.config.role = int(netw::Role::PREDICT);
        rows[at].engine.config.correction = int(netw::CorrectionMode::SNAP);
        rows[at].engine.config.restore = int(netw::RestoreMode::EXACT);
    }

    for (int tick = 1; tick <= p_scenario.run_ticks; ++tick) {
        run.apply_steps(tick, p_plant);
        run.engine_drive(tick, p_plant);
        if (tick > p_scenario.warmup_ticks) {
            run.judge(tick, p_plant);
        }
    }

    rows = run.tracks.ptrw();
    for (int at = 0; at < run.tracks.size(); ++at) {
        const netw::predict::Journal &journal = rows[at].engine.journal;
        rows[at].lane.lane_journal_rows = journal.size();
        rows[at].lane.lane_chain_breaks = 0;
        for (int index = 0; index < journal.size(); ++index) {
            const bool closed
                = (journal.flags_at(index) & netw::predict::ROW_CLOSED) != 0;
            rows[at].lane.lane_closed_rows += closed ? 1 : 0;
            const bool broken
                = (journal.flags_at(index) & netw::predict::ROW_CHAIN_BROKEN)
                != 0;
            rows[at].lane.lane_chain_breaks += broken ? 1 : 0;
        }
    }
    run.settle_regime(p_scenario);
    return run;
}

inline ScenarioRun ScenarioRun::consume(
    const Scenario &p_scenario,
    Plant p_plant
) {
    ScenarioRun run;
    run.ran = p_scenario;
    run.seed_tracks(p_scenario);
    Track *rows = run.tracks.ptrw();
    for (int tick = 1; tick <= p_scenario.run_ticks; ++tick) {
        for (int at = 0; at < run.tracks.size(); ++at) {
            Track &track = rows[at];
            bool has_input = false;
            bool has_later_input = false;
            godot::Vector2 input;
            for (const Scenario::Step &step : p_scenario.steps) {
                if (step.verb != godot::StringName("input_at")
                    || step.subject != track.name) {
                    continue;
                }
                if (step.tick == tick) {
                    has_input = true;
                    input = motion_of(step.value);
                } else if (step.tick > tick) {
                    has_later_input = true;
                }
            }
            netw::predict::ConsumeInputPlan plan
                = netw::predict::plan_consume_input(
                    netw::Schedule::TICK,
                    has_input,
                    has_later_input,
                    track.last_driven_input_tick >= 0,
                    p_scenario.world.entity_at(at).missing_input_policy()
                );
            if (!plan.eligible) {
                continue;
            }
            if (p_plant == PLANT_REPEAT_MISSING && plan.missing) {
                plan.run = track.last_driven_input_tick >= 0;
                plan.use_last = plan.run;
            }
            if (plan.use_last) {
                input = track.input;
            }
            if (plan.run) {
                track.predicted += input * godot::real_t(SPEED * DELTA);
            }
            if (plan.missing) {
                track.lane.lane_missing += 1;
            } else {
                track.input = input;
                track.last_driven_input_tick = tick;
                track.lane.lane_consumed += 1;
            }
            run.judged += 1;
        }
    }
    bool saw_missing = false;
    for (int at = 0; at < run.tracks.size(); ++at) {
        rows[at].lane.lane_position = rows[at].predicted;
        saw_missing = saw_missing || rows[at].lane.lane_missing > 0;
    }
    run.reached_regime = run.judged > 0 && saw_missing;
    return run;
}

#if defined(NETW_TIER_HOSTED)

inline ScenarioRun ScenarioRun::session(
    LoopbackRig &p_rig,
    const Scenario &p_scenario,
    Plant p_plant
) {
    ScenarioRun run;
    run.ran = p_scenario;
    run.seed_tracks(p_scenario);
    p_rig.declare_world(p_scenario.world);

    if (p_scenario.link_conditions.is_valid()) {
        for (int client = 0; client < p_rig.count(); ++client) {
            p_rig.conditions(client, p_scenario.link_conditions);
        }
        p_rig.conditions(-1, p_scenario.link_conditions);
    }
    if (p_scenario.inbound_conditions.is_valid()
        && p_plant != PLANT_ALWAYS_ACK) {
        for (int client = 0; client < p_rig.count(); ++client) {
            p_rig.conditions(client, p_scenario.inbound_conditions);
        }
    }

    std::vector<std::unique_ptr<Recorder>> recorders;
    for (int index = 0; index < run.tracks.size(); ++index) {
        const godot::StringName &name = run.tracks[index].name;
        const int client = run.tracks[index].client;
        godot::Object *handle = p_rig.prediction_handle(name, client);
        netw::NetwMultiplayer *owner_api
            = client < 0 ? p_rig.server() : p_rig.client(client);
        if (p_plant == PLANT_NO_RECOVER) {
            owner_api->predict_set_param(
                p_rig.entity_of(name, client),
                netw::NetwMultiplayer::PREDICT_PARAM_RECOVERY_POLICY,
                3
            );
        }
        if (p_plant == PLANT_UNBUFFERED) {
            p_rig.server()->predict_set_param(
                p_rig.entity_of(name),
                netw::NetwMultiplayer::PREDICT_PARAM_REPLAY_BUFFER_DEPTH,
                0
            );
        }
        if (p_plant == PLANT_REPLAY_UNDER_SNAP) {
            owner_api->predict_set_param(
                p_rig.entity_of(name, client),
                netw::NetwMultiplayer::PREDICT_PARAM_CORRECTION_MODE,
                int(netw::CorrectionMode::REPLAY)
            );
        }
        if (p_plant == PLANT_REPLAY_FRESH) {
            Carrier *owner = godot::Object::cast_to<Carrier>(
                p_rig.node_of(p_rig.entity_of(name, client), client)
            );
            REQUIRE_MESSAGE(owner != nullptr, "prediction needs a carrier");
            if (owner != nullptr) {
                owner->replay_effects(true);
            }
        }
        godot::Vector<godot::StringName> signals;
        signals.push_back("state_evaluated");
        recorders.push_back(std::make_unique<Recorder>(handle, signals));
    }

    for (int tick = 1; tick <= p_scenario.run_ticks; ++tick) {
        Track *tick_tracks = run.tracks.ptrw();
        for (int at = 0; at < run.tracks.size(); ++at) {
            Carrier *owner = godot::Object::cast_to<Carrier>(p_rig.node_of(
                p_rig.entity_of(tick_tracks[at].name, tick_tracks[at].client),
                tick_tracks[at].client
            ));
            REQUIRE_MESSAGE(owner != nullptr, "prediction needs a carrier");
            if (owner != nullptr) {
                owner->set("motion", tick_tracks[at].input);
                owner->set("bombing", false);
            }
        }
        for (const Scenario::Step &step : p_scenario.steps) {
            if (step.tick != tick) {
                continue;
            }
            int track_index = -1;
            for (int at = 0; at < run.tracks.size(); ++at) {
                if (run.tracks[at].name == step.subject) {
                    track_index = at;
                    break;
                }
            }
            REQUIRE_MESSAGE(track_index >= 0, "a step needs a declared lane");
            if (track_index < 0) {
                continue;
            }
            Track &track = tick_tracks[track_index];
            const int client = track.client;
            godot::Node *owner
                = p_rig.node_of(p_rig.entity_of(step.subject, client), client);
            if (step.verb == godot::StringName("hold_input")) {
                track.input = motion_of(step.value);
                owner->set("motion", track.input);
            } else if (step.verb == godot::StringName("release_input")) {
                track.input = godot::Vector2();
                owner->set("motion", track.input);
            } else if (step.verb == godot::StringName("input_at")) {
                owner->set("motion", motion_of(step.value));
                if (step.value.get_type() == godot::Variant::DICTIONARY) {
                    const godot::Dictionary command = step.value;
                    owner->set(
                        "bombing",
                        command.get(godot::StringName("bombing"), false)
                    );
                }
            } else if (step.verb == godot::StringName("perturb")) {
                godot::Node2D *owner = godot::Object::cast_to<godot::Node2D>(
                    p_rig.node_of(p_rig.entity_of(step.subject))
                );
                REQUIRE_MESSAGE(
                    owner != nullptr,
                    "a perturbation needs a body"
                );
                owner->set_position(
                    owner->get_position() + godot::Vector2(step.value)
                );
            }
        }
        if (p_plant == PLANT_PHANTOM_PERTURB && tick == PHANTOM_TICK) {
            for (Track &track : run.tracks) {
                godot::Node2D *owner = godot::Object::cast_to<godot::Node2D>(
                    p_rig.node_of(p_rig.entity_of(track.name))
                );
                REQUIRE_MESSAGE(
                    owner != nullptr,
                    "a perturbation needs a body"
                );
                if (owner != nullptr) {
                    owner->set_position(
                        owner->get_position() + godot::Vector2(5.0, 0.0)
                    );
                }
            }
        }
        if (p_scenario.frame_ticks.is_empty()) {
            p_rig.step_ticks(1);
        } else {
            REQUIRE_MESSAGE(
                tick <= p_scenario.frame_ticks.size(),
                "a frame run needs one declared tick count per frame"
            );
            int frame_tick_count = tick <= p_scenario.frame_ticks.size()
                ? p_scenario.frame_ticks[tick - 1]
                : 0;
            if (p_plant == PLANT_DRIVE_HELD_FRAME && frame_tick_count == 0) {
                frame_tick_count = 1;
            }
            int authority_tick_count = frame_tick_count;
            if (!p_scenario.authority_frame_ticks.is_empty()
                && p_plant != PLANT_LOCKSTEP_AUTHORITY) {
                REQUIRE_MESSAGE(
                    tick <= p_scenario.authority_frame_ticks.size(),
                    "an authority schedule needs one count per frame"
                );
                authority_tick_count
                    = tick <= p_scenario.authority_frame_ticks.size()
                    ? p_scenario.authority_frame_ticks[tick - 1]
                    : 0;
            }
            if (p_scenario.authority_frame_ticks.is_empty()
                || p_plant == PLANT_LOCKSTEP_AUTHORITY) {
                p_rig.step_frame(frame_tick_count);
            } else {
                p_rig.step_split_frame(frame_tick_count, authority_tick_count);
            }
        }
        for (int at = 0; at < run.tracks.size(); ++at) {
            Track &track = tick_tracks[at];
            Carrier *client_owner
                = godot::Object::cast_to<Carrier>(p_rig.node_of(
                    p_rig.entity_of(track.name, track.client),
                    track.client
                ));
            Carrier *server_owner = godot::Object::cast_to<Carrier>(
                p_rig.node_of(p_rig.entity_of(track.name))
            );
            REQUIRE(client_owner != nullptr);
            REQUIRE(server_owner != nullptr);
            if (client_owner != nullptr && server_owner != nullptr) {
                track.lane.lane_position_x = client_owner->simulated_x();
                track.lane.lane_authority_position_x
                    = server_owner->simulated_x();
                track.lane.lane_fresh_effects = client_owner->fresh_effects();
                track.lane.lane_authority_fresh_effects
                    = server_owner->fresh_effects();
            }
        }
    }

    ScenarioRun::Track *tracks = run.tracks.ptrw();
    for (int index = 0; index < run.tracks.size(); ++index) {
        Track &track = tracks[index];
        netw::NetwPredictionHandle *client_handle
            = p_rig.prediction_handle(track.name, track.client);
        netw::NetwPredictionHandle *server_handle
            = p_rig.prediction_handle(track.name);
        const godot::Ref<netw::NetwPredictStats> client_stats
            = client_handle->get_stats();
        const godot::Ref<netw::NetwPredictStats> server_stats
            = server_handle->get_stats();
        REQUIRE(client_stats.is_valid());
        REQUIRE(server_stats.is_valid());
        track.lane.lane_corrections = int(client_stats->get("corrections"));
        track.lane.lane_drives = int(client_stats->get("drive_seq"));
        track.lane.lane_frames = p_scenario.run_ticks;
        track.lane.lane_authoring_clamped
            = int(client_stats->get("authoring_clamped"));
        track.lane.lane_quantum_declared
            = int(client_stats->get("quantum_declared"));
        track.lane.lane_quantum_steps = int(client_stats->get("quantum_steps"));
        track.lane.lane_quantum_faults
            = int(client_stats->get("quantum_faults"));
        track.lane.lane_max_replay_depth
            = int(client_stats->get("max_replay_depth"));
        if (p_plant == PLANT_FORGE_REPLAY_DEPTH) {
            track.lane.lane_max_replay_depth = p_scenario.run_ticks;
        }
        track.lane.lane_fp_verified = int(client_stats->get("fp_verified"));
        track.lane.lane_fp_mismatches = int(client_stats->get("fp_mismatches"));
        track.lane.lane_first_divergence
            = int(client_stats->get("first_divergent_transition"));
        const godot::Ref<netw::NetwPredictJournal> owner_journal
            = client_handle->journal();
        track.lane.lane_journal_rows
            = owner_journal.is_valid() ? owner_journal->size() : 0;
        track.lane.lane_consumed = int(server_stats->get("consumed"));
        track.lane.lane_missing = int(server_stats->get("missing"));
        track.lane.lane_held = int(server_stats->get("held"));
        track.lane.lane_starved = int(server_stats->get("starved"));
        track.lane.lane_speculation_held
            = int(client_stats->get("speculation_held"));
        track.lane.lane_resyncs = int(server_stats->get("resync"));
        track.lane.lane_skipped = int(server_stats->get("skipped"));
        track.lane.lane_queue_depth
            = int(server_stats->get("tape_queue_depth"));
        const godot::Ref<netw::NetwPredictJournal> authority_journal
            = server_handle->journal();
        track.lane.lane_authority_journal_rows
            = authority_journal.is_valid() ? authority_journal->size() : 0;
        track.lane.lane_joint_passes = int(client_stats->get("joint_passes"));
        track.lane.lane_joint_members = int(client_stats->get("joint_members"));
        track.lane.lane_epsilon = p_scenario.epsilon;

        Recorder &recorder = *recorders[size_t(index)];
        const int decisions = recorder.count("state_evaluated");
        run.judged += decisions;
        for (int emission = 0; emission < decisions; ++emission) {
            const godot::Array args
                = recorder.args("state_evaluated", emission);
            REQUIRE_MESSAGE(args.size() == 4, "a verdict has four fields");
            if (args.size() == 4) {
                track.lane.lane_divergence.push_back(double(args[2]));
            }
        }
    }
    run.settle_regime(p_scenario);
    return run;
}

inline ScenarioRun ScenarioRun::record(
    LoopbackRig &p_rig,
    const Scenario &p_scenario,
    Plant p_plant
) {
    ScenarioRun run;
    run.ran = p_scenario;
    run.seed_tracks(p_scenario);
    p_rig.declare_world(p_scenario.world);

    Track *rows = run.tracks.ptrw();
    for (int at = 0; at < run.tracks.size(); ++at) {
        if (p_plant == PLANT_NO_HISTORY) {
            p_rig.server()->lagcomp_timeline_undeclare(
                p_rig.entity_of(rows[at].name)
            );
        }
    }

    godot::HashMap<godot::StringName, int> sample_ticks;
    godot::HashMap<godot::StringName, int> rewind_ticks;
    for (const Scenario::Step &step : p_scenario.steps) {
        if (step.verb == godot::StringName("sample_at")) {
            sample_ticks[step.subject] = step.tick;
        } else if (step.verb == godot::StringName("rewind_at")) {
            rewind_ticks[step.subject] = step.tick;
        }
    }

    for (int tick = 1; tick <= p_scenario.run_ticks; ++tick) {
        for (const Scenario::Step &step : p_scenario.steps) {
            if (step.tick != tick) {
                continue;
            }
            for (int at = 0; at < run.tracks.size(); ++at) {
                Track &track = rows[at];
                if (track.name != step.subject) {
                    continue;
                }
                if (step.verb == godot::StringName("hold_input")) {
                    track.input = motion_of(step.value);
                } else if (step.verb == godot::StringName("release_input")) {
                    track.input = godot::Vector2();
                } else if (
                    step.verb == godot::StringName("undeclare")
                    && p_plant != PLANT_KEEP_HISTORY
                ) {
                    p_rig.server()->lagcomp_timeline_undeclare(
                        p_rig.entity_of(track.name)
                    );
                }
            }
        }
        for (int at = 0; at < run.tracks.size(); ++at) {
            Track &track = rows[at];
            godot::Node2D *body = godot::Object::cast_to<godot::Node2D>(
                p_rig.node_of(p_rig.entity_of(track.name))
            );
            REQUIRE_MESSAGE(body != nullptr, "a recorded lane needs a body");
            if (body != nullptr) {
                body->set_position(body->get_position() + track.input);
            }
        }
        p_rig.step_ticks(1);
        run.judged += 1;
    }

    bool moved = !run.tracks.is_empty();
    for (int at = 0; at < run.tracks.size(); ++at) {
        Track &track = rows[at];
        godot::Node2D *body = godot::Object::cast_to<godot::Node2D>(
            p_rig.node_of(p_rig.entity_of(track.name))
        );
        if (body != nullptr) {
            track.lane.lane_authority_position_x = body->get_position().x;
            track.lane.lane_position_x = track.lane.lane_authority_position_x;
        }
        moved = moved && track.lane.lane_authority_position_x > 0.0;

        const godot::HashMap<godot::StringName, int>::ConstIterator asked
            = sample_ticks.find(track.name);
        if (asked == sample_ticks.end()) {
            continue;
        }
        int sample_tick = asked->value;
        if (p_plant == PLANT_RETAINED_SAMPLE && sample_tick < 0) {
            sample_tick = 0;
        }
        const godot::Ref<netw::DictionaryRecord> past
            = p_rig.server()->lagcomp_sample(
                p_rig.entity_of(track.name),
                sample_tick
            );
        if (past.is_valid() && past->has_value(track.field)) {
            track.lane.lane_sample_found = true;
            track.lane.lane_sample_x = godot::Vector2(past->get(track.field)).x;
        }
        if (p_rig.client_count() > 0) {
            const godot::RID peer_entity
                = p_rig.client_entity_of(0, track.name);
            track.lane.lane_peer_asked = peer_entity.is_valid();
            if (peer_entity.is_valid()) {
                netw::NetwMultiplayer *asked_peer
                    = p_plant == PLANT_PEER_READS_AUTHORITY ? p_rig.server()
                                                            : p_rig.client(0);
                const godot::RID asked_entity
                    = p_plant == PLANT_PEER_READS_AUTHORITY
                    ? p_rig.entity_of(track.name)
                    : peer_entity;
                const godot::Ref<netw::DictionaryRecord> peer_past
                    = asked_peer->lagcomp_sample(asked_entity, sample_tick);
                track.lane.lane_peer_sample_found
                    = peer_past.is_valid() && peer_past->has_value(track.field);
            }
        }

        const godot::HashMap<godot::StringName, int>::ConstIterator rewound
            = rewind_ticks.find(track.name);
        Carrier *carrier = godot::Object::cast_to<Carrier>(body);
        if (rewound == rewind_ticks.end() || carrier == nullptr) {
            continue;
        }
        int rewind_tick = rewound->value;
        if (p_plant == PLANT_RETAINED_REWIND && asked != sample_ticks.end()) {
            rewind_tick = asked->value;
        }
        godot::TypedArray<godot::RID> targets;
        targets.push_back(p_rig.entity_of(track.name));
        p_rig.server()->lagcomp_rewind(
            targets,
            rewind_tick,
            godot::Callable(carrier, godot::StringName("observe_self"))
        );
        track.lane.lane_rewind_visits = carrier->rewind_visit_count();
        track.lane.lane_rewound_x = carrier->observed_x();
        track.lane.lane_restored_x = carrier->get_position().x;
    }
    const godot::Dictionary metrics = p_rig.server()->lagcomp_metrics();
    const godot::Array reported = metrics.keys();
    for (int index = 0; index < reported.size(); ++index) {
        run.held.occupancy_keys.push_back(godot::StringName(reported[index]));
    }
    run.held.occupancy_timelines
        = int(metrics.get(godot::StringName("timelines"), 0));

    run.reached_regime = run.judged > 0 && moved;
    return run;
}

inline ScenarioRun ScenarioRun::scenes(
    LoopbackRig &p_rig,
    const Scenario &p_scenario,
    Plant p_plant
) {
    ScenarioRun run;
    run.ran = p_scenario;
    p_rig.declare_world(p_scenario.world);
    p_rig.flush_interest();
    p_rig.pump(2);

    if (p_scenario.declares("move")) {
        for (int at = 0; at < p_scenario.world.scene_count(); ++at) {
            p_rig.enter_scene(p_scenario.world.scene_at(at).name);
        }
    }

    godot::Vector<godot::StringName> entity_names;
    for (int index = 0; index < p_scenario.world.entity_count(); ++index) {
        const godot::StringName name = p_scenario.world.entity_at(index).name();
        if (!name.is_empty()) {
            entity_names.push_back(name);
        }
    }

    const godot::Vector<Membership> declared_rows
        = read_scene_rows(p_rig, p_scenario, entity_names, p_plant);

    for (int tick = 1; tick <= p_scenario.run_ticks; ++tick) {
        for (const Scenario::Step &step : p_scenario.steps) {
            if (step.tick != tick) {
                continue;
            }
            if (step.verb == godot::StringName("seat")) {
                p_rig.seat(
                    p_rig.entity_of(step.subject),
                    p_rig.entity_of(godot::StringName(step.value))
                );
                run.judged += 1;
            } else if (step.verb == godot::StringName("move")) {
                p_rig.move_scene(
                    p_rig.entity_of(step.subject),
                    p_rig.entity_of(godot::StringName(step.value))
                );
                run.judged += 1;
            } else if (step.verb == godot::StringName("admit")) {
                admit_declared(p_rig, p_scenario, step, p_plant);
                run.judged += 1;
            } else if (step.verb == godot::StringName("release")) {
                p_rig.server()->scene_release(
                    p_rig.entity_of(step.subject),
                    p_rig.peer_id(int(step.value))
                );
                run.judged += 1;
            }
        }
        p_rig.flush_interest();
        p_rig.pump();
    }

    run.scene_rows = p_plant == PLANT_ENROLLED_MEMBERSHIP
        ? declared_rows
        : read_scene_rows(p_rig, p_scenario, entity_names, p_plant);

    bool answered = !run.scene_rows.is_empty();
    for (int index = 0; index < run.scene_rows.size(); ++index) {
        answered = answered && run.scene_rows[index].taken();
    }
    run.reached_regime = answered;
    return run;
}

inline void ScenarioRun::admit_declared(
    LoopbackRig &p_rig,
    const Scenario &p_scenario,
    const Scenario::Step &p_step,
    Plant p_plant
) {
    const int peer = p_rig.peer_id(int(p_step.value));
    p_rig.server()->scene_admit(p_rig.entity_of(p_step.subject), peer);
    if (p_plant != PLANT_SHARED_ADMISSION) {
        return;
    }
    godot::StringName stem;
    for (int index = 0; index < p_scenario.world.scene_count(); ++index) {
        const WorldDecl::SceneRow &row = p_scenario.world.scene_at(index);
        if (row.name == p_step.subject) {
            stem = row.stem;
        }
    }
    for (int index = 0; index < p_scenario.world.scene_count(); ++index) {
        const WorldDecl::SceneRow &row = p_scenario.world.scene_at(index);
        if (row.name != p_step.subject && row.stem == stem) {
            p_rig.server()->scene_admit(p_rig.entity_of(row.name), peer);
        }
    }
}

inline godot::Vector<Membership> ScenarioRun::read_scene_rows(
    LoopbackRig &p_rig,
    const Scenario &p_scenario,
    const godot::Vector<godot::StringName> &p_entity_names,
    Plant p_plant
) {
    godot::Vector<Membership> rows;
    for (int at = 0; at < p_scenario.world.scene_count(); ++at) {
        const WorldDecl::SceneRow &declaration = p_scenario.world.scene_at(at);
        const godot::RID scene = p_rig.entity_of(declaration.name);
        Membership row;
        row.scene_name = declaration.name;
        row.asked = true;

        godot::TypedArray<godot::RID> members
            = p_rig.server()->scene_get_entities(scene);
        if (p_plant == PLANT_TRANSPARENT_NESTING) {
            for (int index = 0; index < members.size(); ++index) {
                const godot::TypedArray<godot::RID> inner
                    = p_rig.server()->scene_get_entities(
                        godot::RID(members[index])
                    );
                members.append_array(inner);
            }
        }
        row.member_count = members.size();
        for (const godot::StringName &name : p_entity_names) {
            if (members.has(p_rig.entity_of(name))) {
                row.enclosed.push_back(name);
            }
        }
        for (int other = 0; other < p_scenario.world.scene_count(); ++other) {
            const WorldDecl::SceneRow &sibling
                = p_scenario.world.scene_at(other);
            if (sibling.name != declaration.name
                && members.has(p_rig.entity_of(sibling.name))) {
                row.enclosed.push_back(sibling.name);
            }
        }

        row.boundary = p_rig.server()->scene_get_layer(scene).is_valid();
        for (int client = 0; client < p_rig.count(); ++client) {
            if (p_rig.server()->scene_admits(scene, p_rig.peer_id(client))) {
                row.admitted.push_back(client);
            }
        }
        rows.push_back(row);
    }
    return rows;
}

#endif

} // namespace netw_test

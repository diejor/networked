#include "support/netw_test.h"

#include "support/installable_stepper.h"

#include "support/netw_cells.h"
#include "support/scenario_run.h"

#include "godot/physics_server.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/timeline.hpp"
#include "netw/lagcomp_core.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwPredictCensusLaws {

using namespace netw_test;
using godot::Dictionary;
using godot::Ref;
using godot::StringName;
using godot::TypedArray;
using godot::Vector2;
using netw::NetwEntity;
using netw::NetwLagCompCore;
using netw::NetwMultiplayer;
using netw::NetwPredictionHandle;
using netw::NetwPredictStats;

EntityDecl census_player(const char *p_name) {
    return EntityDecl()
        .named(p_name)
        .on_schema("PredictedPose")
        .synced("position")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(netw::Schedule::TICK);
}

Scenario two_predicted_lanes() {
    Scenario scenario;
    scenario.label = "predict-census";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3)
        .lag_compensated()
        .player(census_player("P"), 0)
        .player(census_player("Q"), 1);
    scenario.clients = 2;
    scenario.conditions(netw::LocalLinkConditions::polls(4));
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    scenario.hold_input(1, "Q", Vector2(1.0, 0.0));
    return scenario.until(80);
}

Dictionary counters_of(const godot::Variant &p_stepped) {
    const NetwEntity *wrapper = godot::Object::cast_to<NetwEntity>(p_stepped);
    if (wrapper == nullptr) {
        return Dictionary();
    }
    const Ref<NetwPredictionHandle> handle = wrapper->get_prediction();
    if (handle.is_null() || handle->get_stats().is_null()) {
        return Dictionary();
    }
    return handle->get_stats()->to_dictionary();
}

int64_t summed(const TypedArray<godot::Object> &p_stepped, const char *p_key) {
    int64_t total = 0;
    for (int at = 0; at < p_stepped.size(); ++at) {
        total += int64_t(counters_of(p_stepped[at]).get(StringName(p_key), 0));
    }
    return total;
}

Scenario hosted_tick_lane() {
    Scenario scenario;
    scenario.label = "predict-census-hosted";
    scenario.epsilon = 0.01;
    scenario.world.clocked(30, 3).lag_compensated().hosted(census_player("P"));
    scenario.hold_input(1, "P", Vector2(1.0, 0.0));
    return scenario.until(60);
}

Dictionary stats_of_slot(NetwMultiplayer *p_core, int64_t p_slot) {
    netw::NetwPredictionEngine *const pool = p_core->get_prediction_engine();
    const TypedArray<godot::Object> stepped
        = p_core->predict_stepped_entities();
    for (int at = 0; at < stepped.size(); ++at) {
        const Ref<NetwEntity> wrapper = stepped[at];
        if (wrapper.is_valid() && pool->slot_of(wrapper) == p_slot) {
            return counters_of(stepped[at]);
        }
    }
    return Dictionary();
}

int64_t largest(const TypedArray<godot::Object> &p_stepped, const char *p_key) {
    int64_t most = 0;
    for (int at = 0; at < p_stepped.size(); ++at) {
        const int64_t held
            = int64_t(counters_of(p_stepped[at]).get(StringName(p_key), 0));
        most = held > most ? held : most;
    }
    return most;
}

TEST_CASE(
    "[Networked][Predict][Census] PC1 the session's prediction census "
    "is the whole stepped roster summed, so a census over two predicted "
    "entities answers more than either entity holds alone"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);

    const Dictionary census = server->predict_metrics();
    NETW_CHECK_EQ(int64_t(census[StringName("entities")]), int64_t(2));

    const int64_t consumed = summed(stepped, "consumed");
    REQUIRE(consumed > largest(stepped, "consumed"));
    NETW_CHECK_EQ(int64_t(census[StringName("consumed")]), consumed);
    NETW_CHECK_EQ(
        int64_t(census[StringName("corrections")]),
        summed(stepped, "corrections")
    );
    NETW_CHECK_EQ(
        int64_t(census[StringName("missing")]),
        summed(stepped, "missing")
    );
    NETW_CHECK_EQ(
        int64_t(census[StringName("folded")]),
        summed(stepped, "folded")
    );
    NETW_CHECK_EQ(
        int64_t(census[StringName("max_replay_depth")]),
        largest(stepped, "max_replay_depth")
    );

    SUBCASE("and the replay groups' own cadence rides the same roster") {
        const Dictionary joint = census[StringName("joint")];
        NETW_CHECK_EQ(
            int64_t(joint[StringName("joint_passes")]),
            summed(stepped, "joint_passes")
        );
        NETW_CHECK_EQ(
            int64_t(joint[StringName("joint_members")]),
            largest(stepped, "joint_members")
        );
        NETW_CHECK_EQ(
            int64_t(joint[StringName("cells_relayed")]),
            summed(stepped, "cells_relayed")
        );
        NETW_CHECK_EQ(
            int64_t(joint[StringName("cells_substituted")]),
            summed(stepped, "cells_substituted")
        );
        NETW_CHECK_EQ(
            int64_t(joint[StringName("heal_snaps")]),
            summed(stepped, "heal_snaps")
        );
        NETW_CHECK_EQ(
            int64_t(joint[StringName("linger_held")]),
            summed(stepped, "linger_held")
        );
    }

    SUBCASE("and the timeline count is the history's own, never a recount") {
        NetwLagCompCore *history = server->get_lagcomp_core();
        REQUIRE(history != nullptr);
        REQUIRE(history->timeline_registered() > 0);
        NETW_CHECK_EQ(
            int64_t(census[StringName("timelines")]),
            history->timeline_registered()
        );
    }
}

TEST_CASE(
    "[Networked][Predict][Census] PC2 the acknowledgement frame "
    "publishes the frontier it shipped, so a consumer's journal_closed and "
    "ack_frontier are the pool's own answer for that slot rather than a "
    "count the reporter kept"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);

    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);

    int published = 0;
    for (int at = 0; at < stepped.size(); ++at) {
        const Ref<NetwEntity> wrapper = stepped[at];
        REQUIRE(wrapper.is_valid());
        const int64_t slot = pool->slot_of(wrapper);
        REQUIRE(slot >= 0);

        const godot::PackedInt64Array frontier
            = pool->ack_frontier(slot, pool->ack_of(slot));
        if (frontier.is_empty() || frontier[0] < 0) {
            continue;
        }
        const Dictionary ran = counters_of(stepped[at]);
        NETW_CHECK_GE(
            int64_t(ran.get(StringName("journal_closed"), -99)),
            int64_t(0)
        );
        NETW_CHECK_GE(
            int64_t(ran.get(StringName("ack_frontier"), -99)),
            int64_t(0)
        );

        pool->publish_ack_frame(slot);
        const Dictionary counters = counters_of(stepped[at]);
        NETW_CHECK_EQ(
            int64_t(counters.get(StringName("journal_closed"), -99)),
            frontier[0]
        );
        NETW_CHECK_EQ(
            int64_t(counters.get(StringName("ack_frontier"), -99)),
            frontier[1]
        );
        ++published;
    }
    REQUIRE(published > 0);
}

TEST_CASE(
    "[Networked][Predict][Census] PC3 the tape diagnostics a consumer "
    "publishes are the command matrix's own epoch, standing depth and newest "
    "held transition, so a queue that holds transitions never reports -1"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);

    int held_lanes = 0;
    for (int at = 0; at < stepped.size(); ++at) {
        const Ref<NetwEntity> wrapper = stepped[at];
        REQUIRE(wrapper.is_valid());
        const int64_t slot = pool->slot_of(wrapper);
        REQUIRE(slot >= 0);

        const godot::PackedInt64Array transitions
            = pool->command_transitions(slot);
        if (transitions.is_empty()) {
            continue;
        }
        NETW_CHECK_GE(
            int64_t(counters_of(stepped[at]).get(StringName("tape_index"), -1)),
            int64_t(0)
        );

        pool->refresh_tape_diagnostics(slot);
        const Dictionary counters = counters_of(stepped[at]);
        NETW_CHECK_EQ(
            int64_t(counters.get(StringName("tape_epoch"), -99)),
            pool->command_epoch_of(slot)
        );
        NETW_CHECK_EQ(
            int64_t(counters.get(StringName("tape_queue_depth"), -99)),
            int64_t(
                pool->command_depth_from(slot, pool->replay_cursor_of(slot))
            )
        );
        NETW_CHECK_EQ(
            int64_t(counters.get(StringName("tape_index"), -99)),
            transitions[transitions.size() - 1]
        );
        ++held_lanes;
    }
    REQUIRE(held_lanes > 0);
}

TEST_CASE(
    "[Networked][Predict][Census] PC4 the owner's acknowledgement age "
    "is RECOMPUTED at every publish rather than read off a column the last "
    "drive left behind, so a drive far ahead widens the span it reports"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);

    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const int64_t settled = pool->refresh_owner_ack_age(slot);
    REQUIRE(settled > 0);

    const int64_t ahead = int64_t(1) << 20;
    pool->replay_drive(
        slot,
        Dictionary(),
        ahead,
        ahead,
        int(netw::DriveKind::FRESH),
        ahead,
        ahead,
        1.0 / 60.0,
        1,
        0,
        0,
        0,
        0,
        0,
        0,
        true
    );
    NETW_CHECK_GT(pool->refresh_owner_ack_age(slot), settled);
}

TEST_CASE(
    "[Networked][Predict][Census] PC5 authority judges the claim the "
    "owner filed for THAT transition, and a fingerprint that misses counts "
    "once as verified and once as a mismatch"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);
    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const netw::table::SchemaRecord *schema = pool->input_schema(slot);
    REQUIRE(schema != nullptr);

    godot::PackedInt32Array families;
    families.push_back(0);
    families.push_back(0);
    families.push_back(0);

    const int64_t claimed = pool->journal_last_closed(slot);
    REQUIRE(claimed >= 0);

    netw::predict::CommandFrameRecord filed;
    REQUIRE(netw::predict::CommandFrameRecord::open(*schema, filed));
    filed.set_epoch(int(pool->command_epoch_of(slot)));
    REQUIRE(filed.append_transition(claimed, claimed, true));
    REQUIRE(filed.append_evidence(0, 0, 4321, 0, 0, 0, families, families, 0));
    pool->clear_owner_claims(slot);
    pool->record_owner_claims(slot, filed);
    REQUIRE(pool->owner_claim_count(slot) == 1);

    const Dictionary before = counters_of(stepped[0]);
    pool->report_owner_claim(slot, claimed, 4321 + 1);
    const Dictionary after = counters_of(stepped[0]);

    NETW_CHECK_EQ(
        int64_t(after.get(StringName("client_fp_verified"), -99)),
        int64_t(before.get(StringName("client_fp_verified"), -1)) + 1
    );
    NETW_CHECK_EQ(
        int64_t(after.get(StringName("client_mismatches"), -99)),
        int64_t(before.get(StringName("client_mismatches"), -1)) + 1
    );
    NETW_CHECK_EQ(pool->owner_claim_count(slot), 0);
}

TEST_CASE(
    "[Networked][Predict][Census] PC6 a transition a resync steps "
    "over is journaled under the LABEL its queued command carries, so the "
    "acknowledgement run names the owner's own command rather than nothing"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);

    int declared = 0;
    for (int at = 0; at < stepped.size() && declared == 0; ++at) {
        const Ref<NetwEntity> wrapper = stepped[at];
        REQUIRE(wrapper.is_valid());
        const int64_t slot = pool->slot_of(wrapper);
        REQUIRE(slot >= 0);

        const godot::PackedInt64Array held = pool->command_transitions(slot);
        for (int index = 0; index < held.size(); ++index) {
            const int64_t transition = held[index];
            const int64_t label = pool->command_label_of(slot, transition);
            if (label < 0 || pool->journal_slot_of(slot, transition) >= 0) {
                continue;
            }
            pool->declare_skipped_run(slot, transition, transition + 1);
            const int row = pool->journal_slot_of(slot, transition);
            REQUIRE(row >= 0);
            NETW_CHECK_EQ(pool->journal_label_at(slot, row), label);
            ++declared;
            break;
        }
    }
    REQUIRE(declared > 0);
}

TEST_CASE(
    "[Networked][Predict][Census] PC8 a relayed frame is the AUTHOR's "
    "own bytes filed into the cell matrix, counted once, and a payload that "
    "does not decode is the only thing counted as a dropped frame"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    NetwMultiplayer *owner = rig.client(0);
    REQUIRE(server != nullptr);
    REQUIRE(owner != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    netw::NetwPredictionEngine *const authored = owner->get_prediction_engine();
    REQUIRE(pool != nullptr);
    REQUIRE(authored != nullptr);

    const TypedArray<godot::Object> mine = owner->predict_stepped_entities();
    REQUIRE(mine.size() > 0);
    const Ref<NetwEntity> claimed = mine[0];
    REQUIRE(claimed.is_valid());
    const int64_t author = authored->slot_of(claimed);
    REQUIRE(author >= 0);

    const godot::PackedByteArray shipped
        = authored->build_command_frame_for(author);
    REQUIRE(!shipped.is_empty());

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    int64_t relayed_to = -1;
    Ref<NetwPredictionHandle> mirrored;
    for (int at = 0; at < stepped.size(); ++at) {
        const Ref<NetwEntity> wrapper = stepped[at];
        if (wrapper.is_valid()
            && wrapper->get_entity_id() == claimed->get_entity_id()) {
            relayed_to = pool->slot_of(wrapper);
            mirrored = wrapper->get_prediction();
            break;
        }
    }
    REQUIRE(relayed_to >= 0);
    REQUIRE(mirrored.is_valid());
    REQUIRE(pool->input_schema(relayed_to) != nullptr);

    const Dictionary opened = stats_of_slot(server, relayed_to);
    pool->admit_relayed_payload(relayed_to, shipped);
    const Dictionary filed = stats_of_slot(server, relayed_to);
    NETW_CHECK_GT(
        int64_t(filed.get(StringName("relayed_recorded"), -1)),
        int64_t(opened.get(StringName("relayed_recorded"), -1))
    );

    SUBCASE("and the redundancy window re-sending it files nothing more") {
        pool->admit_relayed_payload(relayed_to, shipped);
        const Dictionary again = stats_of_slot(server, relayed_to);
        NETW_CHECK_EQ(
            int64_t(again.get(StringName("relayed_recorded"), -1)),
            int64_t(filed.get(StringName("relayed_recorded"), -2))
        );
    }

    SUBCASE("and only a member reconciling JOINTLY raises its epoch floor") {
        netw::predict::CommandFrameRecord decoded;
        REQUIRE(
            netw::predict::CommandFrameRecord::decode(
                *pool->input_schema(relayed_to),
                shipped,
                decoded
            )
        );

        mirrored->set_reconcile_mode(
            int(netw::NetwPredict::RECONCILE_INDEPENDENT)
        );
        decoded.set_epoch(decoded.epoch() + 1);
        pool->admit_relayed_payload(relayed_to, decoded.to_bytes());
        const int64_t alone = pool->joint_epoch_floor_of(relayed_to);

        mirrored->set_reconcile_mode(int(netw::NetwPredict::RECONCILE_JOINT));
        decoded.set_epoch(decoded.epoch() + 1);
        pool->admit_relayed_payload(relayed_to, decoded.to_bytes());
        NETW_CHECK_GT(pool->joint_epoch_floor_of(relayed_to), alone);
    }

    SUBCASE("and only an undecodable payload counts as a dropped frame") {
        godot::PackedByteArray rubbish;
        rubbish.push_back(0xff);
        rubbish.push_back(0xff);
        const Dictionary held = stats_of_slot(server, relayed_to);
        pool->admit_relayed_payload(relayed_to, rubbish);
        const Dictionary dropped = stats_of_slot(server, relayed_to);
        NETW_CHECK_EQ(
            int64_t(dropped.get(StringName("frames_dropped_invalid"), -1)),
            int64_t(held.get(StringName("frames_dropped_invalid"), -2)) + 1
        );
    }
}

TEST_CASE(
    "[Networked][Predict][Census] PC10 a tick tape that re-anchors "
    "off its own sequence starts a NEW epoch and un-licenses the ack domain, "
    "because a gapped index would otherwise straddle two numberings"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *owner = rig.client(0);
    REQUIRE(owner != nullptr);
    netw::NetwPredictionEngine *const authored = owner->get_prediction_engine();
    REQUIRE(authored != nullptr);

    const TypedArray<godot::Object> mine = owner->predict_stepped_entities();
    REQUIRE(mine.size() > 0);
    const Ref<NetwEntity> claimed = mine[0];
    REQUIRE(claimed.is_valid());
    const int64_t author = authored->slot_of(claimed);
    REQUIRE(author >= 0);

    const godot::PackedInt64Array span = authored->tape_span(author);
    REQUIRE(span.size() > 1);
    const int64_t newest = span[1];
    REQUIRE(newest >= 0);

    const int64_t epoch = authored->tape_epoch_of(author);
    authored->set_ack_domain_confirmed(author, true);

    authored->prepare_tick_tape(author, newest + 1);
    NETW_CHECK_EQ(authored->tape_epoch_of(author), epoch);
    CHECK(authored->ack_domain_confirmed_of(author));
    NETW_CHECK_EQ(authored->next_tape_entry_index_of(author), newest + 1);

    authored->prepare_tick_tape(author, newest + 9);
    NETW_CHECK_EQ(authored->tape_epoch_of(author), (epoch + 1) & 0xFF);
    CHECK_FALSE(authored->ack_domain_confirmed_of(author));
    NETW_CHECK_EQ(authored->ack_of_acks_of(author), int64_t(-1));
    NETW_CHECK_EQ(authored->next_tape_entry_index_of(author), newest + 9);
}

Ref<netw::NetwPhysicsStepper> installable_stepper() {
    Ref<netw_test::InstallableStepper> made;
    made.instantiate();
    return made;
}

TEST_CASE(
    "[Networked][Predict][Census] PC7 a STEPPED member is steppable "
    "only when the driver installed for ITS OWN space is there, so a stepper "
    "on another space leaves it as unsteppable as none at all"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);
    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const Ref<NetwPredictionHandle> handle = wrapper->get_prediction();
    REQUIRE(handle.is_valid());
    REQUIRE(handle->get_simulate().is_valid());
    CHECK(pool->slot_is_steppable(slot));

    handle->set_schedule(
        static_cast<netw::NetwPredict::Schedule>(int(netw::Schedule::STEPPED))
    );
    CHECK_FALSE(pool->slot_is_steppable(slot));

    const Ref<netw::NetwPhysicsStepper> stepper = installable_stepper();
    REQUIRE(stepper.is_valid());
    godot::Node *body = wrapper->get_owner();
    REQUIRE(body != nullptr);
    netw::gd::scene_root()->add_child(body);
    const godot::RID own = server->entity_space_of(wrapper).space;
    REQUIRE(own.is_valid());
    const godot::RID other
        = godot::PhysicsServer2D::get_singleton()->space_create();
    REQUIRE(other != own);

    server->predict_stepper_install(other, stepper);
    CHECK_FALSE(pool->slot_is_steppable(slot));

    server->predict_stepper_install(own, stepper);
    CHECK(pool->slot_is_steppable(slot));

    SUBCASE("and a member this peer only displays never steps at all") {
        handle->set_sim_mode(int(netw::NetwPredict::SIM_MODE_DISPLAY));
        CHECK_FALSE(pool->slot_is_steppable(slot));
    }

    godot::PhysicsServer2D::get_singleton()->free_rid(other);
    netw::gd::scene_root()->remove_child(body);
}

TEST_CASE(
    "[Networked][Predict][Census] PC11 replaying one queued entry "
    "advances the cursor past it, acknowledges exactly it, and remembers the "
    "label and freshness the QUEUE carried rather than the ones it drove"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);

    int replayed = 0;
    for (int at = 0; at < stepped.size(); ++at) {
        const Ref<NetwEntity> wrapper = stepped[at];
        REQUIRE(wrapper.is_valid());
        const int64_t slot = pool->slot_of(wrapper);
        REQUIRE(slot >= 0);

        const int64_t cursor = pool->replay_cursor_of(slot);
        if (!pool->command_has(slot, cursor)) {
            continue;
        }
        const int64_t label = pool->command_label_of(slot, cursor);
        const bool fresh = pool->command_is_fresh(slot, cursor);
        const int64_t consumed
            = int64_t(counters_of(stepped[at]).get(StringName("consumed"), -1));

        CHECK(pool->replay_tape_entry(slot, 1.0 / 60.0, cursor));

        NETW_CHECK_EQ(pool->ack_of(slot), cursor);
        NETW_CHECK_EQ(pool->replay_cursor_of(slot), cursor + 1);
        NETW_CHECK_EQ(pool->last_replayed_label_of(slot), label);
        CHECK(pool->last_replayed_fresh_of(slot) == fresh);
        NETW_CHECK_EQ(
            int64_t(counters_of(stepped[at]).get(StringName("consumed"), -1)),
            consumed + 1
        );
        ++replayed;
    }
    NETW_CHECK_GT(int64_t(replayed), int64_t(0));

    SUBCASE("and an entry the queue does not hold replays nothing at all") {
        const Ref<NetwEntity> wrapper = stepped[0];
        REQUIRE(wrapper.is_valid());
        const int64_t slot = pool->slot_of(wrapper);
        REQUIRE(slot >= 0);
        pool->set_replay_cursor(slot, int64_t(1) << 30);
        const int64_t acked = pool->ack_of(slot);
        CHECK_FALSE(pool->replay_tape_entry(slot, 1.0 / 60.0, 0));
        NETW_CHECK_EQ(pool->ack_of(slot), acked);
    }
}

TEST_CASE(
    "[Networked][Predict][Census] PC12 a consumer stranded past its "
    "declared lag re-opens at the newest arrival behind its buffer, declaring "
    "every tick it stepped over and floored to the acknowledgement window"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);
    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const Ref<netw::NetwTimeline> lane = pool->timeline_of(slot);
    REQUIRE(lane.is_valid());
    const int64_t newest = lane->newest_input_tick();
    REQUIRE(newest > 0);

    const int64_t standing = pool->next_input_tick_of(slot);
    const int64_t buffer = 2;
    const int64_t ceiling = 4;

    NETW_CHECK_EQ(pool->queued_span(slot), newest - standing + 1);

    pool->resync_input_if_stranded(slot, 0, buffer);
    NETW_CHECK_EQ(pool->next_input_tick_of(slot), standing);

    pool->set_next_input_tick(slot, newest - 1);
    pool->resync_input_if_stranded(slot, ceiling, buffer);
    NETW_CHECK_EQ(pool->next_input_tick_of(slot), newest - 1);

    pool->set_next_input_tick(slot, newest - 40);
    const Dictionary before = counters_of(stepped[0]);
    pool->resync_input_if_stranded(slot, ceiling, buffer);

    NETW_CHECK_EQ(pool->next_input_tick_of(slot), newest - buffer);
    const Dictionary after = counters_of(stepped[0]);
    NETW_CHECK_EQ(
        int64_t(after.get(StringName("resync"), -1)),
        int64_t(before.get(StringName("resync"), -2)) + 1
    );
    NETW_CHECK_EQ(
        int64_t(after.get(StringName("skipped"), -1)),
        int64_t(before.get(StringName("skipped"), -2)) + 40 - buffer
    );
    NETW_CHECK_GE(
        int64_t(pool->journal_slot_of(slot, newest - buffer - 1)),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Predict][Census] PC13 a JOINT island is admitted "
    "only where every member steps on a re-runnable tier, so an owner on a "
    "STEPPED tier with no driver installed runs INDEPENDENT and says so once"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);
    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const Ref<NetwPredictionHandle> handle = wrapper->get_prediction();
    REQUIRE(handle.is_valid());

    Ref<netw::NetwPredictIsland> island = handle->get_island();
    if (island.is_null()) {
        island.instantiate();
        handle->set_island(island);
    }
    island->set_reconcile(int(netw::NetwPredict::RECONCILE_INDEPENDENT));
    NETW_CHECK_EQ(
        pool->admitted_reconcile_mode(slot),
        int(netw::NetwPredict::RECONCILE_INDEPENDENT)
    );

    island->set_reconcile(int(netw::NetwPredict::RECONCILE_JOINT));
    NETW_CHECK_EQ(
        pool->admitted_reconcile_mode(slot),
        int(netw::NetwPredict::RECONCILE_JOINT)
    );
    CHECK_FALSE(pool->joint_refusal_reported_of(slot));

    handle->set_schedule(
        static_cast<netw::NetwPredict::Schedule>(int(netw::Schedule::STEPPED))
    );
    REQUIRE_FALSE(pool->slot_is_steppable(slot));
    NETW_CHECK_EQ(
        pool->admitted_reconcile_mode(slot),
        int(netw::NetwPredict::RECONCILE_INDEPENDENT)
    );
    CHECK(pool->joint_refusal_reported_of(slot));

    SUBCASE("and installing the driver its space wanted admits it again") {
        const Ref<netw::NetwPhysicsStepper> stepper = installable_stepper();
        REQUIRE(stepper.is_valid());
        godot::Node *body = wrapper->get_owner();
        REQUIRE(body != nullptr);
        netw::gd::scene_root()->add_child(body);
        server->predict_stepper_install(
            server->entity_space_of(wrapper).space,
            stepper
        );
        NETW_CHECK_EQ(
            pool->admitted_reconcile_mode(slot),
            int(netw::NetwPredict::RECONCILE_JOINT)
        );
        netw::gd::scene_root()->remove_child(body);
    }
}

TEST_CASE(
    "[Networked][Predict][Census] PC14 a listen-server host drives "
    "its own entity from its own gathered input and publishes the result "
    "acknowledging the very tick it authored, because it answers to nobody"
) {
    const Scenario scenario = hosted_tick_lane();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() > 0);
    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);
    NETW_CHECK_EQ(pool->role_of(slot), int(netw::Role::HOST_LOCAL));

    const Ref<netw::NetwPropertySetBinding> binding
        = pool->state_binding_of(slot);
    REQUIRE(binding.is_valid());
    NETW_CHECK_GT(binding->get_authored_tick(), int64_t(0));
    NETW_CHECK_EQ(binding->get_reconcile_ack(), binding->get_authored_tick());

    const Ref<netw::NetwTimeline> lane = pool->timeline_of(slot);
    REQUIRE(lane.is_valid());
    NETW_CHECK_EQ(lane->newest_input_tick(), binding->get_authored_tick());
    NETW_CHECK_GT(
        int64_t(counters_of(stepped[0]).get(StringName("drive_seq"), 0)),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Predict][Census] PC15 only a SOLVER BODY holds the "
    "simulation gate, and a forced release drops it whatever the archetype "
    "says, because a released subject is one the world may step freely"
) {
    const Scenario scenario = two_predicted_lanes();
    LoopbackRig rig(scenario.clients);
    const ScenarioRun run = ScenarioRun::session(rig, scenario);
    REQUIRE(run.regime_reached());

    NetwMultiplayer *server = rig.server();
    REQUIRE(server != nullptr);
    netw::NetwPredictionEngine *const pool = server->get_prediction_engine();
    REQUIRE(pool != nullptr);

    const TypedArray<godot::Object> stepped
        = server->predict_stepped_entities();
    REQUIRE(stepped.size() == 2);
    const Ref<NetwEntity> wrapper = stepped[0];
    REQUIRE(wrapper.is_valid());
    const int64_t slot = pool->slot_of(wrapper);
    REQUIRE(slot >= 0);

    const Ref<NetwPredictionHandle> handle = wrapper->get_prediction();
    REQUIRE(handle.is_valid());

    handle->set_archetype(netw::NetwPredict::ARCHETYPE_SOLVER_BODY);
    pool->refresh_simulation_gate(slot, false);
    const int64_t armed = server->simulation_gate_count();
    NETW_CHECK_GT(armed, int64_t(0));

    pool->refresh_simulation_gate(slot, true);
    NETW_CHECK_EQ(server->simulation_gate_count(), armed - 1);

    pool->refresh_simulation_gate(slot, false);
    NETW_CHECK_EQ(server->simulation_gate_count(), armed);

    handle->set_archetype(netw::NetwPredict::ARCHETYPE_KINEMATIC);
    pool->refresh_simulation_gate(slot, false);
    NETW_CHECK_EQ(server->simulation_gate_count(), armed - 1);
}

} // namespace TestNetwPredictCensusLaws

#endif

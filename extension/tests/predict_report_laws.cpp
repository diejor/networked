#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/performance.hpp"
#include "support/netw_recorder.h"

#include "netw/api/entity.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/api/timeline.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"
#include "netw/prediction_core.hpp"
#include "netw/script/model.hpp"

#include <godot_cpp/classes/node2d.hpp>

namespace TestNetwPredictReportLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionEngine;
namespace predict = netw::predict;
using netw::NetwPredictionHandle;
using netw::NetwPropertySet;
using netw::NetwPropertySetBinding;
using netw::NetwPropertySetColumn;

const char *REPORTED_ID = "reported_player";

Node *build_player(const Variant &p_name) {
    Node2D *made = memnew(Node2D);
    made->set_name(String(p_name));
    return made;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

Ref<NetwEntity> seat_player(LoopbackRig &p_rig, Node *p_parent) {
    const int route = p_rig.spawn_registered(
        StringName(REPORTED_ID),
        callable_mp_static(&build_player),
        named("Reported"),
        one_type(),
        p_parent
    );
    Node *node = p_rig.route_node(route, -1);
    REQUIRE_MESSAGE(node != nullptr, "the spawn reached no node");
    return NetwEntity::of(node);
}

Dictionary charged(int64_t p_transition) {
    Dictionary out;
    out[StringName("transition")] = p_transition;
    return out;
}

Array rows_of(const Array &p_drained, int64_t p_event) {
    Array kept;
    for (int at = 0; at < p_drained.size(); ++at) {
        const Dictionary row = p_drained[at];
        if (!row.is_empty()
            && int64_t(row[netw::event_key::event()]) == p_event) {
            kept.push_back(row);
        }
    }
    return kept;
}

TEST_CASE(
    "[Networked][Predict][Report] PR1 a prediction fact names the "
    "entity seated in the slot it is about, and rides that entity's own "
    "liveness route, so a caller states what happened and never spells who "
    "it happened to"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const int64_t route = core->liveness_route_of(seated.ptr());
    REQUIRE(route > 0);

    core->event_arm(true);
    core->event_ring(route);
    pool->report_predict(
        slot,
        netw::EventPlane::PREDICT_DRIVE,
        charged(7),
        Dictionary()
    );
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    const Array driven = rows_of(drained, netw::EventPlane::PREDICT_DRIVE);
    REQUIRE(driven.size() == 1);
    const Dictionary row = driven[0];
    const StringName entity_id = row[netw::event_key::entity_id()];
    CHECK(entity_id == seated->get_entity_id());
    NETW_CHECK_EQ(int64_t(row[netw::event_key::route()]), route);
    const Dictionary detail = row[netw::event_key::detail()];
    NETW_CHECK_EQ(
        int64_t(detail.get(StringName("transition"), -1)),
        int64_t(7)
    );
}

TEST_CASE(
    "[Networked][Predict][Report] PR2 a slot with no seated entity "
    "names no route, and a fact naming no subject is one no watcher can act "
    "on, so it reaches nobody"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    REQUIRE(pool != nullptr);
    const int64_t route = core->liveness_route_of(seated.ptr());
    REQUIRE(route > 0);
    const int64_t unseated = pool->open();
    REQUIRE(unseated >= 0);

    core->event_arm(true);
    core->event_ring(route);
    core->event_ring(0);
    pool->report_predict(
        unseated,
        netw::EventPlane::PREDICT_DRIVE,
        charged(7),
        Dictionary()
    );
    const Array on_route = core->event_ring(route);
    const Array unrouted = core->event_ring(0);
    core->event_arm(false);

    NETW_CHECK_EQ(rows_of(on_route, netw::EventPlane::PREDICT_DRIVE).size(), 0);
    NETW_CHECK_EQ(rows_of(unrouted, netw::EventPlane::PREDICT_DRIVE).size(), 0);
}

Vector<StringName> episode_edges() {
    Vector<StringName> names;
    names.push_back(StringName("episode_opened"));
    names.push_back(StringName("episode_closed"));
    names.push_back(StringName("episode_fallback"));
    return names;
}

TEST_CASE(
    "[Networked][Predict][Report] PR3 an episode edge takes its "
    "signal from the event that names it, and an event naming no edge "
    "announces nothing at all"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    REQUIRE(core != nullptr);
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const int64_t route = core->liveness_route_of(seated.ptr());

    Object *held = netw::gd::live_object(seated->get_prediction());
    REQUIRE_MESSAGE(held != nullptr, "the entity minted no prediction handle");
    Recorder edges(held, episode_edges());

    core->event_arm(true);
    core->event_ring(route);
    pool->announce_episode(slot, netw::EventPlane::EPISODE_OPEN, Dictionary());
    pool->announce_episode(
        slot,
        netw::EventPlane::EPISODE_FALLBACK,
        Dictionary()
    );
    pool->announce_episode(slot, netw::EventPlane::PREDICT_DRIVE, Dictionary());
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    NETW_CHECK_EQ(edges.count(StringName("episode_opened")), 1);
    NETW_CHECK_EQ(edges.count(StringName("episode_fallback")), 1);
    NETW_CHECK_EQ(edges.count(StringName("episode_closed")), 0);
    NETW_CHECK_EQ(rows_of(drained, netw::EventPlane::EPISODE_OPEN).size(), 1);
    NETW_CHECK_EQ(
        rows_of(drained, netw::EventPlane::EPISODE_FALLBACK).size(),
        1
    );
    NETW_CHECK_EQ(rows_of(drained, netw::EventPlane::PREDICT_DRIVE).size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Report] PR4 a judged disagreement reaches "
    "the game's signal and the watcher's row carrying the same transition "
    "and the same boundary"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const int64_t route = core->liveness_route_of(seated.ptr());

    Object *held = netw::gd::live_object(seated->get_prediction());
    REQUIRE(held != nullptr);
    Vector<StringName> watched;
    watched.push_back(StringName("divergence_detected"));
    Recorder judged(held, watched);

    const int64_t contact = int64_t(netw::predict::Attribution::CONTACT);
    core->event_arm(true);
    core->event_ring(route);
    pool->announce_divergence(slot, 7, contact, 0.5);
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    NETW_CHECK_EQ(judged.count(StringName("divergence_detected")), 1);
    const Array said = judged.args(StringName("divergence_detected"));
    REQUIRE(said.size() == 2);
    NETW_CHECK_EQ(int64_t(said[0]), int64_t(7));
    NETW_CHECK_EQ(int64_t(said[1]), contact);

    const Array rows = rows_of(drained, netw::EventPlane::DIVERGENCE);
    REQUIRE(rows.size() == 1);
    const Dictionary row = rows[0];
    const Dictionary detail = row[netw::event_key::detail()];
    NETW_CHECK_EQ(
        int64_t(detail.get(StringName("transition"), -1)),
        int64_t(7)
    );
    NETW_CHECK_EQ(int64_t(detail.get(StringName("attribution"), -1)), contact);
    NETW_CHECK_CLOSE(
        double(detail.get(StringName("divergence"), 0.0)),
        0.5,
        1.0e-9
    );
}

TEST_CASE(
    "[Networked][Predict][Report] PR5 a consume pass reports the "
    "queue it was handed and the action it took on it"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const int64_t route = core->liveness_route_of(seated.ptr());

    core->event_arm(true);
    core->event_ring(route);
    pool->report_consume(slot, 3, 2, 1);
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    const Array rows = rows_of(drained, netw::EventPlane::PREDICT_CONSUME);
    REQUIRE(rows.size() == 1);
    const Dictionary row = rows[0];
    const Dictionary detail = row[netw::event_key::detail()];
    NETW_CHECK_EQ(int64_t(detail.get(StringName("depth"), -1)), int64_t(3));
    NETW_CHECK_EQ(int64_t(detail.get(StringName("buffer"), -1)), int64_t(2));
    NETW_CHECK_EQ(int64_t(detail.get(StringName("action"), -1)), int64_t(1));
}

TEST_CASE(
    "[Networked][Predict][Report] PR8 a comparison announces the "
    "opening edge only where the episode was NOT open before it, so a run of "
    "comparisons inside one episode announces it once"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const int64_t route = core->liveness_route_of(seated.ptr());
    REQUIRE(pool->open_episode(slot, 3, 1));

    Object *held = netw::gd::live_object(seated->get_prediction());
    REQUIRE(held != nullptr);
    Recorder edges(held, episode_edges());

    core->event_arm(true);
    core->event_ring(route);
    pool->record_episode_comparison(slot, -1);
    pool->record_episode_comparison(
        slot,
        int64_t(netw::NetwPredict::EPISODE_STATE_OPEN)
    );
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    NETW_CHECK_EQ(edges.count(StringName("episode_opened")), 1);
    NETW_CHECK_EQ(rows_of(drained, netw::EventPlane::EPISODE_OPEN).size(), 1);
}

int fallbacks_asked = 0;
int64_t fallback_ack = -1;

void count_fallback(int64_t p_ack, int64_t) {
    fallbacks_asked += 1;
    fallback_ack = p_ack;
}

TEST_CASE(
    "[Networked][Predict][Report] PR9 a comparison judged on "
    "probation and found dirty goes STRAIGHT back to quarantine, announcing "
    "the divergence before the fallback that answers it"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    Object *held = netw::gd::live_object(seated->get_prediction());
    REQUIRE(held != nullptr);
    Vector<StringName> watched;
    watched.push_back(StringName("divergence_detected"));
    watched.push_back(StringName("state_evaluated"));
    Recorder judged(held, watched);

    fallbacks_asked = 0;
    fallback_ack = -1;
    NETW_CHECK_EQ(
        pool->settle_comparison(
            slot,
            11,
            4,
            true,
            true,
            true,
            0.5,
            true,
            -1,
            callable_mp_static(&count_fallback)
        ),
        int(NetwPredictionEngine::SETTLE_REQUARANTINED)
    );

    NETW_CHECK_EQ(judged.count(StringName("divergence_detected")), 1);
    NETW_CHECK_EQ(judged.count(StringName("state_evaluated")), 1);
    NETW_CHECK_EQ(fallbacks_asked, 1);
    NETW_CHECK_EQ(fallback_ack, 4);
    NETW_CHECK_EQ(
        int64_t(pool->verdict_reason_of(slot)),
        int64_t(netw::NetwPredict::VERDICT_REASON_PROBATION_REQUARANTINE)
    );
    const Array said = judged.args(StringName("state_evaluated"));
    REQUIRE(said.size() == 4);
    NETW_CHECK_EQ(int64_t(said[1]), int64_t(4));
    NETW_CHECK_EQ(bool(said[3]), 1);
}

TEST_CASE(
    "[Networked][Predict][Report] PR10 a comparison nobody is on "
    "probation for settles into the ladder, so the caller is told to proceed "
    "and no fallback is asked for"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    Object *held = netw::gd::live_object(seated->get_prediction());
    Vector<StringName> watched;
    watched.push_back(StringName("divergence_detected"));
    Recorder judged(held, watched);

    fallbacks_asked = 0;
    NETW_CHECK_EQ(
        pool->settle_comparison(
            slot,
            11,
            4,
            true,
            true,
            true,
            0.5,
            false,
            -1,
            callable_mp_static(&count_fallback)
        ),
        int(NetwPredictionEngine::SETTLE_PROCEED)
    );

    NETW_CHECK_EQ(judged.count(StringName("divergence_detected")), 1);
    NETW_CHECK_EQ(fallbacks_asked, 0);
    NETW_CHECK_EQ(
        pool->episode_state(slot),
        int(netw::NetwPredict::EPISODE_STATE_OPEN)
    );
}

int commands_sent = 0;

void count_command() {
    commands_sent += 1;
}

void count_demote(int64_t p_transition, int64_t, bool) {
    fallbacks_asked += 1;
    fallback_ack = p_transition;
}

TEST_CASE(
    "[Networked][Predict][Report] PR11 a breach demotes only where "
    "the declaration asked for it, and the demoting slot preserves the "
    "command that produced the breach BEFORE the fallback rewires"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE(handle != nullptr);
    handle->set_witness_contacts(callable_mp_static(&count_command));

    const Callable sends = callable_mp_static(&count_command);
    const Callable demotes = callable_mp_static(&count_demote);

    commands_sent = 0;
    fallbacks_asked = 0;
    handle->set_breach_response(NetwPredict::BREACH_RESPONSE_PREDICT_THROUGH);
    NETW_CHECK_EQ(
        pool->demote_for_breach(slot, 9, true, 77, sends, demotes),
        0
    );
    NETW_CHECK_EQ(commands_sent, 0);

    handle->set_breach_response(NetwPredict::BREACH_RESPONSE_DEMOTE);
    NETW_CHECK_EQ(
        pool->demote_for_breach(slot, 9, false, 77, sends, demotes),
        0
    );
    NETW_CHECK_EQ(commands_sent, 0);

    NETW_CHECK_EQ(
        pool->demote_for_breach(slot, 9, true, 77, sends, demotes),
        1
    );
    NETW_CHECK_EQ(commands_sent, 1);
    NETW_CHECK_EQ(fallbacks_asked, 1);
    NETW_CHECK_EQ(fallback_ack, 9);
    NETW_CHECK_EQ(
        pool->episode_state(slot),
        int(NetwPredict::EPISODE_STATE_OPEN)
    );
}

Ref<NetwPropertySetColumn> pose_column() {
    return NetwPropertySetColumn::create(
        StringName("position"),
        Ref<netw::NetwQuantize>(),
        false,
        int64_t(netw::SchemaCore::VARIANT)
    );
}

Ref<NetwPropertySet> one_column_set() {
    Ref<NetwPropertySet> set;
    set.instantiate();
    set->record = NetwPropertySet::RECORD_STATE;
    set->bind_column(pose_column());
    return set;
}

void nothing(const Variant &) {
}

TEST_CASE(
    "[Networked][Predict][Report] PR17 a pool outside a session names "
    "no present and registers no timeline, so a caller can tell an absent "
    "clock from tick zero and an absent registry from an empty history"
) {
    NetwPredictionEngine orphan;
    NETW_CHECK_EQ(orphan.current_tick(), -1);
    NETW_CHECK_EQ(orphan.register_timeline(Ref<NetwEntity>()).is_null(), 1);

    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    NETW_CHECK_EQ(pool->current_tick(), core->clock_get_tick());

    const Ref<netw::NetwTimeline> first = pool->register_timeline(seated);
    REQUIRE(first.is_valid());
    NETW_CHECK_EQ(pool->register_timeline(seated) == first, 1);
}

TEST_CASE(
    "[Networked][Predict][Report] PR15 a handle whose entity is not "
    "driven by an engine answers EMPTY for every report it publishes, "
    "because a view over no slot has nothing to be a view of and a stale "
    "answer reads to a game exactly like a live one"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE(handle != nullptr);

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    REQUIRE(pool->open_episode(slot, 3, 1));

    NETW_CHECK_EQ(handle->episode().is_empty(), 1);
    NETW_CHECK_EQ(handle->episode_digest().is_empty(), 1);
    NETW_CHECK_EQ(handle->reachability().is_empty(), 1);
    NETW_CHECK_EQ(handle->teleport_distances().is_empty(), 1);
    NETW_CHECK_EQ(handle->tape_transitions().is_empty(), 1);
    REQUIRE(handle->journal().is_valid());
    NETW_CHECK_EQ(handle->journal()->size(), 0);
}

Variant carry_never(const Variant &, const Variant &, double) {
    return Variant();
}

Ref<NetwPropertySet> pose_and_momentum() {
    Ref<NetwPropertySet> set;
    set.instantiate();
    set->record = NetwPropertySet::RECORD_STATE;
    set->bind_column(
        NetwPropertySetColumn::create(
            StringName("position"),
            Ref<netw::NetwQuantize>(),
            false,
            int64_t(netw::SchemaCore::VARIANT)
        )
    );
    set->bind_column(
        NetwPropertySetColumn::create(
            StringName("velocity"),
            Ref<netw::NetwQuantize>(),
            false,
            int64_t(netw::SchemaCore::VARIANT)
        )
    );
    return set;
}

LocalVector<netw::predict::FieldDecl> declared_pose(bool p_channelled) {
    LocalVector<netw::predict::FieldDecl> out;
    out.push_back(
        netw::field_decl(
            StringName("position"),
            int(NetwPropertySet::CAUSAL),
            p_channelled ? StringName("velocity") : StringName()
        )
    );
    out.push_back(
        netw::field_decl(StringName("velocity"), int(NetwPropertySet::CAUSAL))
    );
    return out;
}

int64_t carrying_slot(
    NetwPredictionEngine *p_pool,
    Node *p_node,
    bool p_channelled
) {
    const int64_t slot = p_pool->open(declared_pose(p_channelled));
    REQUIRE(slot >= 0);
    p_pool->bind_property_sets(
        slot,
        NetwPropertySetBinding::create(pose_and_momentum(), p_node),
        Ref<NetwPropertySetBinding>()
    );
    return slot;
}

TEST_CASE(
    "[Networked][Predict][Report] PR16 a slot adopts its whole "
    "field declaration from the state binding it is handed, so the carry "
    "channel and the per-field epsilon the game declared are the ones the "
    "recovery ladder reads"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();
    REQUIRE(owner != nullptr);

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    const Ref<NetwPropertySet> state = pose_and_momentum();
    const Ref<NetwPropertySetColumn> pose = state->get_columns()[0];
    pose->carry_channel = StringName("velocity");
    pose->epsilon_override = 0.25;

    NETW_CHECK_EQ(
        pool->adopt_declaration(
            seated,
            NetwPropertySetBinding::create(state, owner),
            NetwPropertySetBinding::create(pose_and_momentum(), owner),
            int(netw::Schedule::TICK),
            int(netw::Role::PREDICT),
            int(netw::CorrectionMode::SNAP),
            int(netw::RestoreMode::EXACT),
            6,
            0,
            false
        ),
        int(netw::CorrectionMode::SNAP)
    );

    const int field = pool->field_slot(slot, StringName("position"));
    REQUIRE(field >= 0);
    NETW_CHECK_EQ(
        pool->projection_of(slot, field),
        pool->field_slot(slot, StringName("velocity"))
    );
    NETW_CHECK_CLOSE(pool->epsilon_of(slot, field), 0.25, 1.0e-9);
    NETW_CHECK_EQ(pool->owner_bound(slot), 1);
}

TEST_CASE(
    "[Networked][Predict][Report] PR18 the no-write window captures "
    "what the whole recovery is diffed against BEFORE it stages anything, "
    "and a slot with no corridor and nothing to dissipate falls through to "
    "the ladder holding that capture"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->bind_property_sets(
        slot,
        NetwPropertySetBinding::create(pose_and_momentum(), owner),
        Ref<NetwPropertySetBinding>()
    );
    pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );

    NETW_CHECK_EQ(pool->recovery_before_of(slot).is_empty(), 1);

    NETW_CHECK_EQ(
        pool->open_recovery(slot, 4, Dictionary(), Dictionary(), 1, 0),
        int(NetwPredictionEngine::RECOVERY_PLAN)
    );

    NETW_CHECK_EQ(pool->recovery_before_of(slot).is_empty(), 0);
    NETW_CHECK_EQ(pool->recovery_write_of(slot).is_empty(), 1);
    NETW_CHECK_EQ(pool->reconciling(slot), 1);
}

TEST_CASE(
    "[Networked][Predict][Report] PR19 a recovery that DECLINED "
    "names its refusal and writes nothing, and one that carried names the "
    "operator its plan earned, because agreement and a refusal are not the "
    "same row"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->bind_property_sets(
        slot,
        NetwPropertySetBinding::create(pose_and_momentum(), owner),
        Ref<NetwPropertySetBinding>()
    );

    Dictionary written;
    written[StringName("position")] = Vector2(1.0, 0.0);

    netw::RecoveryPlan declined;
    declined.write = written;
    pool->apply_recovery_plan(slot, declined, 4, true);
    NETW_CHECK_EQ(
        int64_t(pool->verdict_reason_of(slot)),
        int64_t(NetwPredict::VERDICT_REASON_DECLINED)
    );
    NETW_CHECK_EQ(pool->recovery_write_of(slot).is_empty(), 1);
    NETW_CHECK_EQ(pool->last_correction_teleported_of(slot), 0);

    netw::RecoveryPlan teleported;
    teleported.restore = written;
    teleported.write = written;
    teleported.teleport = true;
    teleported.skip = false;
    pool->apply_recovery_plan(slot, teleported, 5, true);
    NETW_CHECK_EQ(pool->recovery_write_of(slot).is_empty(), 0);
    NETW_CHECK_EQ(pool->last_correction_teleported_of(slot), 1);
}

int demotes_charged = 0;
int64_t demoted_at = -1;

void count_demote_tick(int64_t p_transition, const Dictionary &) {
    demotes_charged += 1;
    demoted_at = p_transition;
}

TEST_CASE(
    "[Networked][Predict][Report] PR20 a TICK replay re-runs every "
    "unacknowledged input over the restored state, records what each one "
    "produced, and leaves the live input the owner is holding untouched"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->bind_property_sets(
        slot,
        NetwPropertySetBinding::create(pose_and_momentum(), owner),
        Ref<NetwPropertySetBinding>()
    );

    const Ref<netw::NetwTimeline> history = pool->register_timeline(seated);
    REQUIRE(history.is_valid());
    pool->bind_timeline(slot, history);

    Dictionary held;
    held[StringName("position")] = Vector2(1.0, 0.0);
    history->record_input(5, held);
    history->record_input(6, held);
    history->record_input(8, held);

    demotes_charged = 0;
    NETW_CHECK_EQ(
        pool->replay_tick_window(
            slot,
            5,
            6,
            Dictionary(),
            PackedStringArray(),
            callable_mp_static(&count_demote_tick)
        ),
        2
    );
    NETW_CHECK_EQ(history->state_at(6).is_empty(), 0);
    NETW_CHECK_EQ(history->state_at(7).is_empty(), 0);
    NETW_CHECK_EQ(history->state_at(9).is_empty(), 1);

    NETW_CHECK_EQ(
        pool->replay_tick_window(
            slot,
            9,
            9,
            Dictionary(),
            PackedStringArray(),
            callable_mp_static(&count_demote_tick)
        ),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Report] PR21 the ladder resolves its own "
    "projection, its own carried payload and its own tier errors, and an "
    "ESCALATED recovery measures no tier error at all because past the "
    "escalation it may no longer claim it sits below the teleport tier"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const Ref<NetwPropertySet> declared = pose_and_momentum();
    Ref<NetwPropertySetColumn>(declared->get_columns()[0])->carry_channel
        = StringName("velocity");
    pool->adopt_declaration(
        seated,
        NetwPropertySetBinding::create(declared, owner),
        Ref<NetwPropertySetBinding>(),
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        0,
        false
    );
    REQUIRE(pool->has_pose_fields(slot));

    Dictionary authority;
    authority[StringName("position")] = Vector2(4.0, 0.0);
    authority[StringName("velocity")] = Vector2();

    pool->open_recovery(slot, 4, Dictionary(), authority, 1, 0);
    pool->plan_recovery(slot, 4, 4, Dictionary(), authority, false, 0, 1);
    NETW_CHECK_EQ(pool->recovery_carried_of(slot).is_empty(), 0);
    const Dictionary measured = pool->recovery_tier_errors_of(slot);

    pool->plan_recovery(slot, 5, 5, Dictionary(), authority, true, 0, 1);
    NETW_CHECK_EQ(pool->recovery_tier_errors_of(slot).is_empty(), 1);
    NETW_CHECK_EQ(measured.is_empty(), 0);
}

void frame_sink(const Dictionary &) {
}

TEST_CASE(
    "[Networked][Predict][Report] PR22 the role decides the whole "
    "feed, so a role that names no frame sink un-installs the previous "
    "role's wiring rather than inheriting it"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const Callable sink = callable_mp_static(&frame_sink);

    const netw::predict::Feed predicting = pool->role_feed(
        int(netw::Role::PREDICT),
        false,
        sink,
        sink,
        sink,
        sink
    );
    NETW_CHECK_EQ(predicting.state_on_applied == sink, 1);
    NETW_CHECK_EQ(predicting.state_write_gate, 0);
    NETW_CHECK_EQ(predicting.input_volatile_external, 1);

    const netw::predict::Feed consuming = pool->role_feed(
        int(netw::Role::CONSUME),
        false,
        sink,
        sink,
        sink,
        sink
    );
    NETW_CHECK_EQ(consuming.input_on_applied == sink, 1);
    NETW_CHECK_EQ(consuming.state_on_applied.is_valid(), 0);
    NETW_CHECK_EQ(consuming.input_write_gate, 0);

    const netw::predict::Feed remote = pool->role_feed(
        int(netw::Role::REMOTE),
        false,
        sink,
        sink,
        sink,
        sink
    );
    NETW_CHECK_EQ(remote.state_on_applied.is_valid(), 0);
    NETW_CHECK_EQ(remote.input_on_applied.is_valid(), 0);
    NETW_CHECK_EQ(remote.state_write_gate, 1);

    const netw::predict::Feed latched = pool->role_feed(
        int(netw::Role::REMOTE),
        true,
        sink,
        sink,
        sink,
        sink
    );
    NETW_CHECK_EQ(latched.state_on_applied == sink, 1);
    NETW_CHECK_EQ(latched.state_write_gate, 1);
}

TEST_CASE(
    "[Networked][Predict][Report] PR23 the reseed alignment records "
    "the payload as AUTHORITY stated it, never as the seed advanced it, "
    "because the record is what later transitions are compared against"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const Ref<NetwPropertySet> declared = pose_and_momentum();
    Ref<NetwPropertySetColumn>(declared->get_columns()[0])->carry_channel
        = StringName("velocity");
    pool->adopt_declaration(
        seated,
        NetwPropertySetBinding::create(declared, owner),
        Ref<NetwPropertySetBinding>(),
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXTRAPOLATED),
        6,
        0,
        false
    );
    Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    )
        ->set_ack_age_ticks(4);
    const Ref<netw::NetwTimeline> history = pool->register_timeline(seated);
    REQUIRE(history.is_valid());
    pool->bind_timeline(slot, history);

    Dictionary stated;
    stated[StringName("position")] = Vector2(9.0, 0.0);
    stated[StringName("velocity")] = Vector2(3.0, 0.0);

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE(handle != nullptr);
    Vector<StringName> watched;
    watched.push_back(StringName("state_evaluated"));
    Recorder judged(handle, watched);

    pool->finish_reseed_alignment(
        slot,
        11,
        4,
        stated,
        Dictionary(),
        PackedStringArray(),
        Callable()
    );

    const Dictionary recorded = history->state_at(5);
    NETW_CHECK_EQ(recorded.is_empty(), 0);
    NETW_CHECK_EQ(
        Vector2(recorded.get(StringName("position"), Vector2()))
            == Vector2(9.0, 0.0),
        1
    );
    NETW_CHECK_EQ(judged.count(StringName("state_evaluated")), 1);
    const Array said = judged.args(StringName("state_evaluated"));
    REQUIRE(said.size() == 4);
    NETW_CHECK_EQ(int64_t(said[1]), int64_t(4));
    NETW_CHECK_EQ(bool(said[3]), 0);
}

Dictionary simulated_header(int64_t p_tick, bool p_whole) {
    Dictionary payload;
    payload[StringName("position")] = Vector2(2.0, 0.0);
    payload[StringName("velocity")] = Vector2();
    Dictionary header;
    header[StringName("tick")] = p_tick;
    header[StringName("whole")] = p_whole;
    header[StringName("payload")] = payload;
    return header;
}

TEST_CASE(
    "[Networked][Predict][Report] PR24 a simulated remote accepts no "
    "row before its stream has RECONSTRUCTED, because a partial mosaic is "
    "authoritative at no single tick and rebasing on one invents a state "
    "authority never held"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->adopt_declaration(
        seated,
        NetwPropertySetBinding::create(pose_and_momentum(), owner),
        Ref<NetwPropertySetBinding>(),
        int(netw::Schedule::TICK),
        int(netw::Role::SIMULATE),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        0,
        false
    );

    NetwPredictionHandle *handle = Object::cast_to<NetwPredictionHandle>(
        netw::gd::live_object(seated->get_prediction())
    );
    REQUIRE(handle != nullptr);
    Vector<StringName> watched;
    watched.push_back(StringName("state_evaluated"));
    Recorder judged(handle, watched);

    NETW_CHECK_EQ(pool->stream_reconstructed_of(slot), 0);
    pool->admit_simulated_state(slot, simulated_header(7, false));
    NETW_CHECK_EQ(judged.count(StringName("state_evaluated")), 0);

    pool->admit_simulated_state(slot, simulated_header(8, true));
    NETW_CHECK_EQ(pool->stream_reconstructed_of(slot), 1);
    NETW_CHECK_EQ(judged.count(StringName("state_evaluated")), 1);
    const Array said = judged.args(StringName("state_evaluated"));
    REQUIRE(said.size() == 4);
    NETW_CHECK_EQ(int64_t(said[0]), int64_t(8));
    NETW_CHECK_EQ(int64_t(said[1]), int64_t(-1));
    NETW_CHECK_EQ(
        int64_t(pool->verdict_reason_of(slot)),
        int64_t(NetwPredict::VERDICT_REASON_NONE)
    );
}

TEST_CASE(
    "[Networked][Predict][Report] PR25 a drive the pool RECORDED is "
    "reported, and a drive it refused is not, because a watcher counting "
    "rows would otherwise count passes that never opened a transition"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    Node *owner = seated->get_owner();

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    pool->adopt_declaration(
        seated,
        NetwPropertySetBinding::create(pose_and_momentum(), owner),
        Ref<NetwPropertySetBinding>(),
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT),
        6,
        0,
        false
    );
    const int64_t route = core->liveness_route_of(seated.ptr());

    core->event_arm(true);
    core->event_ring(route);
    const Dictionary drove = pool->record_drive(
        slot,
        0,
        0,
        int(netw::DriveKind::FRESH),
        Dictionary(),
        0,
        true,
        true,
        true
    );
    const Dictionary refused = pool->record_drive(
        slot,
        -1,
        1,
        int(netw::DriveKind::NONE),
        Dictionary(),
        -1,
        false,
        true,
        true
    );
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    NETW_CHECK_EQ(drove.is_empty(), 0);
    NETW_CHECK_EQ(refused.is_empty(), 1);
    NETW_CHECK_EQ(rows_of(drained, netw::EventPlane::PREDICT_DRIVE).size(), 1);
}

TEST_CASE(
    "[Networked][Predict][Report] PR14 a field whose forward model is "
    "already a carry channel keeps NO step rule, because one field carries "
    "one forward model and the channel is the one in use"
) {
    Node2D *owner = memnew(Node2D);
    owner->set_name("Carried");
    netw::script::model::bind_node_property_carry(
        owner,
        StringName("position"),
        callable_mp_static(&carry_never)
    );

    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t plain = carrying_slot(pool, owner, false);
    const int64_t channelled = carrying_slot(pool, owner, true);

    pool->push_carry_rules(plain);
    NETW_CHECK_EQ(pool->has_carry_rule(plain, StringName("position")), 1);

    pool->push_carry_rules(channelled);
    NETW_CHECK_EQ(pool->has_carry_rule(channelled, StringName("position")), 0);

    netw::script::model::clear_node_overlay(owner);
    memdelete(owner);
}

TEST_CASE(
    "[Networked][Predict][Report] PR13 a recovery that MOVED nothing "
    "announces nothing, because a recovered signal carrying an empty delta "
    "reads to a game exactly like one that repaired the body"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);
    const int64_t route = core->liveness_route_of(seated.ptr());

    Object *held = netw::gd::live_object(seated->get_prediction());
    REQUIRE(held != nullptr);
    Vector<StringName> watched;
    watched.push_back(StringName("recovered"));
    Recorder repaired(held, watched);

    core->event_arm(true);
    core->event_ring(route);
    NETW_CHECK_EQ(
        pool->announce_recovered(slot, 4, 1, Dictionary(), Dictionary()),
        0
    );
    const Array drained = core->event_ring(route);
    core->event_arm(false);

    NETW_CHECK_EQ(repaired.count(StringName("recovered")), 0);
    NETW_CHECK_EQ(rows_of(drained, netw::EventPlane::RECOVERY).size(), 0);
}

int findings_emitted = 0;

void count_findings(const Array &) {
    findings_emitted += 1;
}

TEST_CASE(
    "[Networked][Predict][Report] PR12 the declaration report is "
    "keyed to the config it judges, so a rewire that changed nothing reports "
    "nothing and a config that moved reports again"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    const Ref<NetwPropertySet> set = one_column_set();
    const Callable emits = callable_mp_static(&count_findings);

    findings_emitted = 0;
    pool->validate_declaration(slot, set, emits);
    NETW_CHECK_EQ(findings_emitted, 1);

    pool->validate_declaration(slot, set, emits);
    NETW_CHECK_EQ(findings_emitted, 1);

    const Ref<NetwPropertySetColumn> column = set->get_columns()[0];
    column->property_class = int64_t(NetwPropertySet::COSMETIC);
    pool->validate_declaration(slot, set, emits);
    NETW_CHECK_EQ(findings_emitted, 2);
}

TEST_CASE(
    "[Networked][Predict][Report] PR6 the property-class report is "
    "keyed to the whole config it judges, so every fact the report would "
    "read moves the key and a rewire that changes nothing does not"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(slot >= 0);

    const Ref<NetwPropertySet> set = one_column_set();
    const int64_t stable = pool->property_class_report_hash(slot, set);
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set), stable);

    const Ref<NetwPropertySetColumn> column = set->get_columns()[0];

    column->property_class = int64_t(netw::NetwPropertySet::COSMETIC);
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != stable, 1);
    column->property_class = int64_t(netw::NetwPropertySet::CAUSAL);

    column->epsilon_override = 0.25;
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != stable, 1);
    column->epsilon_override = -1.0;

    column->explicit_teleport_only = true;
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != stable, 1);
    column->explicit_teleport_only = false;

    column->explicit_reconcile_only = true;
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != stable, 1);
    column->explicit_reconcile_only = false;

    column->carry_channel = StringName("aim");
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != stable, 1);
    column->carry_channel = StringName();

    set->masked = true;
    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != stable, 1);
    set->masked = false;

    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set), stable);
}

TEST_CASE(
    "[Networked][Predict][Report] PR7 a carry rule declared for a "
    "column is part of the config the report judges, because a rule is what "
    "decides whether the column is answered at all"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(slot >= 0);

    const Ref<NetwPropertySet> set = one_column_set();
    const int64_t before = pool->property_class_report_hash(slot, set);

    pool->set_carry(slot, StringName("position"), callable_mp_static(&nothing));
    REQUIRE(pool->has_carry_rule(slot, StringName("position")));

    NETW_CHECK_EQ(pool->property_class_report_hash(slot, set) != before, 1);
}

Dictionary witness_shaped() {
    Dictionary sample;
    sample[StringName("colliders")] = Array();
    sample[StringName("sleeping")] = false;
    sample[StringName("depth")] = 3;
    return sample;
}

Dictionary witness_misshapen() {
    Dictionary sample;
    sample[StringName("colliders")] = Array();
    sample[StringName("sleeping")] = 0;
    return sample;
}

Dictionary witness_freed_collider() {
    Node *ghost = memnew(Node);
    Array colliders;
    colliders.append(ghost);
    memdelete(ghost);
    Dictionary sample;
    sample[StringName("colliders")] = colliders;
    sample[StringName("sleeping")] = false;
    return sample;
}

TEST_CASE(
    "[Networked][Predict][Report] PR12 a witness in the documented "
    "shape is carried through whole, and one outside it answers empty, "
    "because a recovery must read an absent witness rather than a malformed "
    "one"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(slot >= 0);

    Ref<NetwPredictionHandle> handle;
    handle.instantiate();

    NETW_CHECK_EQ(pool->witness_sample(slot, handle).size(), 0);

    handle->set_witness_contacts(callable_mp_static(&witness_shaped));
    const Dictionary accepted = pool->witness_sample(slot, handle);
    NETW_CHECK_EQ(accepted.size(), 3);
    NETW_CHECK_EQ(int(accepted[StringName("depth")]), 3);
    NETW_CHECK_EQ(pool->invalid_witness_reported_of(slot), false);

    handle->set_witness_contacts(callable_mp_static(&witness_misshapen));
    NETW_CHECK_EQ(pool->witness_sample(slot, handle).size(), 0);
    NETW_CHECK_EQ(pool->invalid_witness_reported_of(slot), true);
}

TEST_CASE(
    "[Networked][Predict][Report] PR13 a collider freed since the "
    "game gathered it makes the whole sample unusable, because the pool "
    "cannot tell which contact it stood for"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(slot >= 0);

    Ref<NetwPredictionHandle> handle;
    handle.instantiate();
    handle->set_witness_contacts(callable_mp_static(&witness_freed_collider));

    NETW_CHECK_EQ(pool->witness_sample(slot, handle).size(), 0);
    NETW_CHECK_EQ(pool->invalid_witness_reported_of(slot), true);
}

Dictionary predictor_overlay(const Ref<NetwEntity> &, int64_t p_tick) {
    Dictionary out;
    out[StringName("steer")] = int(p_tick);
    return out;
}

Variant predictor_broken(const Ref<NetwEntity> &, int64_t) {
    return 7;
}

TEST_CASE(
    "[Networked][Predict][Report] PR14 a substituted transition "
    "coasts unless a predictor answers, and a predictor that answers with "
    "anything but a Dictionary leaves the coast baseline standing"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = pool->slot_register(seated);
    REQUIRE(slot >= 0);

    const Dictionary coasted = pool->predicted_command(slot, seated, 4);
    NETW_CHECK_EQ(coasted.has(StringName("steer")), false);
    NETW_CHECK_EQ(pool->invalid_command_predictor_reported_of(slot), false);

    const int64_t subject = seated->get_instance_id();
    REQUIRE(pool->note_simulated_by(
        slot,
        subject,
        callable_mp_static(&predictor_overlay)
    ));
    const Dictionary answered = pool->predicted_command(slot, seated, 4);
    NETW_CHECK_EQ(int(answered[StringName("steer")]), 4);
    NETW_CHECK_EQ(pool->invalid_command_predictor_reported_of(slot), false);

    REQUIRE(pool->clear_simulated_by(slot, subject));
    REQUIRE(pool->note_simulated_by(
        slot,
        subject,
        callable_mp_static(&predictor_broken)
    ));
    const Dictionary refused = pool->predicted_command(slot, seated, 4);
    NETW_CHECK_EQ(refused.has(StringName("steer")), false);
    NETW_CHECK_EQ(pool->invalid_command_predictor_reported_of(slot), true);
}

namespace {

Ref<netw::NetwPredictJudgement> abstaining_seam(
    int64_t,
    int64_t,
    const Dictionary &,
    const Dictionary &,
    const Dictionary &
) {
    return Ref<netw::NetwPredictJudgement>();
}

Dictionary at_position(double p_x) {
    Dictionary out;
    out[StringName("position")] = Vector2(p_x, 0.0);
    return out;
}

int64_t judging_slot(LoopbackRig &p_rig, const Ref<NetwEntity> &p_seated) {
    NetwPredictionEngine *const pool = p_rig.server()->get_prediction_engine();
    const int64_t slot = pool->slot_register(p_seated);
    REQUIRE(slot >= 0);
    pool->bind_property_sets(
        slot,
        NetwPropertySetBinding::create(
            pose_and_momentum(),
            p_seated->get_owner()
        ),
        Ref<NetwPropertySetBinding>()
    );
    pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    return slot;
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Report] PR20 a stock decision is a value, so a "
    "tick that installs no seam mints no record to carry an answer the "
    "engine unwraps and drops on the next line"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());
    NetwPredictionEngine *const pool = rig.server()->get_prediction_engine();
    const int64_t slot = judging_slot(rig, seated);

    const Dictionary predicted = at_position(0.0);
    const Dictionary payload = at_position(5.0);
    pool->judge_state(slot, 4, 0, 0, predicted, payload, Callable());
    pool->recover_through(slot, Dictionary(), predicted, Callable());

    const int64_t before = netw::prediction_core::records_minted();
    for (int at = 0; at < 8; ++at) {
        pool->judge_state(slot, 5 + at, 0, 0, predicted, payload, Callable());
        pool->recover_through(slot, Dictionary(), predicted, Callable());
    }
    NETW_CHECK_EQ(netw::prediction_core::records_minted(), before);
}

TEST_CASE(
    "[Networked][Predict][Report] PR21 a seam that answers nothing is "
    "refused rather than read as agreement, so the stock judgement stands "
    "and the misuse is reported on the entity's own route"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    const Ref<NetwEntity> seated = seat_player(rig, arena);
    REQUIRE(seated.is_valid());

    NetwMultiplayer *core = rig.server();
    core->event_arm(true);
    PackedInt64Array events;
    events.push_back(netw::EventPlane::SEAM_MISUSE);
    core->event_watch(
        events,
        Dictionary(),
        Dictionary(),
        Callable(),
        Dictionary()
    );
    NetwPredictionEngine *const pool = core->get_prediction_engine();
    const int64_t slot = judging_slot(rig, seated);
    const int64_t route = core->liveness_route_of(seated.ptr());

    const Dictionary predicted = at_position(0.0);
    const Dictionary payload = at_position(5.0);
    Dictionary sink;
    const netw::Judgement stock = netw::prediction_core::judge(
        0,
        int(NetwPredict::EXACT_VERDICT_UNJUDGED),
        predicted,
        payload,
        pool->wiring_snapshot(slot),
        sink
    );
    REQUIRE(stock.corrected);

    const Dictionary judged = pool->judge_state(
        slot,
        4,
        0,
        0,
        predicted,
        payload,
        callable_mp_static(&abstaining_seam)
    );
    NETW_CHECK_EQ(
        bool(judged[StringName("corrected")]) ? 1 : 0,
        stock.corrected ? 1 : 0
    );
    CHECK(double(judged[StringName("divergence")]) == stock.divergence);

    const Array refusals
        = rows_of(core->event_ring(route), netw::EventPlane::SEAM_MISUSE);
    NETW_CHECK_EQ(int64_t(refusals.size()), int64_t(1));
    const Dictionary refusal = refusals[0];
    REQUIRE(!refusal.is_empty());
    const Dictionary detail = refusal[netw::event_key::detail()];
    CHECK(String(detail[StringName("seam")]) == String("_predict_evaluate"));
    CHECK(String(detail[StringName("reason")]) == String("null"));
}

predict::FieldDecl momentum_field(const char *p_key) {
    predict::FieldDecl decl;
    decl.key = StringName(p_key);
    decl.type = int(Variant::VECTOR2);
    decl.teleport_only = true;
    return decl;
}

Dictionary reachability_of(NetwPredictionEngine &p_pool, int64_t p_slot) {
    return p_pool.reachability_report(
        p_slot,
        StringName("racer"),
        0.05,
        1.0,
        int(NetwPredict::BREACH_RESPONSE_DEMOTE),
        StringName("code"),
        0,
        0,
        false
    );
}

PackedStringArray fields_found(const Dictionary &p_report, const char *p_code) {
    PackedStringArray named;
    const Array findings = p_report[StringName("findings")];
    for (int at = 0; at < findings.size(); ++at) {
        const Dictionary finding = findings[at];
        if (String(finding[StringName("code")]) != String(p_code)) {
            continue;
        }
        const PackedStringArray listed = finding[StringName("fields")];
        for (int name = 0; name < listed.size(); ++name) {
            named.push_back(listed[name]);
        }
    }
    return named;
}

TEST_CASE(
    "[Networked][Predict][Report] PR26 a causal field a comparison may only "
    "teleport, with no forward model to advance the one write that reaches "
    "it, offers the full closure as its ONLY operator and is named "
    "unrepairable, because a correction that can only teleport such a field "
    "repairs it by discarding the whole prediction or not at all"
) {
    LocalVector<predict::FieldDecl> declaration;
    declaration.push_back(momentum_field("linear_velocity"));

    NetwPredictionEngine held_pool;
    const int64_t slot = held_pool.open(declaration);
    REQUIRE(slot >= 0);

    const Dictionary report = reachability_of(held_pool, slot);
    const Dictionary fields = report[StringName("fields")];
    const Dictionary judged = fields[StringName("linear_velocity")];
    REQUIRE(!judged.is_empty());

    const PackedStringArray operators = judged[StringName("operators")];
    NETW_CHECK_EQ(int64_t(operators.size()), int64_t(1));
    CHECK(String(operators[0]) == String("full_closure"));

    const Dictionary model = judged[StringName("forward_model")];
    CHECK(String(model[StringName("kind")]) == String("none"));
    NETW_CHECK_EQ(bool(model[StringName("live")]) ? 1 : 0, 0);
    NETW_CHECK_EQ(bool(judged[StringName("triggers")]) ? 1 : 0, 1);

    const PackedStringArray unrepairable = fields_found(report, "unrepairable");
    NETW_CHECK_EQ(int64_t(unrepairable.size()), int64_t(1));
    CHECK(String(unrepairable[0]) == String("linear_velocity"));
}

TEST_CASE(
    "[Networked][Predict][Report] PR27 a teleport-only field whose carry "
    "channel the same set replicates is repairable and says so, which is the "
    "half PR26 must not take with it"
) {
    LocalVector<predict::FieldDecl> declaration;
    predict::FieldDecl pose = momentum_field("position");
    pose.carry_channel = StringName("velocity");
    declaration.push_back(pose);
    declaration.push_back(momentum_field("velocity"));

    NetwPredictionEngine held_pool;
    const int64_t slot = held_pool.open(declaration);
    REQUIRE(slot >= 0);

    const Dictionary report = reachability_of(held_pool, slot);
    const Dictionary fields = report[StringName("fields")];
    const Dictionary judged = fields[StringName("position")];
    REQUIRE(!judged.is_empty());

    const Dictionary model = judged[StringName("forward_model")];
    CHECK(String(model[StringName("kind")]) == String("channel"));
    NETW_CHECK_EQ(bool(model[StringName("live")]) ? 1 : 0, 1);

    const PackedStringArray operators = judged[StringName("operators")];
    CHECK(operators.has(String("carry_advance")));

    const PackedStringArray unrepairable = fields_found(report, "unrepairable");
    NETW_CHECK_EQ(unrepairable.has(String("position")) ? 1 : 0, 0);
}

} // namespace TestNetwPredictReportLaws

#endif

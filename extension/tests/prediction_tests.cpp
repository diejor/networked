#include "support/netw_test.h"

#include <cmath>

#include "netw/prediction_core.hpp"

namespace TestPredictionCore {

using namespace godot;
using netw::Attribution;
using netw::NetwPredictFold;
using netw::NetwPredictJudgement;
using netw::PredictionVerdict;
using netw::StateFamily;

namespace prediction_core = netw::prediction_core;

constexpr double TAU = 6.2831853071795864769252867666;

constexpr int DOMAIN_IN = int(netw::Domain::IN_DOMAIN);
constexpr int DOMAIN_OUT = int(netw::Domain::OUT_OF_DOMAIN);

constexpr int VERDICT_UNJUDGED = int(netw::ExactVerdict::UNJUDGED);
constexpr int VERDICT_EQUAL = int(netw::ExactVerdict::EQUAL);
constexpr int VERDICT_UNEQUAL = int(netw::ExactVerdict::UNEQUAL);

constexpr int DRIVE_FRESH = int(netw::DriveKind::FRESH);
constexpr int DRIVE_REPEAT = int(netw::DriveKind::REPEAT);

constexpr int CONSUME_REPLAY = int(netw::ConsumeAction::REPLAY);
constexpr int CONSUME_HOLD = int(netw::ConsumeAction::HOLD);
constexpr int CONSUME_STARVED = int(netw::ConsumeAction::STARVED);

Dictionary one_field(const StringName &p_key, const Variant &p_value) {
    Dictionary row;
    row[p_key] = p_value;
    return row;
}

int joint_cell(bool p_authored, bool p_relayed, bool p_predictor_valid) {
    return prediction_core::calculate_joint_cell(
        p_authored,
        p_relayed,
        p_predictor_valid
    );
}

Dictionary wiring_with(const StringName &p_key, const Variant &p_value) {
    Dictionary wiring;
    wiring[p_key] = p_value;
    return wiring;
}

TEST_CASE(
    "[Networked][Predict] A declared undisturbed transition is in domain"
) {
    NETW_CHECK_EQ(prediction_core::domain_of(true, false, 10, -1), DOMAIN_IN);
}

TEST_CASE(
    "[Networked][Predict] An undeclared entity is out of domain whatever its "
    "label"
) {
    NETW_CHECK_EQ(prediction_core::domain_of(false, false, 0, -1), DOMAIN_OUT);
    NETW_CHECK_EQ(
        prediction_core::domain_of(false, false, 999, -1),
        DOMAIN_OUT
    );
}

TEST_CASE(
    "[Networked][Predict] An approximate island is out of domain whatever its "
    "label"
) {
    NETW_CHECK_EQ(prediction_core::domain_of(true, true, 999, -1), DOMAIN_OUT);
    NETW_CHECK_EQ(prediction_core::domain_of(true, true, 999, 0), DOMAIN_OUT);
}

TEST_CASE(
    "[Networked][Predict] A label short of the window's end is out of domain, "
    "and the end itself is in"
) {
    const int64_t until = 6;

    NETW_CHECK_EQ(
        prediction_core::domain_of(true, false, until - 1, until),
        DOMAIN_OUT
    );
    NETW_CHECK_EQ(
        prediction_core::domain_of(true, false, until, until),
        DOMAIN_IN
    );
    NETW_CHECK_EQ(
        prediction_core::domain_of(true, false, 0, until),
        DOMAIN_OUT
    );
    NETW_CHECK_EQ(
        prediction_core::domain_of(true, false, until + 1, until),
        DOMAIN_IN
    );
}

TEST_CASE(
    "[Networked][Predict] A negative window admits every label of a declared "
    "entity"
) {
    NETW_CHECK_EQ(prediction_core::domain_of(true, false, 0, -1), DOMAIN_IN);
}

TEST_CASE(
    "[Networked][Predict] A newer input label drives fresh, and a stale one "
    "repeats"
) {
    const Ref<NetwPredictFold> fresh = prediction_core::predict_fold(7, 4, 99);
    NETW_CHECK_EQ(fresh->label(), 7);
    CHECK(fresh->fresh());
    NETW_CHECK_EQ(fresh->kind(), DRIVE_FRESH);

    const Ref<NetwPredictFold> repeat = prediction_core::predict_fold(4, 4, 99);
    NETW_CHECK_EQ(repeat->label(), 4);
    CHECK_FALSE(repeat->fresh());
    NETW_CHECK_EQ(repeat->kind(), DRIVE_REPEAT);
}

TEST_CASE(
    "[Networked][Predict] A pass with no input yet labels from its frame"
) {
    const Ref<NetwPredictFold> fold = prediction_core::predict_fold(-1, -1, 99);

    NETW_CHECK_EQ(fold->label(), 99);
    CHECK_FALSE(fold->fresh());
}

TEST_CASE("[Networked][Predict] A fold naming no drive kind mints nothing") {
    CHECK(
        NetwPredictFold::of(
            7,
            true,
            static_cast<netw::NetwPredict::DriveKind>(DRIVE_FRESH)
        )
            .is_valid()
    );
    CHECK(
        NetwPredictFold::of(
            7,
            true,
            static_cast<netw::NetwPredict::DriveKind>(99)
        )
            .is_null()
    );
    CHECK(
        NetwPredictFold::of(
            7,
            true,
            static_cast<netw::NetwPredict::DriveKind>(-1)
        )
            .is_null()
    );
}

TEST_CASE(
    "[Networked][Predict] Authority replays only past the standing buffer"
) {
    NETW_CHECK_EQ(prediction_core::consume_action(3, 2), CONSUME_REPLAY);
    NETW_CHECK_EQ(prediction_core::consume_action(2, 2), CONSUME_HOLD);
    NETW_CHECK_EQ(prediction_core::consume_action(0, 0), CONSUME_STARVED);
}

TEST_CASE(
    "[Networked][Predict] Provenance takes the highest cell its evidence "
    "supports"
) {
    NETW_CHECK_EQ(joint_cell(true, false, false), 3);
    NETW_CHECK_EQ(joint_cell(false, true, false), 2);
    NETW_CHECK_EQ(joint_cell(false, false, true), 1);
    NETW_CHECK_EQ(joint_cell(false, false, false), 0);

    NETW_CHECK_EQ(joint_cell(true, true, true), 3);
    NETW_CHECK_EQ(joint_cell(false, true, true), 2);
}

TEST_CASE("[Networked][Predict] The joint floor is the lowest base offered") {
    Dictionary bases;
    bases[StringName("A")] = (int64_t)10;
    bases[StringName("B")] = (int64_t)5;

    const Dictionary res = prediction_core::calculate_joint_floor(
        bases,
        Dictionary(),
        -1,
        0,
        20
    );

    NETW_CHECK_EQ((int64_t)res[StringName("floor")], 5);
    CHECK_FALSE(bool(res[StringName("heal")]));
}

TEST_CASE(
    "[Networked][Predict] A relay floor and an epoch floor pull the joint "
    "floor down like a base"
) {
    Dictionary bases;
    bases[StringName("A")] = (int64_t)10;

    const Dictionary relayed = prediction_core::calculate_joint_floor(
        bases,
        one_field(StringName("A"), (int64_t)5),
        -1,
        0,
        20
    );
    NETW_CHECK_EQ((int64_t)relayed[StringName("floor")], 5);

    const Dictionary epoched
        = prediction_core::calculate_joint_floor(bases, Dictionary(), 3, 0, 20);
    NETW_CHECK_EQ((int64_t)epoched[StringName("floor")], 3);
}

TEST_CASE(
    "[Networked][Predict] With nothing offered the joint floor is the present"
) {
    const Dictionary res = prediction_core::calculate_joint_floor(
        Dictionary(),
        Dictionary(),
        -1,
        0,
        20
    );

    NETW_CHECK_EQ((int64_t)res[StringName("floor")], 20);
    CHECK_FALSE(bool(res[StringName("heal")]));
}

TEST_CASE(
    "[Networked][Predict] A negative base is an absent one and cannot pull the "
    "floor down"
) {
    const Dictionary res = prediction_core::calculate_joint_floor(
        one_field(StringName("A"), (int64_t)-4),
        Dictionary(),
        -1,
        0,
        20
    );

    NETW_CHECK_EQ((int64_t)res[StringName("floor")], 20);
}

TEST_CASE(
    "[Networked][Predict] A floor under the history floor heals to the present"
) {
    const Dictionary res = prediction_core::calculate_joint_floor(
        one_field(StringName("A"), (int64_t)2),
        Dictionary(),
        -1,
        8,
        20
    );

    CHECK(bool(res[StringName("heal")]));
    NETW_CHECK_EQ((int64_t)res[StringName("floor")], 20);
}

TEST_CASE(
    "[Networked][Predict] An empty prediction diverges without bound and "
    "corrects"
) {
    Dictionary sink;
    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_IN,
        VERDICT_UNJUDGED,
        Dictionary(),
        one_field(StringName("pos"), Vector2(1.0, 0.0)),
        Dictionary(),
        sink
    );

    CHECK(res->corrected());
    const bool unbounded = std::isinf(res->divergence());
    CHECK(unbounded);
}

TEST_CASE("[Networked][Predict] A prediction matching its payload is clean") {
    Dictionary sink;
    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        one_field(StringName("pos"), Vector2(1.0, 1.0)),
        one_field(StringName("pos"), Vector2(1.0, 1.0)),
        Dictionary(),
        sink
    );

    CHECK_FALSE(res->corrected());
    NETW_CHECK_CLOSE(res->divergence(), 0.0, 1e-9);
}

TEST_CASE(
    "[Networked][Predict] Out of domain, a field is judged against epsilon"
) {
    const Dictionary wiring = wiring_with(StringName("epsilon"), 0.5);
    const Dictionary payload = one_field(StringName("pos"), Vector2(0.0, 0.0));
    Dictionary sink;

    const Ref<NetwPredictJudgement> within = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        one_field(StringName("pos"), Vector2(0.25, 0.0)),
        payload,
        wiring,
        sink
    );
    CHECK_FALSE(within->corrected());

    const Ref<NetwPredictJudgement> past = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        one_field(StringName("pos"), Vector2(0.75, 0.0)),
        payload,
        wiring,
        sink
    );
    CHECK(past->corrected());
    NETW_CHECK_CLOSE(past->divergence(), 0.75, 1e-9);
}

TEST_CASE(
    "[Networked][Predict] An epsilon override widens the tolerance of its own "
    "field alone"
) {
    Dictionary wiring;
    wiring[StringName("epsilon")] = 0.1;
    wiring[StringName("epsilon_overrides")]
        = one_field(StringName("loose"), 5.0);

    Dictionary predicted;
    predicted[StringName("loose")] = 0.0;
    predicted[StringName("tight")] = 0.0;

    Dictionary payload;
    payload[StringName("loose")] = 4.0;
    payload[StringName("tight")] = 0.0;
    Dictionary sink;

    const Ref<NetwPredictJudgement> widened = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        predicted,
        payload,
        wiring,
        sink
    );
    CHECK_FALSE(widened->corrected());

    payload[StringName("tight")] = 4.0;
    const Ref<NetwPredictJudgement> neighbour = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        predicted,
        payload,
        wiring,
        sink
    );
    CHECK(neighbour->corrected());
}

TEST_CASE("[Networked][Predict] An excluded field cannot force a correction") {
    Dictionary wiring;
    wiring[StringName("epsilon")] = 0.1;
    wiring[StringName("vote_excludes")]
        = one_field(StringName("cosmetic"), true);

    Dictionary predicted;
    predicted[StringName("cosmetic")] = 0.0;
    Dictionary payload;
    payload[StringName("cosmetic")] = 90.0;
    Dictionary sink;

    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        predicted,
        payload,
        wiring,
        sink
    );

    CHECK_FALSE(res->corrected());
    NETW_CHECK_CLOSE(res->divergence(), 90.0, 1e-9);
}

TEST_CASE(
    "[Networked][Predict] A payload field the prediction never carried corrects"
) {
    Dictionary sink;
    Dictionary payload;
    payload[StringName("pos")] = 0.0;
    payload[StringName("unpredicted")] = 1.0;

    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        one_field(StringName("pos"), 0.0),
        payload,
        wiring_with(StringName("epsilon"), 1000.0),
        sink
    );

    CHECK(res->corrected());
}

TEST_CASE(
    "[Networked][Predict] In domain a judged transition decides, and epsilon "
    "is never consulted"
) {
    const Dictionary wiring = wiring_with(StringName("epsilon"), 1000.0);
    const Dictionary predicted = one_field(StringName("pos"), 0.0);
    const Dictionary payload = one_field(StringName("pos"), 1.0);
    Dictionary sink;

    const Ref<NetwPredictJudgement> unequal = prediction_core::evaluate(
        DOMAIN_IN,
        VERDICT_UNEQUAL,
        predicted,
        predicted,
        wiring,
        sink
    );
    CHECK(unequal->corrected());

    const Ref<NetwPredictJudgement> equal = prediction_core::evaluate(
        DOMAIN_IN,
        VERDICT_EQUAL,
        predicted,
        payload,
        wiring,
        sink
    );
    CHECK_FALSE(equal->corrected());
}

TEST_CASE(
    "[Networked][Predict] Out of domain the verdict is ignored and epsilon "
    "decides"
) {
    Dictionary sink;
    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNEQUAL,
        one_field(StringName("pos"), 0.0),
        one_field(StringName("pos"), 0.0),
        wiring_with(StringName("epsilon"), 0.1),
        sink
    );

    CHECK_FALSE(res->corrected());
}

TEST_CASE(
    "[Networked][Predict] An angle field compares the short way around the "
    "circle"
) {
    const Dictionary wiring = wiring_with(
        StringName("angle_fields"),
        one_field(StringName("heading"), true)
    );
    Dictionary sink;

    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        one_field(StringName("heading"), 0.1),
        one_field(StringName("heading"), TAU - 0.1),
        wiring,
        sink
    );

    NETW_CHECK_CLOSE(res->divergence(), 0.2, 1e-6);
}

TEST_CASE(
    "[Networked][Predict] The field sink records one error per payload field"
) {
    Dictionary predicted;
    predicted[StringName("a")] = 0.0;
    predicted[StringName("b")] = 0.0;

    Dictionary payload;
    payload[StringName("a")] = 3.0;
    payload[StringName("b")] = 1.0;

    Dictionary sink;
    const Ref<NetwPredictJudgement> res = prediction_core::evaluate(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        predicted,
        payload,
        Dictionary(),
        sink
    );

    NETW_CHECK_EQ(sink.size(), 2);
    NETW_CHECK_CLOSE(double(sink[StringName("a")]), 3.0, 1e-9);
    NETW_CHECK_CLOSE(double(sink[StringName("b")]), 1.0, 1e-9);

    NETW_CHECK_CLOSE(res->divergence(), 3.0, 1e-9);
}

TEST_CASE(
    "[Networked][Predict] The struct verdict carries the divergence the "
    "dictionary reported"
) {
    const PredictionVerdict pv = prediction_core::evaluate_struct_verdict(
        DOMAIN_OUT,
        VERDICT_UNJUDGED,
        one_field(StringName("pos"), Vector2(0.0, 0.0)),
        one_field(StringName("pos"), Vector2(3.0, 4.0)),
        wiring_with(StringName("epsilon"), 0.5)
    );

    CHECK(pv.corrected);
    NETW_CHECK_CLOSE(pv.max_error, 5.0, 1e-9);
    NETW_CHECK_EQ(pv.domain, DOMAIN_OUT);
    NETW_CHECK_EQ(pv.verdict, VERDICT_UNJUDGED);
}

TEST_CASE(
    "[Networked][Predict] A run that will not shrink escalates on the third"
) {
    Dictionary state = prediction_core::escalation_after(0, 0, -1.0, 1.0, 1);
    NETW_CHECK_EQ((int)state[StringName("streak")], 1);
    CHECK_FALSE(bool(state[StringName("escalate")]));

    state = prediction_core::escalation_after(1, 1, 1.0, 1.0, 1);
    NETW_CHECK_EQ((int)state[StringName("streak")], 2);
    CHECK_FALSE(bool(state[StringName("escalate")]));

    state = prediction_core::escalation_after(2, 1, 1.0, 2.0, 1);
    CHECK(bool(state[StringName("escalate")]));
    NETW_CHECK_EQ((int)state[StringName("sign")], 1);
}

TEST_CASE("[Networked][Predict] Escalating closes the run it escalated on") {
    const Dictionary escalated
        = prediction_core::escalation_after(2, 1, 1.0, 2.0, 1);

    CHECK(bool(escalated[StringName("escalate")]));
    NETW_CHECK_EQ((int)escalated[StringName("streak")], 0);
}

TEST_CASE(
    "[Networked][Predict] A sign flip escalates at once, whatever the streak"
) {
    const Dictionary state
        = prediction_core::escalation_after(0, 1, 1.0, 1.0, -1);

    CHECK(bool(state[StringName("escalate")]));
    NETW_CHECK_EQ((int)state[StringName("sign")], -1);
}

TEST_CASE(
    "[Networked][Predict] An unsigned sample cannot flip, and still counts "
    "toward the run"
) {
    const Dictionary state
        = prediction_core::escalation_after(1, 1, 1.0, 9.0, 0);

    NETW_CHECK_EQ((int)state[StringName("streak")], 2);
    NETW_CHECK_EQ((int)state[StringName("sign")], 0);
    CHECK_FALSE(bool(state[StringName("escalate")]));
}

TEST_CASE(
    "[Networked][Predict] A shrinking divergence closes the run and outranks "
    "a flip"
) {
    const Dictionary steady
        = prediction_core::escalation_after(2, 1, 5.0, 1.0, 1);
    NETW_CHECK_EQ((int)steady[StringName("streak")], 0);
    NETW_CHECK_EQ((int)steady[StringName("sign")], 0);
    CHECK_FALSE(bool(steady[StringName("escalate")]));

    const Dictionary flipped
        = prediction_core::escalation_after(2, 1, 5.0, 1.0, -1);
    CHECK_FALSE(bool(flipped[StringName("escalate")]));
    NETW_CHECK_EQ((int)flipped[StringName("sign")], 0);
}

TEST_CASE(
    "[Networked][Predict] The meter reads a tolerance as a zero region and "
    "counts whole tolerances past it"
) {
    Dictionary sink;
    Dictionary tolerances;
    sink[StringName("position")] = 0.5;
    tolerances[StringName("position")] = 0.5;
    NETW_CHECK_EQ(prediction_core::measure(sink, tolerances), 0);

    sink[StringName("position")] = 1.0;
    NETW_CHECK_EQ(prediction_core::measure(sink, tolerances), 1);

    Dictionary exact_sink;
    Dictionary exact_tolerances;
    exact_sink[StringName("raw")] = 0.000001;
    exact_tolerances[StringName("raw")] = 0.0;
    NETW_CHECK_EQ(prediction_core::measure(exact_sink, exact_tolerances), 1);
}

TEST_CASE(
    "[Networked][Predict] A field the sink never measured saturates the meter"
) {
    Dictionary tolerances;
    tolerances[StringName("position")] = 0.5;
    NETW_CHECK_EQ(
        prediction_core::measure(Dictionary(), tolerances),
        0x7FFFFFFF
    );
}

TEST_CASE(
    "[Networked][Predict] Attribution charges the first unequal boundary in "
    "causal order"
) {
    const int witness = 1;
    NETW_CHECK_EQ(
        prediction_core::attribute(
            false,
            false,
            false,
            true,
            true,
            true,
            witness,
            witness,
            true
        ),
        int(Attribution::PRE_STATE)
    );
    NETW_CHECK_EQ(
        prediction_core::attribute(
            true,
            false,
            false,
            true,
            true,
            true,
            witness,
            witness,
            true
        ),
        int(Attribution::COMMAND)
    );
    NETW_CHECK_EQ(
        prediction_core::attribute(
            true,
            true,
            false,
            true,
            true,
            true,
            witness,
            witness,
            true
        ),
        int(Attribution::ENVIRONMENT)
    );
    NETW_CHECK_EQ(
        prediction_core::attribute(
            true,
            true,
            true,
            true,
            true,
            true,
            witness,
            witness,
            true
        ),
        int(Attribution::CLOSURE)
    );
}

TEST_CASE(
    "[Networked][Predict] Missing evidence is unknown rather than agreement"
) {
    const int witness = 1;
    NETW_CHECK_EQ(
        prediction_core::attribute(
            true,
            true,
            true,
            true,
            true,
            true,
            witness,
            witness,
            false
        ),
        int(Attribution::UNKNOWN)
    );

    NETW_CHECK_EQ(
        prediction_core::
            attribute(true, true, true, true, true, false, witness, 0, true),
        int(Attribution::UNKNOWN)
    );
}

TEST_CASE("[Networked][Predict] Fingerprints fold their keys in text order") {
    Dictionary forward;
    forward[StringName("alpha")] = 1;
    forward[StringName("zulu")] = 2;
    Dictionary reversed;
    reversed[StringName("zulu")] = 2;
    reversed[StringName("alpha")] = 1;

    NETW_CHECK_EQ(
        prediction_core::fact_fingerprint(forward),
        prediction_core::fact_fingerprint(reversed)
    );
    NETW_CHECK_EQ(
        prediction_core::raw_state_fingerprint(forward),
        prediction_core::raw_state_fingerprint(reversed)
    );

    Dictionary moved;
    moved[StringName("alpha")] = 1;
    moved[StringName("zulu")] = 3;
    CHECK(
        prediction_core::raw_state_fingerprint(forward)
        != prediction_core::raw_state_fingerprint(moved)
    );
}

TEST_CASE(
    "[Networked][Predict] The compared scope keeps the causal fields, and no "
    "declared scope keeps them all"
) {
    Dictionary payload;
    payload[StringName("position")] = 1;
    payload[StringName("tint")] = 2;
    Dictionary causal;
    causal[StringName("position")] = true;

    const Dictionary scoped = prediction_core::compared_state(payload, causal);
    NETW_CHECK_EQ(scoped.size(), 1);
    CHECK(scoped.has(StringName("position")));
    CHECK_FALSE(scoped.has(StringName("tint")));

    const Dictionary whole
        = prediction_core::compared_state(payload, Dictionary());
    NETW_CHECK_EQ(whole.size(), payload.size());

    Dictionary absent;
    absent[StringName("never_declared")] = true;
    NETW_CHECK_EQ(prediction_core::compared_state(payload, absent).size(), 0);
}

TEST_CASE("[Networked][Predict] Contact counts bucket at four and above") {
    NETW_CHECK_EQ(prediction_core::contact_count_bucket(0), 0);
    NETW_CHECK_EQ(prediction_core::contact_count_bucket(3), 3);
    NETW_CHECK_EQ(prediction_core::contact_count_bucket(4), 4);
    NETW_CHECK_EQ(prediction_core::contact_count_bucket(99), 4);
}

TEST_CASE(
    "[Networked][Predict] The family search returns the first differing "
    "family and needs all three"
) {
    PackedInt32Array local;
    local.push_back(1);
    local.push_back(2);
    local.push_back(3);

    PackedInt32Array same = local;
    NETW_CHECK_EQ(
        prediction_core::differing_family(local, same),
        int(StateFamily::NONE)
    );

    PackedInt32Array momentum = local;
    momentum.set(1, 9);
    NETW_CHECK_EQ(
        prediction_core::differing_family(local, momentum),
        int(StateFamily::MOMENTUM)
    );

    PackedInt32Array short_peer;
    short_peer.push_back(9);
    NETW_CHECK_EQ(
        prediction_core::differing_family(local, short_peer),
        int(StateFamily::NONE)
    );
}

TEST_CASE(
    "[Networked][Predict] A window covers the transition that opened it and "
    "merges rather than restarting"
) {
    NETW_CHECK_EQ(prediction_core::window_after(10, 2, -1), (int64_t)13);

    NETW_CHECK_EQ(prediction_core::window_after(10, -5, -1), (int64_t)11);

    const int64_t wide = prediction_core::window_after(10, 20, -1);
    NETW_CHECK_EQ(prediction_core::window_after(11, 1, wide), wide);
}

TEST_CASE(
    "[Networked][Predict] The environment digest folds the epoch and ignores "
    "declaration order"
) {
    Dictionary forward;
    forward[StringName("a")] = 1.0;
    forward[StringName("b")] = 2.0;
    Dictionary reversed;
    reversed[StringName("b")] = 2.0;
    reversed[StringName("a")] = 1.0;

    NETW_CHECK_EQ(
        prediction_core::environment_digest(7, forward),
        prediction_core::environment_digest(7, reversed)
    );
    CHECK(
        prediction_core::environment_digest(7, Dictionary())
        != prediction_core::environment_digest(8, Dictionary())
    );
    CHECK(
        prediction_core::environment_digest(7, forward)
        != prediction_core::environment_digest(7, Dictionary())
    );
}

TEST_CASE(
    "[Networked][Predict] A direction names the dominant axis and its sign"
) {
    const Dictionary along = prediction_core::delta_direction(
        StringName("position"),
        Vector3(1.0, 0.0, -2.0)
    );
    CHECK(bool(String(along[StringName("key")]) == String("position:2")));
    NETW_CHECK_EQ((int)along[StringName("sign")], -1);

    const Dictionary none
        = prediction_core::delta_direction(StringName("flag"), true);
    NETW_CHECK_EQ((int)none[StringName("sign")], 0);
    CHECK(String(none[StringName("key")]).is_empty());
}

TEST_CASE(
    "[Networked][Predict] The projection guard costs a diverged channel only "
    "its own field"
) {
    Dictionary projection;
    projection[StringName("position")] = StringName("velocity");
    projection[StringName("heading")] = StringName("spin");

    Dictionary divergence;
    divergence[StringName("velocity")] = 10.0;
    divergence[StringName("spin")] = 0.0;

    const Dictionary guarded = prediction_core::guard_projection(
        projection,
        divergence,
        0.1,
        Dictionary(),
        8,
        4,
        1.0
    );
    CHECK_FALSE(guarded.has(StringName("position")));
    CHECK(guarded.has(StringName("heading")));
}

} // namespace TestPredictionCore

#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "support/carrier.h"

#include "godot/variant.hpp"

#include "godot/physics_body.hpp"

namespace TestNetwPredictOwnerPort {

#if defined(NETW_TIER_HOSTED)

using namespace godot;
using namespace netw;
using namespace netw::predict;
using netw_test::Carrier;

Ref<NetwPredictionEngine> pool() {
    Ref<NetwPredictionEngine> out;
    out.instantiate();
    return out;
}

Ref<NetwPredictDeclaration> fields(const PackedStringArray &p_keys) {
    Ref<NetwPredictDeclaration> out;
    out.instantiate();
    for (int at = 0; at < p_keys.size(); ++at) {
        out->append_field(
            StringName(p_keys[at]),
            int(PropertyClass::CAUSAL),
            StringName(),
            0.0,
            false,
            false,
            -1.0,
            -1.0,
            false
        );
    }
    return out;
}

PackedStringArray one(const char *p_key) {
    PackedStringArray out;
    out.push_back(p_key);
    return out;
}

Carrier *carrier() {
    Carrier *out = memnew(Carrier);
    out->define("speed", 0.0);
    out->define("throttle", 0.0);
    return out;
}

TEST_CASE("[Networked][Predict][Owner] a slot registered by entity is that "
          "entity's, and binding by entity reaches it") {
    const Ref<NetwPredictionEngine> engine = pool();
    Ref<RefCounted> seated;
    seated.instantiate();
    Ref<RefCounted> other;
    other.instantiate();
    Carrier *body = carrier();

    const int64_t slot = engine->slot_register(seated);
    CHECK(slot >= 0);
    NETW_CHECK_EQ(engine->slot_register(seated), slot);
    NETW_CHECK_EQ(engine->slot_of(seated), slot);
    NETW_CHECK_EQ(engine->slot_of(other), int64_t(-1));
    NETW_CHECK_EQ(engine->slot_registered(), int64_t(1));

    CHECK(engine->slot_bind_owner(seated, body));
    CHECK(engine->owner_bound(slot));
    CHECK_FALSE(engine->slot_bind_owner(other, body));

    engine->slot_unbind_owner(seated);
    CHECK_FALSE(engine->owner_bound(slot));

    engine->slot_unregister(seated);
    NETW_CHECK_EQ(engine->slot_of(seated), int64_t(-1));
    NETW_CHECK_EQ(engine->slot_registered(), int64_t(0));
    CHECK_FALSE(engine->is_open(slot));

    memdelete(body);
}

TEST_CASE("[Networked][Predict][Owner] closing a slot by number unseats its "
          "entity too, so the two doors cannot disagree") {
    const Ref<NetwPredictionEngine> engine = pool();
    Ref<RefCounted> seated;
    seated.instantiate();

    const int64_t slot = engine->slot_register(seated);
    engine->close(slot);

    NETW_CHECK_EQ(engine->slot_of(seated), int64_t(-1));
    NETW_CHECK_EQ(engine->slot_registered(), int64_t(0));
    CHECK(engine->slot_register(seated) >= 0);
}

TEST_CASE("[Networked][Predict][Owner] a bound owner is captured field by "
          "declared field") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    owner->set("speed", 4.5);

    CHECK(engine->bind_owner(slot, owner));
    CHECK(engine->owner_bound(slot));

    const Dictionary captured = engine->capture_state(slot);

    NETW_CHECK_EQ(int64_t(captured.size()), int64_t(1));
    NETW_CHECK_CLOSE(double(captured[StringName("speed")]), 4.5, 0.0);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] an apply writes the declared fields "
          "and no others") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);
    Dictionary payload;
    payload[StringName("speed")] = 9.0;
    payload[StringName("throttle")] = 9.0;

    CHECK(engine->apply_state(slot, payload));

    NETW_CHECK_CLOSE(double(owner->get("speed")), 9.0, 0.0);
    NETW_CHECK_CLOSE(double(owner->get("throttle")), 0.0, 0.0);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] a field the owner does not answer for "
          "is absent rather than null") {
    const Ref<NetwPredictionEngine> engine = pool();
    PackedStringArray keys;
    keys.push_back("speed");
    keys.push_back("nowhere");
    const int64_t slot = engine->open(fields(keys));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);

    const Dictionary captured = engine->capture_state(slot);

    NETW_CHECK_EQ(int64_t(captured.size()), int64_t(1));
    CHECK_FALSE(captured.has(StringName("nowhere")));
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] the input port reads the input "
          "declaration") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    engine->rewire(slot, fields(one("speed")), fields(one("throttle")));
    Carrier *owner = carrier();
    owner->set("speed", 1.0);
    owner->set("throttle", 2.0);
    engine->bind_owner(slot, owner);

    const Dictionary state = engine->capture_state(slot);
    const Dictionary input = engine->capture_input(slot);

    CHECK(state.has(StringName("speed")));
    CHECK_FALSE(state.has(StringName("throttle")));
    CHECK(input.has(StringName("throttle")));
    CHECK_FALSE(input.has(StringName("speed")));
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] an unbound slot performs no property "
          "I/O at all") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);
    engine->unbind_owner(slot);
    Dictionary payload;
    payload[StringName("speed")] = 9.0;

    CHECK_FALSE(engine->owner_bound(slot));
    CHECK(engine->capture_state(slot).is_empty());
    CHECK_FALSE(engine->apply_state(slot, payload));
    NETW_CHECK_CLOSE(double(owner->get("speed")), 0.0, 0.0);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] a freed owner unbinds its own slot and "
          "counts it once") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);
    memdelete(owner);

    const Dictionary captured = engine->capture_state(slot);

    CHECK(captured.is_empty());
    CHECK_FALSE(engine->owner_bound(slot));
    NETW_CHECK_EQ(
        engine->drive_stats(slot)[NetwPredictionEngine::STAT_OWNER_LOST],
        1
    );

    CHECK(engine->capture_state(slot).is_empty());
    NETW_CHECK_EQ(
        engine->drive_stats(slot)[NetwPredictionEngine::STAT_OWNER_LOST],
        1
    );
}

TEST_CASE("[Networked][Predict][Owner] a rewire under a live bind re-asks "
          "what the owner answers for") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    owner->set("throttle", 3.0);
    engine->bind_owner(slot, owner);
    CHECK_FALSE(engine->capture_state(slot).has(StringName("throttle")));

    PackedStringArray widened;
    widened.push_back("speed");
    widened.push_back("throttle");
    engine->rewire(slot, fields(widened));

    const Dictionary captured = engine->capture_state(slot);

    NETW_CHECK_EQ(int64_t(captured.size()), int64_t(2));
    NETW_CHECK_CLOSE(double(captured[StringName("throttle")]), 3.0, 0.0);
    memdelete(owner);
}

class ClosingStep final : public CallableCustom {
    Ref<NetwPredictionEngine> engine;
    int64_t slot = 0;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    ClosingStep(const Ref<NetwPredictionEngine> &p_engine, int64_t p_slot)
        : engine(p_engine), slot(p_slot),
          anchor(netw::gd::instance_id(p_engine.ptr())) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("ClosingStep");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &ClosingStep::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &ClosingStep::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        engine->close(slot);
        engine->unbind_owner(slot);
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

TEST_CASE("[Networked][Predict][Owner] a step reads the input it was handed "
          "and yields what it produced") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    engine->rewire(slot, fields(one("speed")), fields(one("throttle")));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);
    engine->set_simulate(slot, Callable(owner, "sum_into_speed"));
    Dictionary input;
    input[StringName("throttle")] = 2.0;

    const Dictionary produced = engine->run_step(slot, input, 0.5, 7, true);

    NETW_CHECK_CLOSE(double(owner->get("throttle")), 2.0, 0.0);
    NETW_CHECK_CLOSE(double(produced[StringName("speed")]), 2.0, 0.0);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] an explicit step wins over the one the "
          "bind adopted") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();

    CHECK_FALSE(engine->has_simulate(slot));

    engine->set_simulate(slot, Callable(owner, "sum_into_speed"));
    engine->bind_owner(slot, owner);

    CHECK(engine->has_simulate(slot));
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] the roster is frozen for the length of "
          "a pass") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);
    engine->set_simulate(slot, Callable(memnew(ClosingStep(engine, slot))));

    NETW_CHECK_EQ(engine->pass_depth(), 0);

    engine->run_step(slot, Dictionary(), 0.5, 7, true);

    NETW_CHECK_EQ(engine->pass_depth(), 0);
    CHECK(engine->is_open(slot));
    CHECK(engine->owner_bound(slot));
    NETW_CHECK_EQ(engine->mutations_refused_count(), 2);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] the roster reopens once the pass "
          "returns") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    engine->bind_owner(slot, owner);
    engine->run_step(slot, Dictionary(), 0.5, 7, true);

    engine->close(slot);

    CHECK_FALSE(engine->is_open(slot));
    NETW_CHECK_EQ(engine->mutations_refused_count(), 0);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] the step is told which run is the "
          "fresh one") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    owner->define("motion", Vector2(1.0, 0.0));
    owner->define("bombing", true);
    engine->bind_owner(slot, owner);
    engine->set_simulate(slot, Callable(owner, "_network_tick"));

    engine->run_step(slot, Dictionary(), 1.0 / 60.0, 7, true);
    engine->run_step(slot, Dictionary(), 1.0 / 60.0, 7, false);
    engine->run_step(slot, Dictionary(), 1.0 / 60.0, 7, false);

    NETW_CHECK_EQ(owner->fresh_effects(), 1);
    memdelete(owner);
}

TEST_CASE("[Networked][Predict][Owner] whether the owner solves is answered "
          "at the bind") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t kinematic = engine->open(fields(one("speed")));
    const int64_t solver = engine->open(fields(one("speed")));
    Carrier *plain = carrier();
    RigidBody2D *body = memnew(RigidBody2D);

    engine->bind_owner(kinematic, plain);
    engine->bind_owner(solver, body);

    CHECK_FALSE(engine->owner_solves(kinematic));
    CHECK(engine->owner_solves(solver));

    engine->unbind_owner(solver);

    CHECK_FALSE(engine->owner_solves(solver));
    memdelete(body);
    memdelete(plain);
}

TEST_CASE("[Networked][Predict][Owner] AUTO asks the bound owner, and an "
          "explicit correction asks nobody") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t kinematic = engine->open(fields(one("speed")));
    const int64_t solver = engine->open(fields(one("speed")));
    const int64_t unbound = engine->open(fields(one("speed")));
    Carrier *plain = carrier();
    RigidBody2D *body = memnew(RigidBody2D);

    engine->bind_owner(kinematic, plain);
    engine->bind_owner(solver, body);

    const int AUTO = int(CorrectionMode::AUTO);
    const int SNAP = int(CorrectionMode::SNAP);
    const int REPLAY = int(CorrectionMode::REPLAY);

    NETW_CHECK_EQ(engine->resolve_correction(solver, AUTO), SNAP);
    NETW_CHECK_EQ(engine->resolve_correction(kinematic, AUTO), REPLAY);
    NETW_CHECK_EQ(engine->resolve_correction(unbound, AUTO), REPLAY);
    NETW_CHECK_EQ(engine->resolve_correction(solver, REPLAY), REPLAY);
    NETW_CHECK_EQ(engine->resolve_correction(kinematic, SNAP), SNAP);

    memdelete(body);
    memdelete(plain);
}

struct CarryFixture {
    Ref<NetwPredictionEngine> engine;
    int64_t slot = 0;
    Carrier *owner = nullptr;
    Ref<netw::NetwTimeline> lane;

    CarryFixture(const char *p_rule) {
        engine = pool();
        slot = engine->open(fields(one("speed")));
        REQUIRE(engine->configure(
            slot,
            int(Schedule::FRAME),
            int(Role::PREDICT),
            int(CorrectionMode::SNAP),
            int(RestoreMode::EXACT),
            6,
            NetwPredictionEngine::ISLAND_NONE,
            true,
            false
        ));
        owner = carrier();
        engine->bind_owner(slot, owner);
        engine->set_carry(slot, StringName("speed"), Callable(owner, p_rule));

        lane = netw::NetwTimeline::create(64);
        engine->bind_timeline(slot, lane);
        Dictionary command;
        command[StringName("throttle")] = 2.0;
        for (int64_t label = 1; label <= 2; label += 1) {
            engine->tape_author(slot, label, true);
            lane->record_input(label, command);
        }
        const Ref<netw::NetwTimeline> entries = engine->entry_history(slot);
        for (int64_t at = 0; at <= 2; at += 1) {
            Dictionary state;
            state[StringName("speed")] = double(at) * 2.0;
            entries->record_state(at, state);
        }
    }

    ~CarryFixture() {
        memdelete(owner);
    }

    Ref<NetwPredictCarryAttempt> attempt() {
        return engine->attempt_carry(
            slot,
            StringName("speed"),
            10.0,
            -1,
            100.0,
            0.01
        );
    }
};

TEST_CASE("[Networked][Predict][Owner] a rule that reproduces the recorded "
          "past folds the acknowledged value across every entry past it") {
    CarryFixture fixture("carry_speed");

    const Ref<NetwPredictCarryAttempt> attempt = fixture.attempt();

    CHECK(attempt->evidence());
    CHECK(attempt->faithful());
    CHECK(attempt->same_type());
    CHECK(attempt->finite());
    CHECK(attempt->within_envelope());
    CHECK(attempt->pure());
    NETW_CHECK_CLOSE(double(attempt->value()), 14.0, 0.0001);
    NETW_CHECK_CLOSE(attempt->residual(), -1.0, 0.0);
}

TEST_CASE("[Networked][Predict][Owner] a rule that overstates every "
          "transition is caught by the replay and never folded") {
    CarryFixture fixture("carry_speed");
    fixture.owner->set_carry_gain(2.0);

    const Ref<NetwPredictCarryAttempt> attempt = fixture.attempt();

    CHECK(attempt->evidence());
    CHECK_FALSE(attempt->faithful());
    NETW_CHECK_CLOSE(attempt->residual(), 2.0, 0.0001);
    NETW_CHECK_CLOSE(attempt->tolerance(), 0.01, 0.0);
    NETW_CHECK_EQ(int(attempt->value().get_type()), int(Variant::NIL));
}

TEST_CASE("[Networked][Predict][Owner] a rule that writes the body it "
          "describes is caught by the purity bracket") {
    CarryFixture fixture("carry_speed_and_write");

    const Ref<NetwPredictCarryAttempt> attempt = fixture.attempt();

    CHECK(attempt->faithful());
    CHECK_FALSE(attempt->pure());
}

TEST_CASE("[Networked][Predict][Owner] an attempt with no transition to "
          "replay carries no evidence to judge") {
    CarryFixture fixture("carry_speed");

    const Ref<NetwPredictCarryAttempt> empty = fixture.engine->attempt_carry(
        fixture.slot,
        StringName("speed"),
        10.0,
        1,
        100.0,
        0.01
    );
    CHECK_FALSE(empty->evidence());

    CHECK_FALSE(fixture.engine
                    ->attempt_carry(
                        fixture.slot,
                        StringName("throttle"),
                        10.0,
                        -1,
                        100.0,
                        0.01
                    )
                    ->evidence());
}

class ReadsCarrier final : public CallableCustom {
    Carrier *source;
    StringName key;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    ReadsCarrier(Carrier *p_source, const char *p_key)
        : source(p_source), key(p_key) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("ReadsCarrier");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &ReadsCarrier::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &ReadsCarrier::before;
    }

    ObjectID get_object() const override {
        return netw::gd::instance_id(source);
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        r_return_value = source->get(key);
        netw::gd::call_ok(r_call_error);
    }
};

TEST_CASE("[Networked][Predict][Owner] a slot that declared no world fact "
          "digests to the zero an unwritten row already holds") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));

    NETW_CHECK_EQ(engine->sample_environment(slot, -1), int64_t(0));
    CHECK(engine->sensor_samples(slot).is_empty());

    CHECK(engine->sample_environment(slot, 4) != 0);

    NETW_CHECK_EQ(engine->sample_environment(slot + 9000, 4), int64_t(0));
}

TEST_CASE("[Networked][Predict][Owner] the digest moves with what the sensors "
          "answered, and the samples are what it was taken over") {
    const Ref<NetwPredictionEngine> engine = pool();
    const int64_t slot = engine->open(fields(one("speed")));
    Carrier *owner = carrier();
    engine->set_sensor(
        slot,
        StringName("ground"),
        Callable(memnew(ReadsCarrier(owner, "speed")))
    );

    owner->set("speed", 1.0);
    const int64_t first = engine->sample_environment(slot, -1);
    NETW_CHECK_CLOSE(
        double(engine->sensor_samples(slot)[StringName("ground")]),
        1.0,
        0.0
    );

    owner->set("speed", 2.0);
    NETW_CHECK_CLOSE(
        double(engine->sensor_samples(slot)[StringName("ground")]),
        1.0,
        0.0
    );

    CHECK(engine->sample_environment(slot, -1) != first);
    NETW_CHECK_CLOSE(
        double(engine->sensor_samples(slot)[StringName("ground")]),
        2.0,
        0.0
    );

    memdelete(owner);
}

#endif

} // namespace TestNetwPredictOwnerPort

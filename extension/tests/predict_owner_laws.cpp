// The owner port's laws: the one place a pool slot touches a game object.
//
// Tier 2a only, and deliberately not [Hosted]: the subject is a registered
// engine class with a dynamic property bag, which the module tier's own
// registration does not carry.

#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "support/carrier.h"

#include "godot/variant.hpp"

#include <godot_cpp/classes/rigid_body2d.hpp>

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

// A step that tries to close its own slot mid-pass, which is the roster
// mutation D7 refuses. Nothing else can reach the pool from inside its own
// pass.
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

#endif // NETW_TIER_HOSTED

} // namespace TestNetwPredictOwnerPort

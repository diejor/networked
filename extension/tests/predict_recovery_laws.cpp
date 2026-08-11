#include "support/netw_test.h"

#include <cmath>

#include "netw/predict/drive.hpp"

namespace TestNetwPredictRecoveryLaws {

using namespace netw;
using namespace netw::predict;

constexpr double PI = 3.1415926535897932384626433833;

StateRow scalar_state(double p_position, double p_velocity, bool p_latch) {
    StateRow out;
    out.resize(3);
    out.set(0, p_position);
    out.set(1, p_velocity);
    out.set(2, p_latch);
    return out;
}

LocalVector<double> errors(double p_position, double p_velocity) {
    LocalVector<double> out;
    out.resize(3);
    out[0] = p_position;
    out[1] = p_velocity;
    out[2] = -1.0;
    return out;
}

Wiring recovery_wiring(bool p_converge = true) {
    LocalVector<FieldDecl> fields;
    FieldDecl position;
    position.key = StringName("position");
    position.carry_channel = StringName("velocity");
    position.converge_stiffness = p_converge ? 0.5 : 0.0;
    position.epsilon_override = 1.0;
    position.teleport_at = 10.0;
    fields.push_back(position);

    FieldDecl velocity;
    velocity.key = StringName("velocity");
    velocity.teleport_only = true;
    fields.push_back(velocity);

    FieldDecl latch;
    latch.key = StringName("latch");
    fields.push_back(latch);
    return compile(fields);
}

Config snap_config(int p_restore = int(RestoreMode::EXACT)) {
    Config out;
    out.correction = int(CorrectionMode::SNAP);
    out.restore = p_restore;
    return out;
}

RecoveryRequest recovery_request() {
    RecoveryRequest out;
    out.predicted = scalar_state(0.0, 0.0, true);
    out.authority = scalar_state(4.0, 2.0, false);
    out.current = scalar_state(0.0, 0.0, true);
    out.field_errors = errors(4.0, 2.0);
    out.basis = 10;
    out.current_label = 20;
    out.policy = int(RecoveryPolicy::REBASE_RECOVER);
    out.fallback_epsilon = 1.0;
    out.fallback_teleport = 100.0;
    out.max_restore_ticks = 8;
    out.tick_delta = 0.5;
    out.domain = predict::Domain::IN_DOMAIN;
    out.attribution = predict::Attribution::CLOSURE;
    return out;
}

bool writes_every_restore(const WritePlan &p_plan) {
    for (int at = 0; at < int(p_plan.restore.values.size()); ++at) {
        if (!p_plan.restore.has(at)) {
            continue;
        }
        if (!p_plan.write.has(at)
            || value_error(
                p_plan.restore.values[uint32_t(at)],
                p_plan.write.values[uint32_t(at)],
                false
            ) > 0.000001) {
            return false;
        }
    }
    return true;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] a partial recovery is bounded"
) {
    RecoveryState state;
    const WritePlan plan = stage_recovery(
        recovery_wiring(),
        snap_config(),
        recovery_request(),
        state
    );

    CHECK_FALSE(plan.skip);
    CHECK_FALSE(plan.teleport);
    NETW_CHECK_CLOSE(double(plan.restore.values[0]), 2.0, 0.000001);
    CHECK_FALSE(plan.restore.has(1));
    CHECK_FALSE(bool(plan.restore.values[2]));
    CHECK(writes_every_restore(plan));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] full closure outranks rules"
) {
    const Wiring wiring = recovery_wiring();
    RecoveryRequest request = recovery_request();
    request.domain = predict::Domain::OUT_OF_DOMAIN;
    request.suppressed = true;
    RecoveryState state;
    WritePlan plan = stage_recovery(
        wiring,
        snap_config(),
        request,
        state
    );
    CHECK_FALSE(plan.skip);
    CHECK_FALSE(plan.teleport);
    CHECK(plan.restore.has(1));
    NETW_CHECK_CLOSE(double(plan.restore.values[0]), 4.0, 0.000001);

    request.domain = predict::Domain::IN_DOMAIN;
    request.suppressed = false;
    request.field_errors[0] = 10.0;
    plan = stage_recovery(wiring, snap_config(), request, state);
    CHECK(plan.teleport);
    CHECK(plan.restore.has(1));
    NETW_CHECK_EQ(int(plan.op), int(Operator::FULL_CLOSURE));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] windows and cooldowns key the "
    "decision"
) {
    Slot slot;
    slot.open = true;
    slot.rewire(recovery_wiring());
    slot.config = snap_config();
    RecoveryRequest request = recovery_request();
    request.attribution = predict::Attribution::UNKNOWN;

    slot.open_recovery_window(10, 3);
    CHECK(slot.recovery.window_contains(10));
    CHECK(slot.recovery.window_contains(13));
    CHECK_FALSE(slot.recovery.window_contains(14));
    CHECK_FALSE(slot.recover(request).skip);
    CHECK(slot.last_write_plan.restore.has(1));

    request.basis = 14;
    request.attribution = predict::Attribution::CLOSURE;
    slot.suppress_recovery_until(20, 3);
    CHECK(slot.recover(request).skip);
    request.current_label = 23;
    CHECK_FALSE(slot.recover(request).skip);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] guarded projection names only "
    "projected restores"
) {
    const Wiring wiring = recovery_wiring(false);
    RecoveryRequest request = recovery_request();
    request.ack_age_ticks = 2;
    request.field_errors[1] = 0.1;
    RecoveryState state;
    WritePlan projected = stage_recovery(
        wiring,
        snap_config(int(RestoreMode::EXTRAPOLATED)),
        request,
        state
    );
    NETW_CHECK_EQ(int(projected.op), int(Operator::REBASE_PROJECTED));
    NETW_CHECK_CLOSE(double(projected.restore.values[0]), 6.0, 0.000001);

    request.field_errors[1] = 1.0;
    WritePlan guarded = stage_recovery(
        wiring,
        snap_config(int(RestoreMode::EXTRAPOLATED)),
        request,
        state
    );
    NETW_CHECK_EQ(int(guarded.op), int(Operator::REBASE_EXACT));
    NETW_CHECK_CLOSE(double(guarded.restore.values[0]), 4.0, 0.000001);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] non-contraction promotes the "
    "next recovery"
) {
    Slot slot;
    slot.open = true;
    slot.rewire(recovery_wiring());
    slot.config = snap_config();
    RecoveryRequest request = recovery_request();
    request.collision_cooldown_ticks = 5;

    CHECK_FALSE(slot.recover(request).escalated);
    CHECK_FALSE(slot.recover(request).escalated);
    CHECK_FALSE(slot.recover(request).escalated);
    CHECK(slot.recovery.escalate_next);
    const WritePlan promoted = slot.recover(request);
    CHECK(promoted.escalated);
    CHECK(promoted.teleport);
    CHECK(promoted.restore.has(1));
    NETW_CHECK_EQ(slot.recovery.cooldown_until, 25);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] the engine publishes its effect"
) {
    Slot slot;
    slot.open = true;
    slot.rewire(recovery_wiring());
    slot.config = snap_config();
    const WritePlan plan = slot.recover(recovery_request());
    CHECK(writes_every_restore(plan));
    NETW_CHECK_CLOSE(double(slot.state.values[0]), 2.0, 0.000001);

    WritePlan dropped = plan;
    dropped.write = StateRow();
    dropped.write.resize(slot.wiring.count());
    CHECK_FALSE(writes_every_restore(dropped));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] transport preserves newer pose"
) {
    LocalVector<FieldDecl> fields;
    FieldDecl position;
    position.key = StringName("position");
    position.teleport_at = 100.0;
    fields.push_back(position);
    FieldDecl heading;
    heading.key = StringName("heading");
    heading.teleport_at = 100.0;
    heading.angle = true;
    fields.push_back(heading);
    FieldDecl latch;
    latch.key = StringName("latch");
    fields.push_back(latch);
    const Wiring wiring = compile(fields);

    StateRow predicted;
    predicted.resize(3);
    predicted.set(0, Vector2(10.0, 5.0));
    predicted.set(1, 179.0 * PI / 180.0);
    predicted.set(2, true);
    StateRow authority = predicted;
    authority.set(0, Vector2(10.5, 4.75));
    authority.set(1, -179.0 * PI / 180.0);
    StateRow current = predicted;
    current.set(0, Vector2(12.0, 5.0));
    current.set(1, -165.0 * PI / 180.0);

    const TransportPlan plan = transport(
        wiring,
        predicted,
        authority,
        current
    );
    CHECK(plan.valid);
    const Vector2 restored_position = plan.restore.values[0];
    NETW_CHECK_CLOSE(restored_position.x, 12.5, 0.000001);
    NETW_CHECK_CLOSE(restored_position.y, 4.75, 0.000001);
    NETW_CHECK_CLOSE(
        double(plan.delta.values[1]),
        2.0 * PI / 180.0,
        0.000001
    );
    CHECK_FALSE(plan.restore.has(2));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] observe and dissipate name their "
    "effects"
) {
    RecoveryRequest request = recovery_request();
    request.policy = int(RecoveryPolicy::OBSERVE);
    RecoveryState state;
    const WritePlan observed = stage_recovery(
        recovery_wiring(),
        snap_config(),
        request,
        state
    );
    CHECK(observed.skip);
    CHECK_FALSE(observed.restore.any());

    const WritePlan dissipated = dissipate_plan(3, 17);
    CHECK_FALSE(dissipated.skip);
    NETW_CHECK_EQ(dissipated.basis, 17);
    NETW_CHECK_EQ(int(dissipated.op), int(Operator::DISSIPATE));
    CHECK_FALSE(dissipated.restore.any());
    CHECK_FALSE(dissipated.write.any());
}

TransportEvidence admissible_transport() {
    TransportEvidence out;
    out.candidate = true;
    out.basis_witness_clean = true;
    out.recent_witness_clean = true;
    out.non_pose_agrees = true;
    out.below_teleport = true;
    out.snap_correction = true;
    return out;
}

DissipateEvidence admissible_dissipate() {
    DissipateEvidence out;
    out.momentum_active = true;
    out.basis_witness_clean = true;
    out.recent_witness_clean = true;
    out.snap_correction = true;
    out.meter = 1;
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] a transport needs every fact it "
    "gathered, and any one of them withdrawn refuses it"
) {
    CHECK(transport_admissible(admissible_transport()));

    TransportEvidence no_candidate = admissible_transport();
    no_candidate.candidate = false;
    CHECK(!transport_admissible(no_candidate));

    TransportEvidence dirty_basis = admissible_transport();
    dirty_basis.basis_witness_clean = false;
    CHECK(!transport_admissible(dirty_basis));

    TransportEvidence dirty_run = admissible_transport();
    dirty_run.recent_witness_clean = false;
    CHECK(!transport_admissible(dirty_run));

    TransportEvidence non_pose = admissible_transport();
    non_pose.non_pose_agrees = false;
    CHECK(!transport_admissible(non_pose));

    TransportEvidence far = admissible_transport();
    far.below_teleport = false;
    CHECK(!transport_admissible(far));

    TransportEvidence escalated = admissible_transport();
    escalated.escalated = true;
    CHECK(!transport_admissible(escalated));

    TransportEvidence blended = admissible_transport();
    blended.snap_correction = false;
    CHECK(!transport_admissible(blended));

    TransportEvidence observing = admissible_transport();
    observing.observing = true;
    CHECK(!transport_admissible(observing));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Recovery] a dissipate window opens only for "
    "a momentum-only error with a meter left to spend"
) {
    CHECK(dissipate_admissible(admissible_dissipate()));

    DissipateEvidence quiet = admissible_dissipate();
    quiet.momentum_active = false;
    CHECK(!dissipate_admissible(quiet));

    DissipateEvidence mixed = admissible_dissipate();
    mixed.other_active = true;
    CHECK(!dissipate_admissible(mixed));

    DissipateEvidence spent = admissible_dissipate();
    spent.meter = 0;
    CHECK(!dissipate_admissible(spent));

    DissipateEvidence dirty = admissible_dissipate();
    dirty.recent_witness_clean = false;
    CHECK(!dissipate_admissible(dirty));

    DissipateEvidence escalated = admissible_dissipate();
    escalated.escalated = true;
    CHECK(!dissipate_admissible(escalated));

    DissipateEvidence observing = admissible_dissipate();
    observing.observing = true;
    CHECK(!dissipate_admissible(observing));
}

} // namespace TestNetwPredictRecoveryLaws

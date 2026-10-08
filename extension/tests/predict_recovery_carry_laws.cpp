#include "support/netw_test.h"

#include <algorithm>

#include "godot/variant.hpp"
#include "netw/api/prediction.hpp"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/quantize.hpp"
#include "support/loopback_rig.h"
#include "support/minted_script.h"

using namespace godot;

namespace TestNetwPredictRecoveryCarry {

using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr int NUDGED_AT = 20;
constexpr int STEPS = 80;
constexpr double NUDGE = 0.05;
constexpr double CLOSE = 1.0e-5;

constexpr const char *ADVANCING_RULE = R"(extends RefCounted

func rule(value: Variant, ctx: NetwPredictCarryContext) -> Variant:
	var motion: Vector2 = ctx.input.get(&"motion", Vector2())
	return Vector2(value) + motion * 60.0 * ctx.delta
)";

struct Disturbed {
    int64_t corrections = 0;
    double client_y = 0.0;
    double server_y = 0.0;
    double overshoot = 0.0;
};

EntityDecl frame_body(bool p_ruled) {
    EntityDecl out = EntityDecl()
                         .named("P")
                         .on_schema(p_ruled ? "CarriedPose" : "LeaningPose")
                         .synced("position")
                         .placed_at(Vector2())
                         .predicted(0.01)
                         .scheduled(netw::Schedule::FRAME)
                         .recovered_by(netw::RecoveryPolicy::REBASE_RECOVER);
    if (p_ruled) {
        out.carried(1.0, 200.0);
    } else {
        out.synced("lean");
    }
    return out;
}

Disturbed nudged_once(double p_latency_ms, bool p_ruled = false) {
    LoopbackRig rig(1);
    if (p_latency_ms > 0.0) {
        const Ref<netw::LocalLinkConditions> flight
            = netw::LocalLinkConditions::create(7);
        flight->set_latency_ms(p_latency_ms);
        rig.conditions(0, flight);
    }
    rig.declare_world(
        WorldDecl().clocked(30, 3).lag_compensated().player(
            frame_body(p_ruled),
            0
        )
    );
    Variant rule;
    if (p_ruled) {
        const Ref<Script> script = netw_test::minted_script(ADVANCING_RULE);
        REQUIRE(script.is_valid());
        rule = script->call("new");
        rig.prediction_pool(0)->set_carry(
            rig.prediction_slot(StringName("P"), 0),
            StringName("position"),
            Callable(Object::cast_to<Object>(rule), StringName("rule"))
        );
    }
    Node2D *owner
        = Object::cast_to<Node2D>(rig.node_of(rig.entity_of("P", 0), 0));
    Node2D *authority
        = Object::cast_to<Node2D>(rig.node_of(rig.entity_of("P")));
    REQUIRE(owner != nullptr);
    REQUIRE(authority != nullptr);
    owner->set("motion", Vector2(1.0, 0.0));
    Disturbed out;
    for (int at = 0; at < STEPS; ++at) {
        if (at == NUDGED_AT) {
            authority->set_position(
                authority->get_position() + Vector2(0.0, NUDGE)
            );
        }
        rig.step_frame(1);
        out.overshoot = std::max(
            out.overshoot,
            double(owner->get_position().y - authority->get_position().y)
        );
    }
    out.corrections
        = rig.prediction_handle(StringName("P"), 0)
              ->get_stats()
              ->get_int_fact(netw::NetwPredictStats::FACT_CORRECTIONS);
    out.client_y = owner->get_position().y;
    out.server_y = authority->get_position().y;
    return out;
}

TEST_CASE(
    "[Networked][Predict] PR1 one difference on a moving frame body that "
    "does not replay costs one correction, and the client then equals the "
    "server"
) {
    for (const bool ruled : {false, true}) {
        for (const double latency : {0.0, 100.0}) {
            const Disturbed run = nudged_once(latency, ruled);
            NETW_CHECK_EQ(run.corrections, int64_t(1));
            NETW_CHECK_CLOSE(run.server_y, NUDGE, CLOSE);
            NETW_CHECK_CLOSE(run.client_y, run.server_y, CLOSE);
        }
    }
}

TEST_CASE(
    "[Networked][Predict] PR2 a row recorded before a correction's write is "
    "not judged against it, so the difference lands once"
) {
    for (const double latency : {0.0, 100.0}) {
        const Disturbed run = nudged_once(latency);
        NETW_CHECK_CLOSE(run.overshoot, 0.0, CLOSE);
    }
}

Ref<netw::NetwQuantizeScalar> half_step() {
    Ref<netw::NetwQuantizeScalar> out;
    out.instantiate();
    out->set_min_limit(-64.0);
    out->set_max_limit(64.0);
    out->set_resolution_step(0.5);
    return out;
}

double tolerance_of(int p_archetype, const StringName &p_field) {
    LoopbackRig rig(1);
    rig.declare_world(
        WorldDecl().clocked(30, 3).lag_compensated().player(
            EntityDecl()
                .named("P")
                .on_schema("QuantizedPose")
                .archetyped(p_archetype)
                .synced("position")
                .quantized(half_step())
                .synced("lean")
                .placed_at(Vector2())
                .predicted(0.01),
            0
        )
    );
    return rig.prediction_pool(0)->epsilon_for_field(
        rig.prediction_slot(StringName("P"), 0),
        p_field,
        -1.0
    );
}

TEST_CASE(
    "[Networked][Predict] PR5 a solver body tolerates one and a half "
    "quantizer steps on a quantized field, and every other field and body "
    "keeps the divergence epsilon"
) {
    const double step = 2.0 * half_step()->max_error(Variant::VECTOR2);
    NETW_CHECK_CLOSE(
        tolerance_of(
            netw::NetwPrediction::ARCHETYPE_SOLVER_BODY,
            StringName("position")
        ),
        1.5 * step,
        CLOSE
    );
    NETW_CHECK_CLOSE(
        tolerance_of(
            netw::NetwPrediction::ARCHETYPE_SOLVER_BODY,
            StringName("lean")
        ),
        -1.0,
        CLOSE
    );
    NETW_CHECK_CLOSE(
        tolerance_of(
            netw::NetwPrediction::ARCHETYPE_SCRIPTED,
            StringName("position")
        ),
        -1.0,
        CLOSE
    );
}

TEST_CASE(
    "[Networked][Predict] PR6 the recover seam's default carries the "
    "authority's difference onto the present body"
) {
    Ref<netw::NetwMultiplayer> session;
    session.instantiate();
    Dictionary payload;
    payload[StringName("position")] = Vector2(2.0, 1.0);
    Dictionary predicted;
    predicted[StringName("position")] = Vector2(2.0, 0.0);
    Dictionary current;
    current[StringName("position")] = Vector2(5.0, 0.0);
    Dictionary pose_errors;
    pose_errors[StringName("position")] = 1.0;
    Dictionary wiring;
    wiring[StringName("epsilon")] = 0.01;
    wiring[StringName("teleport_threshold")] = 100.0;
    Dictionary verdict;
    verdict[StringName("domain")] = int(netw::Domain::OUT_OF_DOMAIN);
    const Ref<netw::NetwPredictRecovery> planned
        = session->predict_recover_default(
            payload,
            predicted,
            netw::NetwPrediction::RECOVERY_POLICY_REBASE_RECOVER,
            netw::NetwSimulation::RESTORE_EXACT,
            Dictionary(),
            current,
            pose_errors,
            wiring,
            verdict,
            1.0 / 30.0
        );
    REQUIRE(planned.is_valid());
    CHECK_FALSE(planned->skip());
    CHECK_FALSE(planned->teleport());
    const Vector2 written
        = planned->write().get(StringName("position"), Vector2());
    NETW_CHECK_CLOSE(double(written.x), 5.0, CLOSE);
    NETW_CHECK_CLOSE(double(written.y), 1.0, CLOSE);
}

} // namespace TestNetwPredictRecoveryCarry

#endif

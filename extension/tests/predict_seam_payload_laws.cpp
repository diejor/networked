#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/prediction.hpp"
#include "netw/script/model.hpp"

#if defined(NETW_TIER_HOSTED)

#include "support/carrier.h"
#include "support/loopback_rig.h"
#include "support/minted_script.h"

using namespace godot;

namespace TestNetwPredictSeamPayload {

using netw_test::EntityDecl;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

constexpr const char *LISTENING_SESSION = R"(extends NetwMultiplayer

var recovered := []
var evaluated := []

func _predict_recover(
		payload: Dictionary,
		predicted: Dictionary,
		policy: NetwPrediction.RecoveryPolicy,
		snap_restore: NetwSimulation.Restore,
		projection: Dictionary,
		current: Dictionary,
		pose_errors: Dictionary,
		wiring: Dictionary,
		verdict: Dictionary,
		tick_delta: float,
) -> NetwPredictRecovery:
	recovered.append([
		payload.duplicate(), policy, snap_restore, projection.duplicate(),
		current.duplicate(), pose_errors.duplicate(), verdict.duplicate(),
		tick_delta,
	])
	return predict_recover_default(
		payload, predicted, policy, snap_restore, projection, current,
		pose_errors, wiring, verdict, tick_delta,
	)

func _predict_evaluate(
		domain: NetwPrediction.Domain,
		verdict: NetwPrediction.ExactVerdict,
		predicted: Dictionary,
		payload: Dictionary,
		wiring: Dictionary,
		field_sink: Dictionary,
) -> NetwPredictJudgement:
	evaluated.append([domain, verdict, predicted.duplicate(),
		payload.duplicate()])
	return predict_evaluate_default(
		domain, verdict, predicted, payload, wiring, field_sink,
	)
)";

constexpr const char *LISTENING_RULE = R"(extends RefCounted

var seen := []

func rule(value: Variant, ctx: NetwPredictCarryContext) -> Variant:
	seen.append([value, ctx.state.duplicate(), ctx.input.duplicate(),
		ctx.delta, ctx.label])
	var motion: Vector2 = ctx.input.get(&"motion", Vector2())
	return Vector2(value) + motion * 60.0 * ctx.delta
)";

EntityDecl leaning_player(
    netw::Schedule p_schedule,
    netw::RecoveryPolicy p_recovery
) {
    return EntityDecl()
        .named("P")
        .on_schema("SeamPose")
        .synced("position")
        .synced("lean")
        .placed_at(Vector2())
        .predicted(0.01)
        .scheduled(p_schedule)
        .recovered_by(p_recovery);
}

void nudge(LoopbackRig &p_rig) {
    Node2D *authority
        = Object::cast_to<Node2D>(p_rig.node_of(p_rig.entity_of("P")));
    REQUIRE(authority != nullptr);
    authority->set_position(authority->get_position() + Vector2(0.0, 0.05));
    authority->set(
        "lean",
        Vector2(authority->get("lean")) + Vector2(0.02, -0.01)
    );
}

void step(LoopbackRig &p_rig, netw::Schedule p_schedule) {
    nudge(p_rig);
    if (p_schedule == netw::Schedule::FRAME) {
        p_rig.step_frame(1);
    } else {
        p_rig.step_ticks(1);
    }
}

struct Heard {
    int64_t calls = 0;
    int64_t digest = 0;
    String first;
};

Heard heard_of(const Array &p_calls) {
    Heard out;
    out.calls = p_calls.size();
    const String text = UtilityFunctions::var_to_str(p_calls);
    out.digest = int64_t(text.hash());
    if (!p_calls.is_empty()) {
        out.first = UtilityFunctions::var_to_str(p_calls[0]);
    }
    return out;
}

struct SeamRun {
    Heard recovered;
    Heard evaluated;
};

SeamRun seam_run() {
    LoopbackRig rig(1);
    const Ref<Script> session = netw_test::minted_script(LISTENING_SESSION);
    REQUIRE(session.is_valid());
    rig.client(0)->set_script(session);
    rig.declare_world(
        WorldDecl().clocked(30, 3).lag_compensated().player(
            leaning_player(
                netw::Schedule::FRAME,
                netw::RecoveryPolicy::REBASE_RECOVER
            ),
            0
        )
    );
    rig.node_of(rig.entity_of("P", 0), 0)->set("motion", Vector2(1.0, 0.0));
    for (int at = 0; at < 30; ++at) {
        step(rig, netw::Schedule::FRAME);
    }
    SeamRun out;
    out.recovered = heard_of(rig.client(0)->get("recovered"));
    out.evaluated = heard_of(rig.client(0)->get("evaluated"));
    return out;
}

Heard carry_run() {
    LoopbackRig rig(1);
    rig.declare_world(
        WorldDecl().clocked(30, 3).lag_compensated().player(
            EntityDecl()
                .named("P")
                .on_schema("CarriedPose")
                .synced("position")
                .placed_at(Vector2())
                .predicted(0.01)
                .scheduled(netw::Schedule::FRAME)
                .carried(1.0, 200.0),
            0
        )
    );
    rig.conditions(0, netw::LocalLinkConditions::polls(4));
    rig.conditions(-1, netw::LocalLinkConditions::polls(4));
    const Ref<Script> rule_script = netw_test::minted_script(LISTENING_RULE);
    REQUIRE(rule_script.is_valid());
    const Variant rule = rule_script->call("new");
    Object *listener = rule;
    REQUIRE(listener != nullptr);
    rig.prediction_pool(0)->set_carry(
        rig.prediction_slot(StringName("P"), 0),
        StringName("position"),
        Callable(listener, StringName("rule"))
    );
    rig.node_of(rig.entity_of("P", 0), 0)->set("motion", Vector2(1.0, 0.0));
    for (int at = 0; at < 70; ++at) {
        if (at == 31) {
            Node2D *authority
                = Object::cast_to<Node2D>(rig.node_of(rig.entity_of("P")));
            authority->set_position(
                authority->get_position() + Vector2(60.0, -40.0)
            );
        }
        rig.step_frame(1);
    }
    return heard_of(listener->get("seen"));
}

void check_heard(
    const Heard &p_heard,
    int64_t p_calls,
    int64_t p_digest,
    const char *p_first
) {
    NETW_CHECK_EQ(p_heard.calls, p_calls);
    NETW_CHECK_EQ(p_heard.digest, p_digest);
    const bool same_first = p_heard.first == String(p_first);
    if (!same_first) {
        UtilityFunctions::print(p_heard.first);
    }
    CHECK(same_first);
}

constexpr const char *RECOVER_FIRST = R"([{
&"lean": Vector2(0.06, -0.03),
&"position": Vector2(2, 0.15)
}, 1, 0, {}, {
&"lean": Vector2(0, 0),
&"position": Vector2(4, 0)
}, {}, {
&"ack_age_ticks": 1,
&"attribution": 0,
&"contact_window": false,
&"domain": 1,
&"pose_unmeasured": true,
&"suppressed": false
}, 0.03333333333333333])";

constexpr const char *EVALUATE_FIRST = R"([1, 0, {
&"lean": Vector2(0, 0),
&"position": Vector2(2, 0)
}, {
&"lean": Vector2(0.06, -0.03),
&"position": Vector2(2, 0.15)
}])";

constexpr const char *CARRY_FIRST = R"([Vector2(58, 0), {
&"position": Vector2(58, 0)
}, {
&"bombing": false,
&"motion": Vector2(1, 0)
}, 0.03333333333333333, 29])";

TEST_CASE(
    "[Networked][Predict] HS1 a script overriding the recover seam hears the "
    "same keys and values on every correction"
) {
    check_heard(seam_run().recovered, 14, 3010858608LL, RECOVER_FIRST);
}

TEST_CASE(
    "[Networked][Predict] HS2 a script overriding the evaluate seam hears the "
    "same keys and values on every comparison"
) {
    check_heard(seam_run().evaluated, 14, 2305648560LL, EVALUATE_FIRST);
}

TEST_CASE(
    "[Networked][Predict] HS3 a carry rule hears the same state and input on "
    "every carried recovery"
) {
    check_heard(carry_run(), 15, 3245973770LL, CARRY_FIRST);
}

} // namespace TestNetwPredictSeamPayload

#endif

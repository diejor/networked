#include "support/netw_test.h"

#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"
#include "netw/prediction_core.hpp"

namespace TestNetwPredictMeterLaws {

using namespace godot;
using namespace netw;
using netw::predict::Domain;
using netw::predict::ROW_ACKED;

Ref<NetwPredictJudgement> seam_corrected(
    int64_t p_domain,
    int64_t p_verdict,
    Dictionary p_predicted,
    Dictionary p_payload,
    Dictionary p_divergence
) {
    p_divergence[StringName("value")] = 0.25;
    return NetwPredictJudgement::of(0.25, true);
}

Ref<NetwPredictJudgement> seam_agreed(
    int64_t p_domain,
    int64_t p_verdict,
    Dictionary p_predicted,
    Dictionary p_payload,
    Dictionary p_divergence
) {
    p_divergence[StringName("value")] = 12.0;
    return NetwPredictJudgement::of(12.0, false);
}

LocalVector<netw::predict::FieldDecl> declaration(double p_epsilon) {
    LocalVector<netw::predict::FieldDecl> out;
    out.push_back(
        netw::field_decl(
            StringName("value"),
            0,
            StringName(),
            0.0,
            false,
            false,
            p_epsilon
        )
    );
    return out;
}

int64_t seated(NetwPredictionEngine &r_pool, double p_epsilon) {
    const int64_t slot = r_pool.open(declaration(p_epsilon));
    REQUIRE(slot >= 0);
    return slot;
}

int meter_of_judgement(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    Domain p_domain,
    const Callable &p_seam
) {
    const Dictionary judged = p_pool->judge_state(
        p_slot,
        4,
        int64_t(p_domain),
        int64_t(ROW_ACKED),
        Dictionary(),
        Dictionary(),
        p_seam
    );
    return int(judged[StringName("meter")]);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Meter] M1 a correction is never worth "
    "zero, so a budget that spends on one can always tell it happened"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = seated(held_pool, 0.5);

    const int measured = meter_of_judgement(
        pool,
        slot,
        Domain::OUT_OF_DOMAIN,
        callable_mp_static(&seam_corrected)
    );
    NETW_CHECK_GE(measured, 1);

    SUBCASE("and the field it corrected sat INSIDE its declared epsilon") {
        const Dictionary judged = pool->judge_state(
            slot,
            4,
            int64_t(Domain::OUT_OF_DOMAIN),
            int64_t(ROW_ACKED),
            Dictionary(),
            Dictionary(),
            callable_mp_static(&seam_corrected)
        );
        CHECK(bool(judged[StringName("corrected")]));
        NETW_CHECK_CLOSE(
            double(judged[StringName("divergence")]),
            0.25,
            0.0001
        );
    }
}

TEST_CASE(
    "[Networked][Predict][Hosted][Meter] M2 a verdict that corrected nothing "
    "reports a meter of zero however far apart the two states measured"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = seated(held_pool, 0.5);

    const Dictionary judged = pool->judge_state(
        slot,
        4,
        int64_t(Domain::IN_DOMAIN),
        int64_t(ROW_ACKED),
        Dictionary(),
        Dictionary(),
        callable_mp_static(&seam_agreed)
    );
    CHECK_FALSE(bool(judged[StringName("corrected")]));
    NETW_CHECK_EQ(int(judged[StringName("meter")]), 0);
    NETW_CHECK_CLOSE(double(judged[StringName("divergence")]), 12.0, 0.0001);
}

} // namespace TestNetwPredictMeterLaws

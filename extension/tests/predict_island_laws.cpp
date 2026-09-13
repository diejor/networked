#include "support/netw_test.h"

#include "netw/api/predict.hpp"
#include "netw/api/predict_island.hpp"

using namespace godot;

namespace TestNetwPredictIslandLaws {

using netw::NetwPredict;
using netw::NetwPredictIsland;

Ref<NetwPredictIsland> island() {
    Ref<NetwPredictIsland> out;
    out.instantiate();
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] naming a roster or a producer IS the "
    "declaration, and nothing else is"
) {
    Ref<NetwPredictIsland> rule = island();
    CHECK_FALSE(rule->get_declared());

    rule->set_reconcile(NetwPredict::RECONCILE_JOINT);
    rule->simulate_nearest(3);
    CHECK_FALSE(rule->get_declared());

    rule->from_interest(StringName("near"));
    CHECK(rule->get_declared());
    NETW_CHECK_EQ(rule->get_producers().size(), 1);

    rule->from_interest(StringName("near"));
    NETW_CHECK_EQ(rule->get_producers().size(), 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] the two comparison claims are one "
    "question, and a produced island may not answer it exactly"
) {
    Ref<NetwPredictIsland> rule = island();
    rule->set_approximate(true);
    REQUIRE(rule->get_approximate());

    rule->set_exact_claim(true);
    CHECK(rule->get_exact_claim());
    CHECK_FALSE(rule->get_approximate());

    rule->set_approximate(true);
    CHECK(rule->get_approximate());
    CHECK_FALSE(rule->get_exact_claim());

    Ref<NetwPredictIsland> produced = island();
    produced->from_interest();
    CHECK(produced->get_approximate());
    produced->set_exact_claim(true);
    CHECK_FALSE(produced->get_exact_claim());
    CHECK(produced->get_approximate());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] a member the island does not have "
    "declares no fidelity and no predictor, and writing one does not add it"
) {
    Ref<NetwPredictIsland> rule = island();
    Ref<netw::NetwEntity> absent;

    NETW_CHECK_EQ(rule->fidelity_of(absent), -1);
    CHECK_FALSE(rule->predictor_of(absent).is_valid());
    CHECK_FALSE(rule->has_member(absent));

    rule->set_fidelity(absent, NetwPredict::FIDELITY_SIMULATED);
    NETW_CHECK_EQ(rule->fidelity_of(absent), -1);
    CHECK_FALSE(rule->has_member(absent));
    NETW_CHECK_EQ(rule->get_participants().size(), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] an inherited rule is replaced WHOLE "
    "by the first act that names a roster, and refined by everything else"
) {
    Ref<NetwPredictIsland> scene_rule = island();
    scene_rule->from_interest(StringName("scene"));
    scene_rule->simulate_nearest(4);

    Ref<NetwPredictIsland> taken = scene_rule->inheritable();
    REQUIRE(taken.is_valid());
    CHECK(taken->get_inherited());
    NETW_CHECK_EQ(taken->get_producers().size(), 1);
    NETW_CHECK_EQ(taken->get_promotion_count(), 4);

    taken->set_reconcile(NetwPredict::RECONCILE_JOINT);
    NETW_CHECK_EQ(taken->get_producers().size(), 1);
    CHECK(taken->get_inherited());

    taken->from_interest(StringName("own"));
    CHECK_FALSE(taken->get_inherited());
    NETW_CHECK_EQ(taken->get_producers().size(), 1);
    CHECK(taken->get_producers()[0] == String("own"));
    NETW_CHECK_EQ(taken->get_promotion_count(), 0);
    NETW_CHECK_EQ(taken->get_reconcile(), int(NetwPredict::RECONCILE_JOINT));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] a rule that names nobody is not "
    "inheritable, because it opts no descendant into a compare"
) {
    Ref<NetwPredictIsland> rule = island();
    rule->set_reconcile(NetwPredict::RECONCILE_JOINT);
    rule->simulate_all();
    CHECK(rule->inheritable().is_null());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] a promotion verb names one policy "
    "and clamps the budget it carries"
) {
    Ref<NetwPredictIsland> rule = island();
    NETW_CHECK_EQ(rule->get_promotion(), int(NetwPredict::PROMOTION_NONE));

    rule->simulate_nearest(-3);
    NETW_CHECK_EQ(rule->get_promotion(), int(NetwPredict::PROMOTION_NEAREST));
    NETW_CHECK_EQ(rule->get_promotion_count(), 0);

    rule->simulate_within(-2.5);
    NETW_CHECK_EQ(rule->get_promotion(), int(NetwPredict::PROMOTION_WITHIN));
    CHECK(rule->get_promotion_meters() == doctest::Approx(0.0));

    rule->simulate_all();
    NETW_CHECK_EQ(rule->get_promotion(), int(NetwPredict::PROMOTION_ALL));

    rule->set_input_delay_ticks(-9);
    NETW_CHECK_EQ(rule->get_input_delay_ticks(), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Island] every act that moves what a drive is "
    "judged against announces itself, and reading one does not"
) {
    Ref<NetwPredictIsland> rule = island();
    Ref<NetwPredictIsland> probe = island();
    rule->watch_declaration(Callable(probe.ptr(), "simulate_all"));

    rule->get_declared();
    rule->get_participants();
    rule->fidelity_of(Ref<netw::NetwEntity>());
    NETW_CHECK_EQ(probe->get_promotion(), int(NetwPredict::PROMOTION_NONE));

    rule->from_interest(StringName("near"));
    NETW_CHECK_EQ(probe->get_promotion(), int(NetwPredict::PROMOTION_ALL));

    probe->simulate_nearest(0);
    rule->set_reconcile(NetwPredict::RECONCILE_JOINT);
    NETW_CHECK_EQ(probe->get_promotion(), int(NetwPredict::PROMOTION_ALL));

    probe->simulate_nearest(0);
    rule->simulate_nearest(2);
    NETW_CHECK_EQ(probe->get_promotion(), int(NetwPredict::PROMOTION_NEAREST));

    Ref<NetwPredictIsland> copy = rule->duplicate_rule();
    probe->simulate_nearest(0);
    copy->set_reconcile(NetwPredict::RECONCILE_INDEPENDENT);
    NETW_CHECK_EQ(probe->get_promotion(), int(NetwPredict::PROMOTION_NEAREST));
}

} // namespace TestNetwPredictIslandLaws

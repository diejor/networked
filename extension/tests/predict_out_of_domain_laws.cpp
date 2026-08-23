#include "support/netw_test.h"

#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestNetwPredictOutOfDomainLaws {

namespace {

using godot::Ref;
using godot::StringName;
using netw::NetwPredictionEngine;

int64_t predicting_slot(const Ref<NetwPredictionEngine> &p_pool) {
    Ref<netw::NetwPredictDeclaration> fields;
    fields.instantiate();
    fields->append_field(
        StringName("position"),
        int(netw::predict::PropertyClass::CAUSAL),
        StringName(),
        0.0,
        false,
        false,
        -1.0,
        -1.0,
        false
    );
    return p_pool->open(fields);
}

} // namespace

TEST_CASE(
    "[Networked][Predict][Hosted][Domain] an opened window covers its own "
    "label and every label of its cooldown"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = predicting_slot(pool);

    NETW_CHECK_EQ(pool->out_of_domain_until(slot), -1);
    CHECK_FALSE(pool->out_of_domain_at(slot, 10));

    pool->open_out_of_domain_window(slot, 10, 3);

    for (int64_t label = 10; label <= 13; ++label) {
        CAPTURE(label);
        CHECK(pool->out_of_domain_at(slot, label));
    }
    CHECK_FALSE(pool->out_of_domain_at(slot, 14));
    NETW_CHECK_EQ(pool->out_of_domain_until(slot), 14);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Domain] a later fact extends an open window "
    "and an earlier one never shortens it"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = predicting_slot(pool);

    pool->open_out_of_domain_window(slot, 20, 2);
    const int64_t after_later = pool->out_of_domain_until(slot);
    NETW_CHECK_EQ(after_later, 23);

    pool->open_out_of_domain_window(slot, 5, 2);
    NETW_CHECK_EQ(pool->out_of_domain_until(slot), after_later);
    CHECK(pool->out_of_domain_at(slot, 22));

    pool->open_out_of_domain_window(slot, 30, 2);
    NETW_CHECK_EQ(pool->out_of_domain_until(slot), 33);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Domain] clearing the window leaves no label "
    "covered"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t slot = predicting_slot(pool);

    pool->open_out_of_domain_window(slot, 10, 3);
    REQUIRE(pool->out_of_domain_at(slot, 12));

    pool->clear_out_of_domain_window(slot);

    NETW_CHECK_EQ(pool->out_of_domain_until(slot), -1);
    CHECK_FALSE(pool->out_of_domain_at(slot, 12));
    CHECK_FALSE(pool->out_of_domain_at(slot, 0));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Domain] a window belongs to the slot that "
    "opened it"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();
    const int64_t disturbed = predicting_slot(pool);
    const int64_t calm = predicting_slot(pool);

    pool->open_out_of_domain_window(disturbed, 10, 3);

    CHECK(pool->out_of_domain_at(disturbed, 12));
    CHECK_FALSE(pool->out_of_domain_at(calm, 12));
    NETW_CHECK_EQ(pool->out_of_domain_until(calm), -1);

    pool->clear_out_of_domain_window(calm);

    CHECK(pool->out_of_domain_at(disturbed, 12));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Domain] a slot that was never opened has no "
    "window and cannot be given one"
) {
    Ref<NetwPredictionEngine> pool;
    pool.instantiate();

    NETW_CHECK_EQ(pool->out_of_domain_until(-1), -1);
    CHECK_FALSE(pool->out_of_domain_at(-1, 0));

    pool->open_out_of_domain_window(-1, 10, 3);

    NETW_CHECK_EQ(pool->out_of_domain_until(-1), -1);
    CHECK_FALSE(pool->out_of_domain_at(-1, 10));
}

} // namespace TestNetwPredictOutOfDomainLaws

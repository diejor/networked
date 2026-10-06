#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict_stats.hpp"
#include "netw/api/prediction.hpp"

namespace TestNetwPredictStatsLifetimeLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredictionEngine;
using netw::NetwPredictStats;

String facts_off_their_fallback(const Dictionary &p_read) {
    String differing;
    for (int at = 0; at < NetwPredictStats::fact_count(); ++at) {
        const NetwPredictStats::Fact &fact = NetwPredictStats::facts()[at];
        const Variant value = p_read.get(StringName(fact.name), Variant());
        if (value.get_type() == Variant::INT
            && int64_t(value) != fact.fallback) {
            differing += String(fact.name) + " ";
        }
    }
    return differing;
}

TEST_CASE(
    "[Networked][Predict][Hosted] PL1 a predicted entity the session "
    "despawns is freed once the game drops its node, because its stats "
    "never hold it alive"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    NetwPredictionEngine *pool = core->get_prediction_engine();

    Node *body = memnew(Node);
    body->set_name("Predicted");
    ObjectID wrapper;
    {
        const Ref<NetwEntity> entity = NetwEntity::ensure(body);
        REQUIRE(entity.is_valid());
        wrapper = entity->get_instance_id();
        REQUIRE(pool->slot_register(entity) >= 0);
        entity->get_prediction()->fact_book()->bind_slot(pool, entity);
        pool->slot_unregister(entity);
    }
    REQUIRE(netw::gd::object_registered(wrapper));

    memdelete(body);

    CHECK_FALSE(netw::gd::object_registered(wrapper));
}

TEST_CASE(
    "[Networked][Predict][Hosted] PL2 stats read after their entity is gone "
    "answer every fact's fallback, never the seat the pool closed behind it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    NetwPredictionEngine *pool = core->get_prediction_engine();

    Node *body = memnew(Node);
    body->set_name("Departed");
    ObjectID wrapper;
    Ref<NetwPredictStats> stats;
    {
        const Ref<NetwEntity> entity = NetwEntity::ensure(body);
        REQUIRE(entity.is_valid());
        wrapper = entity->get_instance_id();
        REQUIRE(pool->slot_register(entity) >= 0);
        entity->get_prediction()->fact_book()->bind_slot(pool, entity);
        stats = entity->get_prediction()->get_stats();
        const String seated = facts_off_their_fallback(stats->to_dictionary());
        REQUIRE_FALSE(seated.is_empty());
        pool->slot_unregister(entity);
    }
    memdelete(body);
    REQUIRE_FALSE(netw::gd::object_registered(wrapper));

    const CharString gone
        = facts_off_their_fallback(stats->to_dictionary()).utf8();
    INFO(gone.get_data());
    CHECK(gone.length() == 0);
}

} // namespace TestNetwPredictStatsLifetimeLaws

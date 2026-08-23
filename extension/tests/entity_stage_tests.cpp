#include "support/netw_test.h"

#include "netw/entity_stage.hpp"

namespace TestNetwEntityStage {

using netw::EntityStage;
using netw::stage_can_begin_despawn;
using netw::stage_edge_is_legal;

constexpr int STAGES = 7;

struct Edge {
    EntityStage from;
    EntityStage to;
};

constexpr Edge LEGAL_EDGES[] = {
    {EntityStage::UNBOUND, EntityStage::TEMPLATE},
    {EntityStage::UNBOUND, EntityStage::ARMED},
    {EntityStage::UNBOUND, EntityStage::DESPAWNING},
    {EntityStage::ARMED, EntityStage::LIVE},
    {EntityStage::ARMED, EntityStage::DESPAWNING},
    {EntityStage::LIVE, EntityStage::DESPAWNING},
    {EntityStage::DESPAWNING, EntityStage::LINGERING},
    {EntityStage::DESPAWNING, EntityStage::FREED},
    {EntityStage::LINGERING, EntityStage::FREED},
};

bool listed_as_legal(int p_from, int p_to) {
    for (const Edge &edge : LEGAL_EDGES) {
        if (int(edge.from) == p_from && int(edge.to) == p_to) {
            return true;
        }
    }
    return false;
}

TEST_CASE("[Networked][Entity][Hosted] S1 the whole edge table, cell by cell") {
    for (int from = 0; from < STAGES; ++from) {
        for (int to = 0; to < STAGES; ++to) {
            NETW_FORMAT_INT(from_text, from);
            NETW_FORMAT_INT(to_text, to);
            CAPTURE(from_text);
            CAPTURE(to_text);
            CHECK(stage_edge_is_legal(from, to) == listed_as_legal(from, to));
        }
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] S2 a stage is never re-entered, which is "
    "where this differs from the session"
) {
    for (int stage = 0; stage < STAGES; ++stage) {
        NETW_FORMAT_INT(stage_text, stage);
        CAPTURE(stage_text);
        CHECK_FALSE(stage_edge_is_legal(stage, stage));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] S3 the two terminal stages admit nothing, and "
    "a stage off the table admits nothing either"
) {
    for (int to = 0; to < STAGES; ++to) {
        CHECK_FALSE(stage_edge_is_legal(int(EntityStage::TEMPLATE), to));
        CHECK_FALSE(stage_edge_is_legal(int(EntityStage::FREED), to));
    }

    CHECK_FALSE(stage_edge_is_legal(99, int(EntityStage::LIVE)));
    CHECK_FALSE(stage_edge_is_legal(-1, int(EntityStage::LIVE)));
    CHECK_FALSE(stage_edge_is_legal(int(EntityStage::LIVE), 99));
    CHECK_FALSE(stage_can_begin_despawn(99));
}

TEST_CASE(
    "[Networked][Entity][Hosted] S4 teardown opens wider than LIVE, so a "
    "hand-bound route is never stranded"
) {
    CHECK(stage_can_begin_despawn(int(EntityStage::UNBOUND)));
    CHECK(stage_can_begin_despawn(int(EntityStage::ARMED)));
    CHECK(stage_can_begin_despawn(int(EntityStage::LIVE)));

    CHECK_FALSE(stage_can_begin_despawn(int(EntityStage::TEMPLATE)));
    CHECK_FALSE(stage_can_begin_despawn(int(EntityStage::DESPAWNING)));
    CHECK_FALSE(stage_can_begin_despawn(int(EntityStage::LINGERING)));
    CHECK_FALSE(stage_can_begin_despawn(int(EntityStage::FREED)));

    for (int stage = 0; stage < STAGES; ++stage) {
        if (!stage_can_begin_despawn(stage)) {
            continue;
        }
        NETW_FORMAT_INT(stage_text, stage);
        CAPTURE(stage_text);
        CHECK(stage_edge_is_legal(stage, int(EntityStage::DESPAWNING)));
    }
}

} // namespace TestNetwEntityStage

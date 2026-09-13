#include "support/netw_test.h"

#include "netw/entity/stage.hpp"

namespace TestNetwStage {

using netw::entity::Stage;
using netw::entity::stage_can_begin_despawn;
using netw::entity::stage_edge_is_legal;

constexpr int STAGES = 7;

struct Edge {
    Stage from;
    Stage to;
};

constexpr Edge LEGAL_EDGES[] = {
    {Stage::UNBOUND, Stage::TEMPLATE},
    {Stage::UNBOUND, Stage::ARMED},
    {Stage::UNBOUND, Stage::DESPAWNING},
    {Stage::ARMED, Stage::LIVE},
    {Stage::ARMED, Stage::DESPAWNING},
    {Stage::LIVE, Stage::DESPAWNING},
    {Stage::DESPAWNING, Stage::LINGERING},
    {Stage::DESPAWNING, Stage::FREED},
    {Stage::LINGERING, Stage::FREED},
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
        CHECK_FALSE(stage_edge_is_legal(int(Stage::TEMPLATE), to));
        CHECK_FALSE(stage_edge_is_legal(int(Stage::FREED), to));
    }

    CHECK_FALSE(stage_edge_is_legal(99, int(Stage::LIVE)));
    CHECK_FALSE(stage_edge_is_legal(-1, int(Stage::LIVE)));
    CHECK_FALSE(stage_edge_is_legal(int(Stage::LIVE), 99));
    CHECK_FALSE(stage_can_begin_despawn(99));
}

TEST_CASE(
    "[Networked][Entity][Hosted] S4 teardown opens wider than LIVE, so a "
    "hand-bound route is never stranded"
) {
    CHECK(stage_can_begin_despawn(int(Stage::UNBOUND)));
    CHECK(stage_can_begin_despawn(int(Stage::ARMED)));
    CHECK(stage_can_begin_despawn(int(Stage::LIVE)));

    CHECK_FALSE(stage_can_begin_despawn(int(Stage::TEMPLATE)));
    CHECK_FALSE(stage_can_begin_despawn(int(Stage::DESPAWNING)));
    CHECK_FALSE(stage_can_begin_despawn(int(Stage::LINGERING)));
    CHECK_FALSE(stage_can_begin_despawn(int(Stage::FREED)));

    for (int stage = 0; stage < STAGES; ++stage) {
        if (!stage_can_begin_despawn(stage)) {
            continue;
        }
        NETW_FORMAT_INT(stage_text, stage);
        CAPTURE(stage_text);
        CHECK(stage_edge_is_legal(stage, int(Stage::DESPAWNING)));
    }
}

} // namespace TestNetwStage

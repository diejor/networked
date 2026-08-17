// The entity stage machine's laws.
//
// The whole specification is a 7x7 table, so S1 states the whole table rather
// than a sample of it: an edge law that checks the interesting rows is a law
// that cannot notice a row nobody thought was interesting. The rest name the
// three properties the table has that the session's does not, each of which a
// reader would otherwise have to infer from an absence.

#include "support/netw_test.h"

#include "netw/entity_stage.hpp"

namespace TestNetwEntityStage {

using netw::EntityStage;
using netw::NetwEntityStage;

constexpr int STAGES = 7;

// The table, spelled as data so the law reads as a table rather than as a
// second implementation of the switch it is checking.
constexpr bool LEGAL[STAGES][STAGES] = {
    //          UNB    TMPL   ARMED  LIVE   DESP   LING   FREED
    /* UNB  */ {false, true,  true,  false, true,  false, false},
    /* TMPL */ {false, false, false, false, false, false, false},
    /* ARMED*/ {false, false, false, true,  true,  false, false},
    /* LIVE */ {false, false, false, false, true,  false, false},
    /* DESP */ {false, false, false, false, false, true,  true },
    /* LING */ {false, false, false, false, false, false, true },
    /* FREED*/ {false, false, false, false, false, false, false},
};

TEST_CASE("[Networked][Entity][Hosted] S1 the whole edge table, cell by cell") {
    for (int from = 0; from < STAGES; ++from) {
        for (int to = 0; to < STAGES; ++to) {
            NETW_FORMAT_INT(from_text, from);
            NETW_FORMAT_INT(to_text, to);
            CAPTURE(from_text);
            CAPTURE(to_text);
            CHECK(NetwEntityStage::edge_is_legal(from, to) == LEGAL[from][to]);
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
        CHECK_FALSE(NetwEntityStage::edge_is_legal(stage, stage));
    }
}

TEST_CASE(
    "[Networked][Entity][Hosted] S3 the two terminal stages admit nothing, and "
    "a stage off the table admits nothing either"
) {
    for (int to = 0; to < STAGES; ++to) {
        CHECK_FALSE(
            NetwEntityStage::edge_is_legal(int(EntityStage::TEMPLATE), to)
        );
        CHECK_FALSE(
            NetwEntityStage::edge_is_legal(int(EntityStage::FREED), to)
        );
    }

    // A stage the table does not know is a refusal rather than a crash or a
    // pass, because the value crosses ClassDB as a plain int and a caller can
    // hand over anything.
    CHECK_FALSE(NetwEntityStage::edge_is_legal(99, int(EntityStage::LIVE)));
    CHECK_FALSE(NetwEntityStage::edge_is_legal(-1, int(EntityStage::LIVE)));
    CHECK_FALSE(NetwEntityStage::edge_is_legal(int(EntityStage::LIVE), 99));
    CHECK_FALSE(NetwEntityStage::can_begin_despawn(99));
}

TEST_CASE(
    "[Networked][Entity][Hosted] S4 teardown opens wider than LIVE, so a "
    "hand-bound route is never stranded"
) {
    CHECK(NetwEntityStage::can_begin_despawn(int(EntityStage::UNBOUND)));
    CHECK(NetwEntityStage::can_begin_despawn(int(EntityStage::ARMED)));
    CHECK(NetwEntityStage::can_begin_despawn(int(EntityStage::LIVE)));

    CHECK_FALSE(NetwEntityStage::can_begin_despawn(int(EntityStage::TEMPLATE)));
    CHECK_FALSE(
        NetwEntityStage::can_begin_despawn(int(EntityStage::DESPAWNING))
    );
    CHECK_FALSE(
        NetwEntityStage::can_begin_despawn(int(EntityStage::LINGERING))
    );
    CHECK_FALSE(NetwEntityStage::can_begin_despawn(int(EntityStage::FREED)));

    // Every stage teardown may open from is a stage DESPAWNING is reachable
    // from, or the verb would admit a move the table refuses.
    for (int stage = 0; stage < STAGES; ++stage) {
        if (!NetwEntityStage::can_begin_despawn(stage)) {
            continue;
        }
        NETW_FORMAT_INT(stage_text, stage);
        CAPTURE(stage_text);
        CHECK(NetwEntityStage::edge_is_legal(
            stage,
            int(EntityStage::DESPAWNING)
        ));
    }
}

} // namespace TestNetwEntityStage

#include "support/netw_test.h"

#include "support/netw_cells.h"

#include "godot/variant.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_stats.hpp"

namespace TestNetwSessionStatsLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

constexpr int64_t GATE_ROUTE = 5;
constexpr int64_t ACK_PEER = 4;

enum Plant {
    PLANT_NONE,
    PLANT_A_COUNTER_THE_ENUM_DOES_NOT_NAME,
    PLANT_A_KEY_SPELLED_UNLIKE_ITS_NAME,
    PLANT_A_STAT_READ_FROM_THE_WRONG_KEY,
    PLANT_A_SNAPSHOT_THAT_COUNTS_AS_AN_ACT,
};

struct StatsScenario {
    String label;
    int refusals = 0;
    int malformations = 0;
    int acks = 0;
};

StatsScenario a_quiet_session() {
    StatsScenario scenario;
    scenario.label = "quiet-session";
    return scenario;
}

StatsScenario refused_frames() {
    StatsScenario scenario;
    scenario.label = "refused-frames";
    scenario.refusals = 3;
    return scenario;
}

StatsScenario malformed_intake() {
    StatsScenario scenario;
    scenario.label = "malformed-intake";
    scenario.malformations = 2;
    return scenario;
}

StatsScenario advancing_acks() {
    StatsScenario scenario;
    scenario.label = "advancing-acks";
    scenario.acks = 2;
    return scenario;
}

StatsScenario a_busy_session() {
    StatsScenario scenario;
    scenario.label = "busy-session";
    scenario.refusals = 2;
    scenario.malformations = 1;
    scenario.acks = 1;
    return scenario;
}

struct StatRow {
    const char *name;
    const char *key;
};

const StatRow ENUM_TABLE[] = {
#define NETW_SESSION_STAT_ROW(m_name, m_key) {#m_name, m_key},
    NETW_SESSION_STAT_TABLE(NETW_SESSION_STAT_ROW)
#undef NETW_SESSION_STAT_ROW
};

class StatsRun {
    StatsScenario declared;
    Plant planted = PLANT_NONE;
    Dictionary first;
    Dictionary second;
    PackedInt64Array reads;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

public:
    StatsRun(const StatsScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> core;
        core.instantiate();

        for (int index = 0; index < declared.refusals; index++) {
            core->finish_stage_verdict(
                netw::EventPlane::GATE_SYNC,
                ERR_SKIP,
                GATE_ROUTE
            );
        }
        for (int index = 0; index < declared.malformations; index++) {
            PackedByteArray truncated;
            truncated.push_back(uint8_t(0));
            core->receive_header(GATE_ROUTE, truncated);
        }
        for (int index = 0; index < declared.acks; index++) {
            core->note_peer_ack(ACK_PEER, index + 1, 0);
        }

        first = core->stats_snapshot();
        if (plant_is(PLANT_A_COUNTER_THE_ENUM_DOES_NOT_NAME)) {
            first[StringName("packets_the_enum_forgot")] = 1;
        }
        for (int index = 0; index < int(NetwMultiplayer::STAT_COUNT); index++) {
            const int wrong = (index + 1) % int(NetwMultiplayer::STAT_COUNT);
            const int read = plant_is(PLANT_A_STAT_READ_FROM_THE_WRONG_KEY)
                ? wrong
                : index;
            reads.push_back(int64_t(
                first.get(StringName(ENUM_TABLE[read].key), int64_t(-1))
            ));
        }

        if (plant_is(PLANT_A_SNAPSHOT_THAT_COUNTS_AS_AN_ACT)) {
            core->finish_stage_verdict(
                netw::EventPlane::GATE_SYNC,
                ERR_SKIP,
                GATE_ROUTE
            );
        }
        second = core->stats_snapshot();
    }

    const StatsScenario &scenario() const {
        return declared;
    }

    const Dictionary &snapshot() const {
        return first;
    }

    const Dictionary &retaken() const {
        return second;
    }

    int64_t read_at(int p_index) const {
        return p_index >= 0 && p_index < int(reads.size()) ? reads[p_index]
                                                           : -1;
    }

    String key_at(int p_index) const {
        if (plant_is(PLANT_A_KEY_SPELLED_UNLIKE_ITS_NAME) && p_index == 0) {
            return String("a_key_nobody_would_guess");
        }
        return String(ENUM_TABLE[p_index].key);
    }

    String name_at(int p_index) const {
        return String(ENUM_TABLE[p_index].name);
    }
};

typedef LawRowFor<StatsRun> StatsLaw;

LawVerdict law_total(const StatsRun &p_run) {
    const Dictionary snapshot = p_run.snapshot();
    for (int index = 0; index < int(NetwMultiplayer::STAT_COUNT); index++) {
        const StringName key(p_run.key_at(index));
        if (!snapshot.has(key)) {
            const CharString text = String(key).utf8();
            return law_broken("the snapshot names no %s", text.get_data());
        }
    }
    if (int(snapshot.size()) != int(NetwMultiplayer::STAT_COUNT)) {
        return law_broken(
            "the snapshot holds %d rows for %d stats",
            int(snapshot.size()),
            int(NetwMultiplayer::STAT_COUNT)
        );
    }
    return law_held();
}

LawVerdict law_spelled(const StatsRun &p_run) {
    for (int index = 0; index < int(NetwMultiplayer::STAT_COUNT); index++) {
        const String spelled = p_run.name_at(index).to_lower();
        const String key = p_run.key_at(index);
        if (key != spelled) {
            const CharString key_text = key.utf8();
            const CharString name_text = spelled.utf8();
            return law_broken(
                "the stat spelled %s is keyed %s",
                name_text.get_data(),
                key_text.get_data()
            );
        }
    }
    return law_held();
}

LawVerdict law_agrees(const StatsRun &p_run) {
    const Dictionary snapshot = p_run.snapshot();
    for (int index = 0; index < int(NetwMultiplayer::STAT_COUNT); index++) {
        const int64_t held
            = int64_t(snapshot.get(StringName(p_run.key_at(index)), -1));
        if (p_run.read_at(index) != held) {
            const CharString text = p_run.key_at(index).utf8();
            return law_broken(
                "%s reads %d and the snapshot holds %d",
                text.get_data(),
                int(p_run.read_at(index)),
                int(held)
            );
        }
    }
    return law_held();
}

LawVerdict law_still(const StatsRun &p_run) {
    const Dictionary first = p_run.snapshot();
    const Dictionary second = p_run.retaken();
    if (first.size() != second.size()) {
        return law_broken(
            "a retaken snapshot holds %d rows against %d",
            int(second.size()),
            int(first.size())
        );
    }
    const Array keys = first.keys();
    for (int index = 0; index < keys.size(); index++) {
        const StringName key(keys[index]);
        if (int64_t(first[key]) != int64_t(second.get(key, -1))) {
            const CharString text = String(key).utf8();
            return law_broken(
                "%s moved from %d to %d with nothing between the reads",
                text.get_data(),
                int(int64_t(first[key])),
                int(int64_t(second.get(key, -1)))
            );
        }
    }
    return law_held();
}

const StatsLaw L_TOTAL = {
    "total",
    "the snapshot names every stat the enum names and nothing else",
    &law_total,
};

const StatsLaw L_SPELLED = {
    "spelled",
    "a stat's key is its own constant, lowercased",
    &law_spelled,
};

const StatsLaw L_AGREES = {
    "agrees",
    "reading one stat answers what the whole snapshot holds for it",
    &law_agrees,
};

const StatsLaw L_STILL = {
    "still",
    "taking a snapshot is a read and moves no counter",
    &law_still,
};

const StatsLaw LAWS[] = {L_TOTAL, L_SPELLED, L_AGREES, L_STILL};

TEST_CASE("[Networked][Session][Hosted] the session stat laws hold") {
    const StatsScenario CORPUS[] = {
        a_quiet_session(),
        refused_frames(),
        malformed_intake(),
        advancing_acks(),
        a_busy_session(),
    };
    for (const StatsScenario &scenario : CORPUS) {
        const StatsRun run(scenario);
        REQUIRE(run.snapshot().size() > 0);
        for (const StatsLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] a counter the enum never named reds "
    "total"
) {
    const StatsScenario scenario = a_busy_session();
    const StatsRun run(scenario, PLANT_A_COUNTER_THE_ENUM_DOES_NOT_NAME);
    NETW_CELL(L_TOTAL, scenario);
    NETW_LAW_BREAKS(L_TOTAL, run);
}

TEST_CASE(
    "[Networked][Session][Hosted] a key spelled unlike its constant reds "
    "spelled"
) {
    const StatsScenario scenario = a_busy_session();
    const StatsRun run(scenario, PLANT_A_KEY_SPELLED_UNLIKE_ITS_NAME);
    NETW_CELL(L_SPELLED, scenario);
    NETW_LAW_BREAKS(L_SPELLED, run);
}

TEST_CASE(
    "[Networked][Session][Hosted] a stat read from a neighbour's key "
    "reds agrees"
) {
    const StatsScenario scenario = a_busy_session();
    const StatsRun run(scenario, PLANT_A_STAT_READ_FROM_THE_WRONG_KEY);
    NETW_CELL(L_AGREES, scenario);
    NETW_LAW_BREAKS(L_AGREES, run);
}

TEST_CASE(
    "[Networked][Session][Hosted] a refusal between two reads reds "
    "still"
) {
    const StatsScenario scenario = a_busy_session();
    const StatsRun run(scenario, PLANT_A_SNAPSHOT_THAT_COUNTS_AS_AN_ACT);
    NETW_CELL(L_STILL, scenario);
    NETW_LAW_BREAKS(L_STILL, run);
}

} // namespace TestNetwSessionStatsLaws

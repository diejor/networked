#include "support/netw_test.h"

#include "support/netw_cells.h"

#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwSessionVerdictLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

constexpr int64_t LIVE_ROUTE = 7;
constexpr int64_t ABSENT_ROUTE = 99;
constexpr int64_t SERVER_PEER = 1;
constexpr int64_t CLIENT_PEER = 2;

enum Gate {
    GATE_A_SYNC_FRAME,
    GATE_A_SPAWN_FRAME,
};

enum Situation {
    ROUTE_NOBODY_BOUND,
    ROUTE_RELEASED,
    ROUTE_WHOSE_NODE_LEFT,
    A_SPAWN_FROM_A_CLIENT,
    A_SPAWN_WITH_NO_BODY,
};

enum Plant {
    PLANT_NONE,
    PLANT_A_GATE_ANSWERED_ON_A_LIVE_ROUTE,
};

struct VerdictScenario {
    String label;
    Gate gate = GATE_A_SYNC_FRAME;
    Situation situation = ROUTE_NOBODY_BOUND;
    Error answers = OK;
    NetwMultiplayer::Stat lands_on = NetwMultiplayer::STAT_VERDICT_SKIP;
};

VerdictScenario a_route_nobody_bound() {
    VerdictScenario scenario;
    scenario.label = "a-route-nobody-bound";
    scenario.situation = ROUTE_NOBODY_BOUND;
    scenario.answers = ERR_DOES_NOT_EXIST;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_DOES_NOT_EXIST;
    return scenario;
}

VerdictScenario a_route_the_session_released() {
    VerdictScenario scenario;
    scenario.label = "a-route-the-session-released";
    scenario.situation = ROUTE_RELEASED;
    scenario.answers = ERR_SKIP;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_SKIP;
    return scenario;
}

VerdictScenario a_route_whose_node_left() {
    VerdictScenario scenario;
    scenario.label = "a-route-whose-node-left";
    scenario.situation = ROUTE_WHOSE_NODE_LEFT;
    scenario.answers = ERR_UNAVAILABLE;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_UNAVAILABLE;
    return scenario;
}

VerdictScenario a_spawn_a_client_sent() {
    VerdictScenario scenario;
    scenario.label = "a-spawn-a-client-sent";
    scenario.gate = GATE_A_SPAWN_FRAME;
    scenario.situation = A_SPAWN_FROM_A_CLIENT;
    scenario.answers = ERR_UNAUTHORIZED;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_UNAUTHORIZED;
    return scenario;
}

VerdictScenario a_spawn_carrying_nothing() {
    VerdictScenario scenario;
    scenario.label = "a-spawn-carrying-nothing";
    scenario.gate = GATE_A_SPAWN_FRAME;
    scenario.situation = A_SPAWN_WITH_NO_BODY;
    scenario.answers = ERR_INVALID_DATA;
    scenario.lands_on = NetwMultiplayer::STAT_VERDICT_INVALID_DATA;
    return scenario;
}

const NetwMultiplayer::Stat VERDICT_STATS[] = {
    NetwMultiplayer::STAT_VERDICT_DOES_NOT_EXIST,
    NetwMultiplayer::STAT_VERDICT_SKIP,
    NetwMultiplayer::STAT_VERDICT_UNAVAILABLE,
    NetwMultiplayer::STAT_VERDICT_UNAUTHORIZED,
    NetwMultiplayer::STAT_VERDICT_INVALID_DATA,
    NetwMultiplayer::STAT_VERDICT_BUSY,
};

class VerdictRun {
    VerdictScenario declared;
    Plant planted = PLANT_NONE;
    Error answered = OK;
    int64_t stats[6] = {0, 0, 0, 0, 0, 0};
    int64_t legacy = 0;
    Error ticked = FAILED;

public:
    VerdictRun(const VerdictScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> session;
        session.instantiate();
        session->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);

        Node3D *body = memnew(Node3D);
        netw::gd::scene_root()->add_child(body);
        const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
        session->liveness_bind_route(LIVE_ROUTE, wrapper.ptr());

        PackedByteArray payload;
        payload.push_back(1);
        int64_t route = LIVE_ROUTE;
        int64_t sender = CLIENT_PEER;

        switch (declared.situation) {
            case ROUTE_NOBODY_BOUND:
                route = ABSENT_ROUTE;
                break;
            case ROUTE_RELEASED: {
                PackedInt64Array released;
                released.push_back(LIVE_ROUTE);
                session->liveness_release_routes(released);
            } break;
            case ROUTE_WHOSE_NODE_LEFT:
                wrapper->set_owner(nullptr);
                break;
            case A_SPAWN_FROM_A_CLIENT:
                sender = CLIENT_PEER;
                break;
            case A_SPAWN_WITH_NO_BODY:
                sender = SERVER_PEER;
                payload = PackedByteArray();
                break;
        }
        if (planted == PLANT_A_GATE_ANSWERED_ON_A_LIVE_ROUTE) {
            route = LIVE_ROUTE;
            sender = SERVER_PEER;
            payload.clear();
            payload.push_back(1);
        }

        answered = declared.gate == GATE_A_SPAWN_FRAME
            ? session->spawn_admit_frame_default(
                  sender,
                  0,
                  netw::wire::builtin_channel("SPAWN"),
                  payload
              )
            : session->sync_admit_frame_default(
                  sender,
                  route,
                  0,
                  netw::wire::builtin_channel("SYNC"),
                  0,
                  -1,
                  payload
              );

        session->finish_stage_verdict(
            netw::EventPlane::GATE_SYNC,
            answered,
            route
        );

        for (int index = 0; index < 6; index++) {
            stats[index] = session->stats_get(VERDICT_STATS[index]);
        }
        ticked = session->session_flush_tick(LIVE_ROUTE);

        wrapper->set_owner(body);
        session->clear_session_state();
        body->queue_free();
    }

    const VerdictScenario &scenario() const {
        return declared;
    }

    Error verdict() const {
        return answered;
    }

    int64_t stat_at(int p_index) const {
        return stats[p_index];
    }

    Error tick() const {
        return ticked;
    }
};

typedef LawRowFor<VerdictRun> VerdictLaw;

LawVerdict law_answers(const VerdictRun &p_run) {
    if (p_run.verdict() != p_run.scenario().answers) {
        return law_broken(
            "the gate answered %d where the situation names %d",
            int(p_run.verdict()),
            int(p_run.scenario().answers)
        );
    }
    return law_held();
}

LawVerdict law_partitioned(const VerdictRun &p_run) {
    for (int index = 0; index < 6; index++) {
        const int64_t expected
            = VERDICT_STATS[index] == p_run.scenario().lands_on ? 1 : 0;
        if (p_run.stat_at(index) != expected) {
            return law_broken(
                "verdict stat %d reads %d against %d",
                index,
                int(p_run.stat_at(index)),
                int(expected)
            );
        }
    }
    return law_held();
}

LawVerdict law_serviced(const VerdictRun &p_run) {
    if (p_run.tick() != OK) {
        return law_broken(
            "the tick door answered %d after a refused frame",
            int(p_run.tick())
        );
    }
    return law_held();
}

const VerdictLaw L_ANSWERS = {
    "answers",
    "a gate names the one refusal the situation earns",
    &law_answers,
};

const VerdictLaw L_PARTITIONED = {
    "partitioned",
    "staging a gate answer moves that verdict's own counter and no other",
    &law_partitioned,
};

const VerdictLaw L_SERVICED = {
    "serviced",
    "a refused frame leaves the session's own tick door open",
    &law_serviced,
};

const VerdictLaw LAWS[] = {L_ANSWERS, L_PARTITIONED, L_SERVICED};

TEST_CASE("[Networked][Session][SceneTree] the gate verdict laws hold") {
    const VerdictScenario CORPUS[] = {
        a_route_nobody_bound(),
        a_route_the_session_released(),
        a_route_whose_node_left(),
        a_spawn_a_client_sent(),
        a_spawn_carrying_nothing(),
    };
    for (const VerdictScenario &scenario : CORPUS) {
        const VerdictRun run(scenario);
        for (const VerdictLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a frame the gate admits reds "
    "answers"
) {
    const VerdictScenario scenario = a_route_nobody_bound();
    const VerdictRun run(scenario, PLANT_A_GATE_ANSWERED_ON_A_LIVE_ROUTE);
    NETW_CELL(L_ANSWERS, scenario);
    NETW_LAW_BREAKS(L_ANSWERS, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a frame the gate admits reds "
    "partitioned"
) {
    const VerdictScenario scenario = a_route_nobody_bound();
    const VerdictRun run(scenario, PLANT_A_GATE_ANSWERED_ON_A_LIVE_ROUTE);
    NETW_CELL(L_PARTITIONED, scenario);
    NETW_LAW_BREAKS(L_PARTITIONED, run);
}

} // namespace TestNetwSessionVerdictLaws

#include "support/netw_test.h"

#include "support/minted_script.h"

#include "support/declared_nodes.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/display/decl.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwDisplayPredictionRoleLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionHandle;
using netw::sim::Mode;
using netw_test::FlowPair;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

enum Plant {
    PLANT_NONE,
    PLANT_NOBODY_REGISTERED_THE_ENGINE,
};

struct PredictionScenario {
    String label;
    bool steers = true;
    bool closed = false;
    bool replicas_active = false;
    NetwMultiplayer::LiveMode live_mode = NetwMultiplayer::LIVE_MODE_CHASE;
    Mode mode = Mode::PREDICT;
    int64_t role = NetwMultiplayer::DISPLAY_ROLE_PREDICTED;
};

PredictionScenario a_predicting_controller() {
    PredictionScenario scenario;
    scenario.label = "a-predicting-controller";
    return scenario;
}

PredictionScenario a_closed_controller() {
    PredictionScenario scenario;
    scenario.label = "a-closed-controller";
    scenario.closed = true;
    scenario.mode = Mode::PROXY;
    scenario.role = NetwMultiplayer::DISPLAY_ROLE_REMOTE;
    return scenario;
}

PredictionScenario an_active_copy() {
    PredictionScenario scenario;
    scenario.label = "an-active-copy";
    scenario.steers = false;
    scenario.replicas_active = true;
    scenario.live_mode = NetwMultiplayer::LIVE_MODE_BRACKETED;
    scenario.mode = Mode::ACTIVE;
    return scenario;
}

PredictionScenario a_proxy_copy() {
    PredictionScenario scenario;
    scenario.label = "a-proxy-copy";
    scenario.steers = false;
    scenario.mode = Mode::PROXY;
    scenario.role = NetwMultiplayer::DISPLAY_ROLE_REMOTE;
    return scenario;
}

struct Evidence {
    int64_t server_role = -1;
    int64_t client_role = -1;
    int64_t client_pump = -1;
    int64_t client_mode = -1;
    bool registered = false;
    bool controlled_locally = false;
};

class PredictionRoleRun {
    PredictionScenario declared;
    Plant planted = PLANT_NONE;
    Evidence read;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    int64_t stat_of(
        NetwMultiplayer *p_core,
        const Ref<NetwEntity> &p_entity,
        const char *p_stat
    ) const {
        p_core->display_mark_dirty(
            p_entity->get_rid_handle(),
            netw::display::DIRT_ROLE
        );
        p_core->session_flush_deferred();
        return int64_t(p_core->display_get_track_stat(
            p_entity->get_rid_handle(),
            StringName(),
            StringName(p_stat)
        ));
    }

public:
    explicit PredictionRoleRun(
        const PredictionScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        LoopbackRig rig(1);
        rig.declare_world(WorldDecl().clocked(30).lag_compensated());
        rig.mount();
        NetwMultiplayer *server = netw_test::flow_core(rig.server());
        NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
        const FlowPair pair = netw_test::stand_flow_pair(
            rig,
            netw_test::gdsrc::INTERPOLATED_STATE_AND_INPUT,
            "PredictedPlayer"
        );
        netw_test::steer(pair, declared.steers ? int(rig.peer_id(0)) : 0);
        const Ref<NetwEntity> on_server = NetwEntity::of(pair.authored);
        const Ref<NetwEntity> on_client = NetwEntity::of(pair.mirror(0));
        REQUIRE(on_server.is_valid());
        REQUIRE(on_client.is_valid());
        on_client->get_interpolation()->set_live_mode(declared.live_mode);
        const RID rid = on_client->get_rid_handle();

        if (declared.replicas_active) {
            client->sim_row(rid).declaration.replicas
                = netw::sim::Replicas::ACTIVE;
        }
        if (!plant_is(PLANT_NOBODY_REGISTERED_THE_ENGINE)) {
            NETW_CHECK_EQ(int(client->predict_declare(rid)), int(OK));
        }
        const Ref<NetwPredictionHandle> prediction
            = on_client->get_prediction();
        REQUIRE(prediction.is_valid());
        if (declared.closed) {
            prediction->set_recovery_policy(
                NetwPredict::RECOVERY_POLICY_DELAY_CLOSED
            );
        }
        read.server_role = stat_of(server, on_server, "role");
        read.client_role = stat_of(client, on_client, "role");
        read.client_pump = stat_of(client, on_client, "pump_mode");

        read.registered = prediction->is_registered();
        read.controlled_locally = on_client->get_is_controlled_locally();
        const netw::sim::Row *row = client->sim_row_of(rid);
        read.client_mode = row != nullptr ? int64_t(row->mode) : -1;
    }

    PredictionRoleRun(const PredictionRoleRun &) = delete;
    PredictionRoleRun &operator=(const PredictionRoleRun &) = delete;

    const PredictionScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return read;
    }
};

typedef LawRowFor<PredictionRoleRun> PredictionRoleLaw;

LawVerdict law_engine_is_live(const PredictionRoleRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (!read.registered) {
        return law_broken(
            "the declaration registered no engine, so the mode under test is "
            "never resolved"
        );
    }
    if (read.controlled_locally != p_run.scenario().steers) {
        return law_broken(
            "the peer under test steers %d where the world declares %d",
            int(read.controlled_locally),
            int(p_run.scenario().steers)
        );
    }
    if (read.client_mode != int64_t(p_run.scenario().mode)) {
        return law_broken(
            "the peer resolved mode %d where the world declares %d",
            int(read.client_mode),
            int(p_run.scenario().mode)
        );
    }
    return law_held();
}

LawVerdict law_mode_draws(const PredictionRoleRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.client_role != p_run.scenario().role) {
        return law_broken(
            "the peer under test displays role %d, owed %d",
            int(read.client_role),
            int(p_run.scenario().role)
        );
    }
    return law_held();
}

LawVerdict law_live_pump(const PredictionRoleRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.client_role != NetwMultiplayer::DISPLAY_ROLE_PREDICTED) {
        return law_held();
    }
    const int64_t owed
        = p_run.scenario().live_mode == NetwMultiplayer::LIVE_MODE_BRACKETED
        ? int64_t(NetwMultiplayer::DISPLAY_PUMP_BRACKETED)
        : int64_t(NetwMultiplayer::DISPLAY_PUMP_CHASE);
    if (read.client_pump != owed) {
        return law_broken(
            "a live copy pumps %d where its live mode asks for %d",
            int(read.client_pump),
            int(owed)
        );
    }
    return law_held();
}

LawVerdict law_authority_peer(const PredictionRoleRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.server_role != NetwMultiplayer::DISPLAY_ROLE_AUTHORITY) {
        return law_broken(
            "the authority displays role %d rather than the simulation it "
            "runs",
            int(read.server_role)
        );
    }
    return law_held();
}

const PredictionRoleLaw L_LIVE = {
    "live",
    "the mode under test is resolved by a registered engine on a peer that "
    "steers or watches as declared, not from a fact set written by hand",
    &law_engine_is_live,
};

const PredictionRoleLaw L_MODE_DRAWS = {
    "mode-draws",
    "the auto role derives from the resolved mode. A mode that runs the body "
    "here draws it live, and a proxy draws it remote",
    &law_mode_draws,
};

const PredictionRoleLaw L_LIVE_PUMP = {
    "live-pump",
    "a body drawn live pumps what live_mode names, whether this peer predicts "
    "it or runs a copy",
    &law_live_pump,
};

const PredictionRoleLaw L_AUTHORITY = {
    "authority",
    "the peer holding authority over what another peer steers displays the "
    "simulation it runs",
    &law_authority_peer,
};

const PredictionRoleLaw LAWS[] = {
    L_LIVE,
    L_MODE_DRAWS,
    L_LIVE_PUMP,
    L_AUTHORITY,
};

TEST_CASE(
    "[Networked][Display][SceneTree] the display role derives from the "
    "resolved mode against a real engine"
) {
    const PredictionScenario CORPUS[] = {
        a_predicting_controller(),
        a_closed_controller(),
        an_active_copy(),
        a_proxy_copy(),
    };
    for (const PredictionScenario &scenario : CORPUS) {
        const PredictionRoleRun run(scenario);
        for (const PredictionRoleLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] an engine nobody declared reds "
    "live"
) {
    const PredictionScenario scenario = a_predicting_controller();
    const PredictionRoleRun run(scenario, PLANT_NOBODY_REGISTERED_THE_ENGINE);
    NETW_CELL(L_LIVE, scenario);
    NETW_LAW_BREAKS(L_LIVE, run);
}

} // namespace TestNetwDisplayPredictionRoleLaws

#endif

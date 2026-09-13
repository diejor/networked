#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/netw_cells.h"

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/display/decl.hpp"
#include "netw/property_set_builder.hpp"

namespace TestNetwDisplayPredictionRoleLaws {

using namespace godot;
using netw::Netw;
using netw::NetwDisplayHandle;
using netw::NetwEntity;
using netw::NetwInterpolate;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionHandle;
using netw::NetwPropertyConfig;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum Plant {
    PLANT_NONE,
    PLANT_NOBODY_REGISTERED_THE_ENGINE,
};

struct PredictionScenario {
    String label;
    bool demoted = false;
};

PredictionScenario a_simulating_engine() {
    PredictionScenario scenario;
    scenario.label = "a-simulating-engine";
    return scenario;
}

PredictionScenario an_engine_demoted_to_display() {
    PredictionScenario scenario;
    scenario.label = "an-engine-demoted-to-display";
    scenario.demoted = true;
    return scenario;
}

struct Evidence {
    int64_t server_role = -1;
    int64_t client_role = -1;
    bool registered = false;
    bool controlled_locally = false;
};

const int64_t SUBJECT_ROUTE = 31;

class PredictionRoleRun {
    PredictionScenario declared;
    Plant planted = PLANT_NONE;
    Evidence read;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    Ref<NetwEntity> stand_up(
        NetwMultiplayer *p_core,
        Node *p_branch,
        int64_t p_controller
    ) const {
        Node2D *body = memnew(Node2D);
        body->set_name("PredictedPlayer");
        p_branch->add_child(body);

        Ref<NetwPropertyConfig> config
            = Netw::configure_property(body, StringName("position"), true);
        Ref<NetwInterpolate> spec;
        spec.instantiate();
        config->interpolate(
            netw::gd::array_of(
                spec->lerp()->smooth(0.0)->to(StringName("position"))
            )
        );
        config->state();

        Dictionary configs;
        configs[StringName("position")] = config;
        const Ref<netw::NetwPropertySet> set
            = netw::property_set_builder::from_property_configs(
                configs,
                netw::NetwPropertySet::RECORD_STATE
            );
        REQUIRE(set.is_valid());
        set->set_sealed(true);
        const Ref<NetwEntity> entity = NetwEntity::ensure(body);
        NETW_CHECK_EQ(
            int(p_core->sync_pipeline()->register_property_set(body, set)),
            int(OK)
        );
        entity->set_controller(p_controller);
        p_core->liveness_bind_route(SUBJECT_ROUTE, entity.ptr());
        return entity;
    }

    int64_t role_of(
        NetwMultiplayer *p_core,
        const Ref<NetwEntity> &p_entity
    ) const {
        p_core->display_mark_dirty(
            p_entity->get_rid_handle(),
            netw::display::DIRT_ROLE
        );
        p_core->session_flush_deferred();
        return int64_t(p_core->display_get_track_stat(
            p_entity->get_rid_handle(),
            StringName(),
            StringName("role")
        ));
    }

public:
    explicit PredictionRoleRun(
        const PredictionScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        netw_test::LoopbackRig rig(1);
        rig.mount();
        NetwMultiplayer *server
            = Object::cast_to<NetwMultiplayer>(rig.server());
        NetwMultiplayer *client
            = Object::cast_to<NetwMultiplayer>(rig.client(0));
        REQUIRE(server != nullptr);
        REQUIRE(client != nullptr);
        client->lagcomp_initialize(8, 12);
        const int64_t controller = int64_t(rig.peer_id(0));

        const Ref<NetwEntity> on_server
            = stand_up(server, rig.branch(-1), controller);
        const Ref<NetwEntity> on_client
            = stand_up(client, rig.branch(0), controller);

        if (!plant_is(PLANT_NOBODY_REGISTERED_THE_ENGINE)) {
            NETW_CHECK_EQ(
                int(client->predict_declare(on_client->get_rid_handle())),
                int(OK)
            );
        }
        const Ref<NetwPredictionHandle> prediction
            = on_client->get_prediction();
        REQUIRE(prediction.is_valid());
        prediction->set_input_source(NetwPredict::INPUT_SOURCE_PREDICTED);
        prediction->set_sim_mode(
            declared.demoted ? NetwPredict::SIM_MODE_DISPLAY
                             : NetwPredict::SIM_MODE_SPECULATIVE
        );
        read.registered = prediction->is_registered();
        read.controlled_locally = on_client->get_is_controlled_locally();

        read.server_role = role_of(server, on_server);
        read.client_role = role_of(client, on_client);
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
            "the declaration registered no engine, so the rung under test is "
            "never reached"
        );
    }
    if (!read.controlled_locally) {
        return law_broken(
            "the peer under test does not steer the entity, so its rung is a "
            "different one"
        );
    }
    return law_held();
}

LawVerdict law_predicting_peer(const PredictionRoleRun &p_run) {
    const Evidence &read = p_run.evidence();
    const int64_t owed = p_run.scenario().demoted
        ? int64_t(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
        : int64_t(NetwMultiplayer::DISPLAY_ROLE_PREDICTED);
    if (read.client_role != owed) {
        return law_broken(
            "the steering peer displays role %d, owed %d",
            int(read.client_role),
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
            "consumes",
            int(read.server_role)
        );
    }
    return law_held();
}

const PredictionRoleLaw L_LIVE = {
    "live",
    "the rung under test is reached from a registered engine on the peer that "
    "steers the entity, not from a fact set written by hand",
    &law_engine_is_live,
};

const PredictionRoleLaw L_PREDICTING = {
    "predicting",
    "a peer whose registered engine still simulates displays its prediction, "
    "and one demoted to the display mode falls to remote rather than going "
    "dark, because nothing else writes the display once the simulation stops",
    &law_predicting_peer,
};

const PredictionRoleLaw L_AUTHORITY = {
    "authority",
    "the peer holding authority over what it steers displays the simulation "
    "it consumes",
    &law_authority_peer,
};

const PredictionRoleLaw LAWS[] = {L_LIVE, L_PREDICTING, L_AUTHORITY};

TEST_CASE(
    "[Networked][Display][SceneTree] the prediction rung of the role ladder "
    "holds against a real engine"
) {
    const PredictionScenario CORPUS[] = {
        a_simulating_engine(),
        an_engine_demoted_to_display(),
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
    const PredictionScenario scenario = a_simulating_engine();
    const PredictionRoleRun run(scenario, PLANT_NOBODY_REGISTERED_THE_ENGINE);
    NETW_CELL(L_LIVE, scenario);
    NETW_LAW_BREAKS(L_LIVE, run);
}

} // namespace TestNetwDisplayPredictionRoleLaws

#endif

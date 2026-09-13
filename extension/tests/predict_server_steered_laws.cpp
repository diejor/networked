#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"

namespace TestNetwPredictServerSteered {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwPredict;
using netw::NetwPredictionHandle;

struct Axes {
    NetwPredict::InputSource input_source;
    NetwPredict::SimMode sim_mode;
    NetwPredict::Role role;
    int64_t controller;
};

EntityDecl a_predicted_body(const char *p_name) {
    return EntityDecl()
        .named(StringName(p_name))
        .on_schema(StringName("SteeredPose"))
        .synced(StringName("position"))
        .placed_at(Vector2())
        .predicted(0.01);
}

void arm_rewind(LoopbackRig &p_rig) {
    NETW_CHECK_EQ(int(p_rig.server()->lagcomp_initialize(8, 12)), int(OK));
}

Axes axes_on_the_authority(LoopbackRig &p_rig, const EntityDecl &p_decl) {
    const RID entity = p_rig.declare_entity(p_decl);
    REQUIRE_MESSAGE(entity.is_valid(), "the declaration minted no entity");
    p_rig.pump(6);

    const Ref<NetwEntity> seated = p_rig.server()->entity_get_view(entity);
    REQUIRE_MESSAGE(seated.is_valid(), "the server holds no view of it");
    const Ref<NetwPredictionHandle> prediction = seated->get_prediction();
    REQUIRE_MESSAGE(prediction.is_valid(), "the entity carries no handle");

    Axes read;
    read.controller = seated->get_controller();
    read.input_source
        = NetwPredict::InputSource(prediction->get_input_source());
    read.sim_mode = NetwPredict::SimMode(prediction->get_sim_mode());
    read.role
        = NetwPredictionHandle::role_for_axes(read.input_source, read.sim_mode);
    return read;
}

TEST_CASE(
    "[Networked][Predict] SS1 a server-controlled body is authored "
    "locally by the server rather than seated as a consumer, because no peer "
    "will ever send the command stream a consumer waits on and the entity "
    "would never be driven at all"
) {
    LoopbackRig rig(1);
    rig.mount();
    arm_rewind(rig);

    const Axes read = axes_on_the_authority(rig, a_predicted_body("Ball"));

    NETW_CHECK_EQ(read.controller, int64_t(0));
    NETW_CHECK_EQ(int(read.input_source), int(NetwPredict::INPUT_SOURCE_LOCAL));
    NETW_CHECK_EQ(int(read.sim_mode), int(NetwPredict::SIM_MODE_AUTHORITATIVE));
    NETW_CHECK_EQ(int(read.role), int(NetwPredict::ROLE_HOST_LOCAL));
}

TEST_CASE(
    "[Networked][Predict] SS2 a peer-controlled body still consumes "
    "on the server, which is the half SS1 must not take with it"
) {
    LoopbackRig rig(1);
    rig.mount();
    arm_rewind(rig);
    rig.join(0, StringName("alpha"));

    const Axes read = axes_on_the_authority(
        rig,
        EntityDecl(a_predicted_body("Car")).controlled_by(rig.peer_id(0))
    );

    NETW_CHECK_EQ(read.controller, int64_t(rig.peer_id(0)));
    NETW_CHECK_EQ(
        int(read.input_source),
        int(NetwPredict::INPUT_SOURCE_RECEIVED)
    );
    NETW_CHECK_EQ(int(read.sim_mode), int(NetwPredict::SIM_MODE_AUTHORITATIVE));
    NETW_CHECK_EQ(int(read.role), int(NetwPredict::ROLE_CONSUME));
}

} // namespace TestNetwPredictServerSteered

#endif

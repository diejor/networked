#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwSimSessionAuthority {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPredict;
using netw::NetwPredictionHandle;
using netw::sim::Mode;

struct Read {
    Mode mode = Mode::NONE;
    int role = -1;
};

Read read_of(NetwMultiplayer *p_session, const RID &p_entity) {
    Read read;
    const netw::sim::Row *row = p_session->sim_row_of(p_entity);
    if (row != nullptr) {
        read.mode = row->mode;
    }
    const Ref<NetwEntity> seated = p_session->entity_get_view(p_entity);
    const Ref<NetwPredictionHandle> handle = seated.is_valid()
        ? seated->get_prediction()
        : Ref<NetwPredictionHandle>();
    if (handle.is_valid()) {
        read.role = netw::NetwPredictionEngine::role_for_axes(
            int(handle->get_input_source()),
            int(handle->get_sim_mode())
        );
    }
    return read;
}

TEST_CASE(
    "[Networked][Sim][Law] a peer that stops holding session authority "
    "re-resolves the entities it ran, and resolves them back when it holds "
    "it again"
) {
    LoopbackRig rig(1);
    rig.mount();
    NETW_CHECK_EQ(int(rig.server()->lagcomp_initialize(8, 12)), int(OK));
    const RID entity = rig.declare_entity(
        EntityDecl()
            .named(StringName("Ball"))
            .on_schema(StringName("SessionAuthorityPose"))
            .synced(StringName("position"))
            .placed_at(Vector2())
            .predicted(0.01)
    );
    REQUIRE(entity.is_valid());
    rig.pump(6);

    NetwMultiplayer *server = rig.server();
    const NetwMultiplayer::Role held = server->session_get_role();
    REQUIRE(server->is_host());
    const Read before = read_of(server, entity);
    NETW_CHECK_EQ(int(before.mode), int(Mode::AUTHORITY));
    NETW_CHECK_EQ(before.role, int(NetwPredict::ROLE_HOST_LOCAL));

    server->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    REQUIRE_FALSE(server->is_host());
    const Read demoted = read_of(server, entity);
    NETW_CHECK_EQ(int(demoted.mode), int(Mode::PROXY));
    NETW_CHECK_EQ(demoted.role, int(NetwPredict::ROLE_REMOTE));

    server->session_set_role(held);
    REQUIRE(server->is_host());
    const Read restored = read_of(server, entity);
    NETW_CHECK_EQ(int(restored.mode), int(Mode::AUTHORITY));
    NETW_CHECK_EQ(restored.role, int(NetwPredict::ROLE_HOST_LOCAL));
}

} // namespace TestNetwSimSessionAuthority

#endif

#include "support/netw_test.h"

#include <godot_cpp/classes/audio_stream_player.hpp>
#include <godot_cpp/classes/node2d.hpp>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/entity_decl.h"
#include "support/loopback_rig.h"

namespace TestNetwCoordinatorPerception {

using namespace godot;
using netw::NetwEntity;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw_test::EntityDecl;
using netw_test::LoopbackRig;

constexpr int64_t COORDINATOR = 7;
constexpr int64_t LOCAL_VIEW = 1;

TEST_CASE(
    "[Networked][Interest] L1 an entity hidden from a session whose "
    "coordinator is 7 stays alive in the tree, only its visibility and "
    "volume snapshotted and forced down, restoring puts both back rather "
    "than freeing anything, and once this same transport peer 1 is no "
    "longer the authority it reads its own projected declaration rather "
    "than the bitmask only the authority keeps"
) {
    LoopbackRig rig(1, int(COORDINATOR));
    NetwMultiplayer *core = rig.server();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    REQUIRE(core->is_host());
    NETW_CHECK_EQ(core->session_authority_peer(), COORDINATOR);

    const RID handle = rig.declare_entity(
        EntityDecl().named("CoordinatedSubject").on_route(88)
    );
    const Ref<NetwEntity> entity = core->entity_get_view(handle);
    Node2D *owner = Object::cast_to<Node2D>(rig.node_of(handle));
    REQUIRE(owner != nullptr);
    AudioStreamPlayer *speaker = memnew(AudioStreamPlayer);
    speaker->set_name("Speaker");
    speaker->set_volume_db(-12.0);
    owner->add_child(speaker);

    const Ref<NetwInterestLayer> layer
        = core->interest_layer(StringName("coordinator_stealth"));
    REQUIRE(layer.is_valid());
    layer->add_entity(entity);
    rig.flush_interest();

    NETW_CHECK_EQ(core->interest_participant_sees(LOCAL_VIEW, entity), false);
    CHECK_FALSE(owner->is_visible());
    NETW_CHECK_CLOSE(double(speaker->get_volume_db()), -80.0, 0.001);
    CHECK(core->entity_get_node(handle) == owner);

    layer->add_viewer(LOCAL_VIEW);
    rig.flush_interest();

    NETW_CHECK_EQ(core->interest_participant_sees(LOCAL_VIEW, entity), true);
    CHECK(owner->is_visible());
    NETW_CHECK_CLOSE(double(speaker->get_volume_db()), -12.0, 0.001);
    CHECK(core->entity_get_node(handle) == owner);

    layer->remove_viewer(LOCAL_VIEW);
    rig.flush_interest();
    NETW_CHECK_EQ(core->interest_participant_sees(LOCAL_VIEW, entity), false);

    core->session_set_role(NetwMultiplayer::ROLE_CLIENT);
    REQUIRE(core->is_server());
    REQUIRE_FALSE(core->is_host());

    NETW_CHECK_EQ(core->interest_participant_sees(LOCAL_VIEW, entity), true);
}

} // namespace TestNetwCoordinatorPerception

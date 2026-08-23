#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/entity_control.hpp"

namespace TestEntityControlLaws {

using namespace godot;
using netw::NetwEntity;

struct ArmedNode {
    Node *owner = nullptr;
    Ref<NetwEntity> entity;

    ArmedNode(const char *p_id, int64_t p_peer, int p_initial) {
        owner = memnew(Node);
        NetwEntity::bind(owner, p_id, p_peer);
        entity = NetwEntity::of(owner);
        REQUIRE(entity.is_valid());
        entity->set_initial_controller(p_initial);
        entity->arm(nullptr);
    }

    int64_t authority() const { return owner->get_multiplayer_authority(); }

    ~ArmedNode() { memdelete(owner); }
};

TEST_CASE(
    "[Networked][Entity][Hosted] EC1 arming a represented-peer entity makes "
    "the peer its name spells the controller and the node authority at once"
) {
    ArmedNode armed(
        "valeria",
        42,
        int(netw::InitialController::REPRESENTED_PEER)
    );

    NETW_CHECK_EQ(armed.entity->get_controller(), 42);
    NETW_CHECK_EQ(armed.authority(), 42);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_PEER_CONTROLLED)
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC2 arming a server-controlled entity keeps "
    "authority at the server however its name reads"
) {
    ArmedNode armed("valeria", 42, int(netw::InitialController::SERVER));

    NETW_CHECK_EQ(armed.entity->get_controller(), 0);
    NETW_CHECK_EQ(armed.authority(), 1);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_SERVER_CONTROLLED)
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC3 a represented-peer entity naming no peer "
    "arms to the server"
) {
    ArmedNode armed(
        "valeria",
        0,
        int(netw::InitialController::REPRESENTED_PEER)
    );

    NETW_CHECK_EQ(armed.entity->get_controller(), 0);
    NETW_CHECK_EQ(armed.authority(), 1);
}

TEST_CASE(
    "[Networked][Entity][Hosted] EC4 a grant moves the controller and the "
    "node authority as one act, and a revoke returns both to the server"
) {
    ArmedNode armed(
        "valeria",
        0,
        int(netw::InitialController::REPRESENTED_PEER)
    );

    armed.entity->grant_control(42);

    NETW_CHECK_EQ(armed.entity->get_controller(), 42);
    NETW_CHECK_EQ(armed.authority(), 42);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_PEER_CONTROLLED)
    );

    armed.entity->revoke_control();

    NETW_CHECK_EQ(armed.entity->get_controller(), 0);
    NETW_CHECK_EQ(armed.authority(), 1);
    NETW_CHECK_EQ(
        armed.entity->get_control_kind(),
        int64_t(NetwEntity::CONTROL_SERVER_CONTROLLED)
    );
}

} // namespace TestEntityControlLaws

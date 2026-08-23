#include "support/netw_test.h"

#include "netw/entity_control.hpp"

using namespace godot;

namespace TestNetwEntityControl {

using godot::Ref;
using netw::DisconnectRule;
using netw::InitialController;
using netw::NetwEntityControl;
using netw::Transfer;

constexpr int64_t REPRESENTED = 42;
constexpr int64_t OTHER = 7;

Ref<NetwEntityControl> fresh() {
    Ref<NetwEntityControl> control;
    control.instantiate();
    return control;
}

TEST_CASE(
    "[Networked][Entity][Hosted] C1 an unwritten controller answers the rule, "
    "and a write answers itself"
) {
    Ref<NetwEntityControl> control = fresh();
    NETW_CHECK_EQ(control->resolve(REPRESENTED), 0);

    control->initial = int(InitialController::REPRESENTED_PEER);
    NETW_CHECK_EQ(control->resolve(REPRESENTED), REPRESENTED);

    CHECK_FALSE(control->configured);
    control->set_controller(OTHER);
    CHECK(control->configured);
    NETW_CHECK_EQ(control->resolve(REPRESENTED), OTHER);

    control->set_controller(0);
    NETW_CHECK_EQ(control->resolve(REPRESENTED), 0);
}

TEST_CASE(
    "[Networked][Entity][Hosted] C2 a controller write reports whether it "
    "moved, so a redundant one announces nothing"
) {
    Ref<NetwEntityControl> control = fresh();

    CHECK(control->set_controller(REPRESENTED));
    CHECK_FALSE(control->set_controller(REPRESENTED));
    CHECK(control->set_controller(0));
    CHECK_FALSE(control->set_controller(0));
}

TEST_CASE(
    "[Networked][Entity][Hosted] C3 the local peer steers only what it is "
    "resolved to steer, and no session steers nothing"
) {
    Ref<NetwEntityControl> control = fresh();
    control->initial = int(InitialController::REPRESENTED_PEER);

    CHECK(control->controlled_by(REPRESENTED, REPRESENTED));
    CHECK_FALSE(control->controlled_by(OTHER, REPRESENTED));

    CHECK_FALSE(control->controlled_by(0, REPRESENTED));
    control->set_controller(0);
    CHECK_FALSE(control->controlled_by(REPRESENTED, REPRESENTED));
}

TEST_CASE(
    "[Networked][Entity][Hosted] C4 a fixed entity refuses every request and a "
    "requestable one admits"
) {
    Ref<NetwEntityControl> control = fresh();
    CHECK_FALSE(control->admits_request());

    control->transfer = int(Transfer::REQUESTABLE);
    CHECK(control->admits_request());
}

TEST_CASE(
    "[Networked][Entity][Hosted] C5 representation outranks control when its "
    "peer disconnects"
) {
    Ref<NetwEntityControl> control = fresh();
    control->set_controller(REPRESENTED);

    NETW_CHECK_EQ(
        control->disconnect_verdict(REPRESENTED, REPRESENTED),
        NetwEntityControl::DESPAWN_REPRESENTED
    );
    control->on_disconnect = int(DisconnectRule::DESPAWN);
    NETW_CHECK_EQ(
        control->disconnect_verdict(REPRESENTED, REPRESENTED),
        NetwEntityControl::DESPAWN_REPRESENTED
    );

    Ref<NetwEntityControl> npc = fresh();
    npc->set_controller(OTHER);
    NETW_CHECK_EQ(
        npc->disconnect_verdict(OTHER, 0),
        NetwEntityControl::REVERT_TO_SERVER
    );
    npc->on_disconnect = int(DisconnectRule::DESPAWN);
    NETW_CHECK_EQ(
        npc->disconnect_verdict(OTHER, 0),
        NetwEntityControl::DESPAWN_CONTROLLER
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] C6 a disconnect that reaches neither role "
    "asks for nothing"
) {
    Ref<NetwEntityControl> control = fresh();
    control->set_controller(REPRESENTED);
    control->on_disconnect = int(DisconnectRule::DESPAWN);

    NETW_CHECK_EQ(
        control->disconnect_verdict(OTHER, REPRESENTED),
        NetwEntityControl::NOTHING
    );

    NETW_CHECK_EQ(
        fresh()->disconnect_verdict(0, 0),
        NetwEntityControl::NOTHING
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] the write policy answers the same question "
    "for a receiver and for an author"
) {
    const int64_t AUTHORITY_PEER = 1;
    const int64_t CONTROLLER_PEER = REPRESENTED;

    CHECK(NetwEntityControl::policy_admits(
        int(netw::WritePolicy::AUTHORITY),
        AUTHORITY_PEER,
        AUTHORITY_PEER,
        CONTROLLER_PEER
    ));
    CHECK_FALSE(NetwEntityControl::policy_admits(
        int(netw::WritePolicy::AUTHORITY),
        CONTROLLER_PEER,
        AUTHORITY_PEER,
        CONTROLLER_PEER
    ));

    SUBCASE("a controller policy answers for the controller and nobody else") {
        CHECK(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::CONTROLLER),
            CONTROLLER_PEER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        ));
        CHECK_FALSE(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::CONTROLLER),
            OTHER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        ));
        CHECK_FALSE(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::CONTROLLER),
            AUTHORITY_PEER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        ));
    }

    SUBCASE("a node carrying no entity has no controller and admits nobody") {
        CHECK_FALSE(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::CONTROLLER),
            AUTHORITY_PEER,
            AUTHORITY_PEER,
            0
        ));
        CHECK_FALSE(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::CONTROLLER),
            CONTROLLER_PEER,
            AUTHORITY_PEER,
            0
        ));
    }

    SUBCASE("an open policy admits every peer, including one nobody knows") {
        CHECK(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::ANY_PEER),
            OTHER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        ));
    }

    SUBCASE("a policy ordinal naming nothing admits nobody") {
        CHECK_FALSE(NetwEntityControl::policy_admits(
            99,
            AUTHORITY_PEER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        ));
    }
}

} // namespace TestNetwEntityControl

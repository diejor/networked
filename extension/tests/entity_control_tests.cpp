#include "support/netw_test.h"

#include "netw/entity/control.hpp"

using namespace godot;

namespace TestControl {

using godot::Ref;
using netw::entity::Control;

constexpr int64_t REPRESENTED = 42;
constexpr int64_t OTHER = 7;

TEST_CASE(
    "[Networked][Entity][Hosted] C1 an unwritten controller answers the rule, "
    "and a write answers itself"
) {
    Control held;
    Control *const control = &held;
    NETW_CHECK_EQ(control->resolve(REPRESENTED), 0);

    control->initial = int(Control::InitialController::REPRESENTED_PEER);
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
    Control held;
    Control *const control = &held;

    CHECK(control->set_controller(REPRESENTED));
    CHECK_FALSE(control->set_controller(REPRESENTED));
    CHECK(control->set_controller(0));
    CHECK_FALSE(control->set_controller(0));
}

TEST_CASE(
    "[Networked][Entity][Hosted] C3 the local peer steers only what it is "
    "resolved to steer, and no session steers nothing"
) {
    Control held;
    Control *const control = &held;
    control->initial = int(Control::InitialController::REPRESENTED_PEER);

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
    Control held;
    Control *const control = &held;
    CHECK_FALSE(control->admits_request());

    control->transfer = int(Control::Transfer::REQUESTABLE);
    CHECK(control->admits_request());
}

TEST_CASE(
    "[Networked][Entity][Hosted] C5 representation outranks control when its "
    "peer disconnects"
) {
    Control held;
    Control *const control = &held;
    control->set_controller(REPRESENTED);

    NETW_CHECK_EQ(
        control->disconnect_verdict(REPRESENTED, REPRESENTED),
        Control::DESPAWN_REPRESENTED
    );
    control->on_disconnect = int(Control::DisconnectRule::DESPAWN);
    NETW_CHECK_EQ(
        control->disconnect_verdict(REPRESENTED, REPRESENTED),
        Control::DESPAWN_REPRESENTED
    );

    Control held_npc;
    Control *const npc = &held_npc;
    npc->set_controller(OTHER);
    NETW_CHECK_EQ(npc->disconnect_verdict(OTHER, 0), Control::REVERT_TO_SERVER);
    npc->on_disconnect = int(Control::DisconnectRule::DESPAWN);
    NETW_CHECK_EQ(
        npc->disconnect_verdict(OTHER, 0),
        Control::DESPAWN_CONTROLLER
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] C6 a disconnect that reaches neither role "
    "asks for nothing"
) {
    Control held;
    Control *const control = &held;
    control->set_controller(REPRESENTED);
    control->on_disconnect = int(Control::DisconnectRule::DESPAWN);

    NETW_CHECK_EQ(
        control->disconnect_verdict(OTHER, REPRESENTED),
        Control::NOTHING
    );

    NETW_CHECK_EQ(Control().disconnect_verdict(0, 0), Control::NOTHING);
}

TEST_CASE(
    "[Networked][Entity][Hosted] the write policy answers the same question "
    "for a receiver and for an author"
) {
    const int64_t AUTHORITY_PEER = 1;
    const int64_t CONTROLLER_PEER = REPRESENTED;

    CHECK(
        Control::policy_admits(
            int(Control::WritePolicy::AUTHORITY),
            AUTHORITY_PEER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        )
    );
    CHECK_FALSE(
        Control::policy_admits(
            int(Control::WritePolicy::AUTHORITY),
            CONTROLLER_PEER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        )
    );

    SUBCASE("a controller policy answers for the controller and nobody else") {
        CHECK(
            Control::policy_admits(
                int(Control::WritePolicy::CONTROLLER),
                CONTROLLER_PEER,
                AUTHORITY_PEER,
                CONTROLLER_PEER
            )
        );
        CHECK_FALSE(
            Control::policy_admits(
                int(Control::WritePolicy::CONTROLLER),
                OTHER,
                AUTHORITY_PEER,
                CONTROLLER_PEER
            )
        );
        CHECK_FALSE(
            Control::policy_admits(
                int(Control::WritePolicy::CONTROLLER),
                AUTHORITY_PEER,
                AUTHORITY_PEER,
                CONTROLLER_PEER
            )
        );
    }

    SUBCASE("a node carrying no entity has no controller and admits nobody") {
        CHECK_FALSE(
            Control::policy_admits(
                int(Control::WritePolicy::CONTROLLER),
                AUTHORITY_PEER,
                AUTHORITY_PEER,
                0
            )
        );
        CHECK_FALSE(
            Control::policy_admits(
                int(Control::WritePolicy::CONTROLLER),
                CONTROLLER_PEER,
                AUTHORITY_PEER,
                0
            )
        );
    }

    SUBCASE("an open policy admits every peer, including one nobody knows") {
        CHECK(
            Control::policy_admits(
                int(Control::WritePolicy::ANY_PEER),
                OTHER,
                AUTHORITY_PEER,
                CONTROLLER_PEER
            )
        );
    }

    SUBCASE("a policy ordinal naming nothing admits nobody") {
        CHECK_FALSE(
            Control::policy_admits(
                99,
                AUTHORITY_PEER,
                AUTHORITY_PEER,
                CONTROLLER_PEER
            )
        );
    }
}

} // namespace TestControl

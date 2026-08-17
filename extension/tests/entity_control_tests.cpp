// The entity control machine's laws.
//
// Four of the five below are about a question being asked BEFORE the answer is
// stored. An entity is configured, read, and reasoned about long before it
// arms, and the whole reason the controller resolves lazily is that a read
// then must answer what arm will do later. A core that answered 0 until arm
// would be right eventually and wrong for the entire window a game sets its
// entities up in.

#include "support/netw_test.h"

#include "netw/entity_control.hpp"

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

    // A pre-arm write is a choice, and arm resolving the rule again must not
    // overwrite it. `configured` is the whole of what tells the two apart.
    CHECK_FALSE(control->configured);
    control->set_controller(OTHER);
    CHECK(control->configured);
    NETW_CHECK_EQ(control->resolve(REPRESENTED), OTHER);

    // Including the write that happens to name the server.
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

    // The rule alone is enough: an entity reads as locally controlled before
    // anything wrote a controller onto it.
    CHECK(control->controlled_by(REPRESENTED, REPRESENTED));
    CHECK_FALSE(control->controlled_by(OTHER, REPRESENTED));

    // Offline, or before a peer id exists, nobody is the local peer, and a
    // server-controlled entity is steered by nobody at all.
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

    // The peer both represents and steers this entity. The entity IS that
    // player, so it leaves with them whatever the disconnect rule says.
    NETW_CHECK_EQ(
        control->disconnect_verdict(REPRESENTED, REPRESENTED),
        NetwEntityControl::DESPAWN_REPRESENTED
    );
    control->on_disconnect = int(DisconnectRule::DESPAWN);
    NETW_CHECK_EQ(
        control->disconnect_verdict(REPRESENTED, REPRESENTED),
        NetwEntityControl::DESPAWN_REPRESENTED
    );

    // A server-owned entity a peer merely steers is the rule's to decide.
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

    // The server is not a peer that can disconnect, and an entity representing
    // nobody and steered by nobody must not read a server-valued controller as
    // a match for one.
    NETW_CHECK_EQ(
        fresh()->disconnect_verdict(0, 0),
        NetwEntityControl::NOTHING
    );
}

TEST_CASE(
    "[Networked][Entity][Hosted] the write policy answers the same question "
    "for a receiver and for an author"
) {
    // A receiver asks whether the peer that sent a write was allowed to. An
    // author asks whether it is the peer that may write at all, which is the
    // same question with its own id as the sender.
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
        // The authority is not the controller by being the authority.
        CHECK_FALSE(NetwEntityControl::policy_admits(
            int(netw::WritePolicy::CONTROLLER),
            AUTHORITY_PEER,
            AUTHORITY_PEER,
            CONTROLLER_PEER
        ));
    }

    SUBCASE("a node carrying no entity has no controller and admits nobody") {
        // Zero is not a peer, so the controller policy refuses every sender
        // rather than admitting one that happens to compare equal.
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

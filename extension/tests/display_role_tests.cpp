// Laws for NetwDisplayRoleFacts.
//
// The facts are readings; what this class protects is the ORDER they are
// consulted in, which is not obvious and is where every past display bug
// lived. Each case below states one rung of the ladder against the rung it
// outranks, and the last one holds the whole ladder to answering a real role
// for every state its facts can be in.

#include "support/netw_test.h"

#include "netw/display_decl.hpp"

namespace TestNetwDisplayRole {

using namespace godot;
using netw::NetwDisplayDecl;
using netw::NetwDisplayRoleFacts;

Ref<NetwDisplayRoleFacts> facts() {
    Ref<NetwDisplayRoleFacts> out;
    out.instantiate();
    return out;
}

TEST_CASE(
    "[Networked][Display][Hosted] L1 a locally simulated entity displays what "
    "it predicts, before authority is asked at all"
) {
    Ref<NetwDisplayRoleFacts> f = facts();
    f->set_simulates_locally(true);
    f->set_controlled_locally(true);
    f->set_owner_is_authority(true);
    f->set_authors_streams(true);

    NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_PREDICTED));

    SUBCASE("predicted input predicts even without local control") {
        f->set_controlled_locally(false);
        f->set_predicted_input(true);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_PREDICTED));
    }

    SUBCASE("neither control nor predicted input is not a prediction") {
        f->set_controlled_locally(false);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_AUTHORITY));
    }

    SUBCASE("control without local simulation is not a prediction either") {
        f->set_simulates_locally(false);
        f->set_authors_streams(false);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_DISABLED));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L2 a registered prediction that still "
    "simulates displays its own authority"
) {
    // Simulating something nobody here controls: a server stepping a client's
    // predicted entity. Local control or predicted input would have made it a
    // prediction one rung earlier.
    Ref<NetwDisplayRoleFacts> f = facts();
    f->set_prediction_registered(true);
    f->set_simulates_locally(true);

    NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_AUTHORITY));

    SUBCASE("and it does so without authoring a stream") {
        f->set_authors_streams(false);
        f->set_owner_is_authority(true);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_AUTHORITY));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L3 a demoted prediction displays as remote "
    "rather than going dark"
) {
    // It has stopped simulating and consumes the same replicated stream a
    // remote peer consumes, so it displays as remote even on the peer holding
    // its authority and its control. The disable rung would black it out and
    // nothing else writes the display once the simulation stops.
    Ref<NetwDisplayRoleFacts> f = facts();
    f->set_prediction_registered(true);
    f->set_simulates_locally(false);
    f->set_authors_streams(false);
    f->set_owner_is_authority(true);
    f->set_controlled_locally(true);

    NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_REMOTE));

    SUBCASE("one that authors its streams falls through to the ladder") {
        f->set_authors_streams(true);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_DISABLED));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L4 holding authority over what you control "
    "outranks authoring the stream"
) {
    Ref<NetwDisplayRoleFacts> f = facts();
    f->set_owner_is_authority(true);
    f->set_controlled_locally(true);
    f->set_authors_streams(true);

    NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_DISABLED));

    SUBCASE("without local control the authored stream wins") {
        f->set_controlled_locally(false);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_AUTHORITY));
    }

    SUBCASE("with neither, authority alone disables") {
        f->set_controlled_locally(false);
        f->set_authors_streams(false);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_DISABLED));
    }

    SUBCASE("a peer that neither authors nor holds authority is remote") {
        f->set_owner_is_authority(false);
        f->set_controlled_locally(false);
        f->set_authors_streams(false);
        NETW_CHECK_EQ(f->resolve(), int(NetwDisplayDecl::ROLE_REMOTE));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] the ladder answers a real role for every "
    "state its facts can be in"
) {
    // Six booleans is 64 states and the ladder must be total over all of
    // them: AUTO is a request and never an answer, and a fall-through that
    // reached the end of the ladder without returning would name nothing.
    Ref<NetwDisplayRoleFacts> f = facts();
    int seen[NetwDisplayDecl::ROLE_MAX] = {0, 0, 0, 0, 0};

    for (int state = 0; state < 64; ++state) {
        f->set_simulates_locally((state & 1) != 0);
        f->set_controlled_locally((state & 2) != 0);
        f->set_predicted_input((state & 4) != 0);
        f->set_prediction_registered((state & 8) != 0);
        f->set_authors_streams((state & 16) != 0);
        f->set_owner_is_authority((state & 32) != 0);

        const int role = f->resolve();
        const bool names_a_role
            = role > NetwDisplayDecl::ROLE_AUTO
            && role < NetwDisplayDecl::ROLE_MAX;
        CHECK(names_a_role);
        if (!names_a_role) {
            break;
        }
        seen[role] += 1;
        // Reading it twice from the same facts answers the same role.
        NETW_CHECK_EQ(f->resolve(), role);
    }

    // Every role is reachable, so no rung is dead.
    NETW_CHECK_GT(seen[NetwDisplayDecl::ROLE_REMOTE], 0);
    NETW_CHECK_GT(seen[NetwDisplayDecl::ROLE_PREDICTED], 0);
    NETW_CHECK_GT(seen[NetwDisplayDecl::ROLE_DISABLED], 0);
    NETW_CHECK_GT(seen[NetwDisplayDecl::ROLE_AUTHORITY], 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] L5 the pump a role is driven by, and the two "
    "roles that share one"
) {
    Ref<NetwDisplayDecl> decl;
    decl.instantiate();

    // Both play back a simulation this peer advances, a tick behind. They
    // differ in what they sample, not in how it is driven.
    NETW_CHECK_EQ(
        decl->pump_for(NetwDisplayDecl::ROLE_AUTHORITY),
        int(NetwDisplayDecl::PUMP_BRACKETED)
    );
    decl->set_param(
        NetwDisplayDecl::PARAM_PREDICTED_MODE,
        NetwDisplayDecl::PREDICTED_BRACKETED
    );
    NETW_CHECK_EQ(
        decl->pump_for(NetwDisplayDecl::ROLE_PREDICTED),
        int(NetwDisplayDecl::PUMP_BRACKETED)
    );

    SUBCASE("a chasing prediction is the other pump") {
        decl->set_param(
            NetwDisplayDecl::PARAM_PREDICTED_MODE,
            NetwDisplayDecl::PREDICTED_CHASE
        );
        NETW_CHECK_EQ(
            decl->pump_for(NetwDisplayDecl::ROLE_PREDICTED),
            int(NetwDisplayDecl::PUMP_CHASE)
        );
        // The predicted mode says nothing about any other role.
        NETW_CHECK_EQ(
            decl->pump_for(NetwDisplayDecl::ROLE_AUTHORITY),
            int(NetwDisplayDecl::PUMP_BRACKETED)
        );
    }

    SUBCASE("remote has its own, and everything else runs nothing") {
        NETW_CHECK_EQ(
            decl->pump_for(NetwDisplayDecl::ROLE_REMOTE),
            int(NetwDisplayDecl::PUMP_REMOTE)
        );
        NETW_CHECK_EQ(
            decl->pump_for(NetwDisplayDecl::ROLE_DISABLED),
            int(NetwDisplayDecl::PUMP_DISABLED)
        );
        // AUTO is a request rather than a role, so it drives nothing either.
        NETW_CHECK_EQ(
            decl->pump_for(NetwDisplayDecl::ROLE_AUTO),
            int(NetwDisplayDecl::PUMP_DISABLED)
        );
    }
}

} // namespace TestNetwDisplayRole

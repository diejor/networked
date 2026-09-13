#include "support/netw_test.h"

#include "netw/display/decl.hpp"
#include "netw/display/role_facts.hpp"

namespace TestNetwDisplayRole {

using namespace godot;
using netw::display::Decl;
using netw::display::resolve_role_facts;
using netw::display::RoleFacts;

TEST_CASE(
    "[Networked][Display][Hosted] L1 a locally simulated entity displays what "
    "it predicts, before authority is asked at all"
) {
    RoleFacts f;
    f.simulates_locally = true;
    f.controlled_locally = true;
    f.owner_is_authority = true;
    f.authors_streams = true;

    NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_PREDICTED));

    SUBCASE("predicted input predicts even without local control") {
        f.controlled_locally = false;
        f.predicted_input = true;
        NETW_CHECK_EQ(
            resolve_role_facts(f),
            int(netw::display::ROLE_PREDICTED)
        );
    }

    SUBCASE("neither control nor predicted input is not a prediction") {
        f.controlled_locally = false;
        NETW_CHECK_EQ(
            resolve_role_facts(f),
            int(netw::display::ROLE_AUTHORITY)
        );
    }

    SUBCASE("control without local simulation is not a prediction either") {
        f.simulates_locally = false;
        f.authors_streams = false;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L2 a registered prediction that still "
    "simulates displays its own authority"
) {
    RoleFacts f;
    f.prediction_registered = true;
    f.simulates_locally = true;

    NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_AUTHORITY));

    SUBCASE("and it does so without authoring a stream") {
        f.authors_streams = false;
        f.owner_is_authority = true;
        NETW_CHECK_EQ(
            resolve_role_facts(f),
            int(netw::display::ROLE_AUTHORITY)
        );
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L3 a demoted prediction displays as remote "
    "rather than going dark"
) {
    RoleFacts f;
    f.prediction_registered = true;
    f.simulates_locally = false;
    f.authors_streams = false;
    f.owner_is_authority = true;
    f.controlled_locally = true;

    NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_REMOTE));

    SUBCASE("one that authors its streams falls through to the ladder") {
        f.authors_streams = true;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] L4 holding authority over what you control "
    "outranks authoring the stream"
) {
    RoleFacts f;
    f.owner_is_authority = true;
    f.controlled_locally = true;
    f.authors_streams = true;

    NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));

    SUBCASE("without local control the authored stream wins") {
        f.controlled_locally = false;
        NETW_CHECK_EQ(
            resolve_role_facts(f),
            int(netw::display::ROLE_AUTHORITY)
        );
    }

    SUBCASE("with neither, authority alone disables") {
        f.controlled_locally = false;
        f.authors_streams = false;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_DISABLED));
    }

    SUBCASE("a peer that neither authors nor holds authority is remote") {
        f.owner_is_authority = false;
        f.controlled_locally = false;
        f.authors_streams = false;
        NETW_CHECK_EQ(resolve_role_facts(f), int(netw::display::ROLE_REMOTE));
    }
}

TEST_CASE(
    "[Networked][Display][Hosted] the ladder answers a real role for every "
    "state its facts can be in"
) {
    RoleFacts f;
    int seen[netw::display::ROLE_MAX] = {0, 0, 0, 0, 0};

    for (int state = 0; state < 64; ++state) {
        f.simulates_locally = (state & 1) != 0;
        f.controlled_locally = (state & 2) != 0;
        f.predicted_input = (state & 4) != 0;
        f.prediction_registered = (state & 8) != 0;
        f.authors_streams = (state & 16) != 0;
        f.owner_is_authority = (state & 32) != 0;

        const int role = resolve_role_facts(f);
        const bool names_a_role
            = role > netw::display::ROLE_AUTO && role < netw::display::ROLE_MAX;
        CHECK(names_a_role);
        if (!names_a_role) {
            break;
        }
        seen[role] += 1;
        NETW_CHECK_EQ(resolve_role_facts(f), role);
    }

    NETW_CHECK_GT(seen[netw::display::ROLE_REMOTE], 0);
    NETW_CHECK_GT(seen[netw::display::ROLE_PREDICTED], 0);
    NETW_CHECK_GT(seen[netw::display::ROLE_DISABLED], 0);
    NETW_CHECK_GT(seen[netw::display::ROLE_AUTHORITY], 0);
}

TEST_CASE(
    "[Networked][Display][Hosted] L5 the pump a role is driven by, and the two "
    "roles that share one"
) {
    Decl decl;

    NETW_CHECK_EQ(
        decl.pump_for(netw::display::ROLE_AUTHORITY),
        int(netw::display::PUMP_BRACKETED)
    );
    decl.set_param(
        netw::display::PARAM_PREDICTED_MODE,
        netw::display::PREDICTED_BRACKETED
    );
    NETW_CHECK_EQ(
        decl.pump_for(netw::display::ROLE_PREDICTED),
        int(netw::display::PUMP_BRACKETED)
    );

    SUBCASE("a chasing prediction is the other pump") {
        decl.set_param(
            netw::display::PARAM_PREDICTED_MODE,
            netw::display::PREDICTED_CHASE
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_PREDICTED),
            int(netw::display::PUMP_CHASE)
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_AUTHORITY),
            int(netw::display::PUMP_BRACKETED)
        );
    }

    SUBCASE("remote has its own, and everything else runs nothing") {
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_REMOTE),
            int(netw::display::PUMP_REMOTE)
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_DISABLED),
            int(netw::display::PUMP_DISABLED)
        );
        NETW_CHECK_EQ(
            decl.pump_for(netw::display::ROLE_AUTO),
            int(netw::display::PUMP_DISABLED)
        );
    }
}

} // namespace TestNetwDisplayRole
